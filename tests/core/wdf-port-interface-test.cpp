#include <doctest/doctest.h>

#include <tuple>
#include <type_traits>

#include "primitives/circuit/wdf/one-port.h"
#include "primitives/circuit/wdf/wave-elements.h"
#include "primitives/circuit/wdf/wave-sources.h"
#include "primitives/circuit/wdf/wave-terminations.h"

// WDF port interface contract suite (wdf-primitives feature, T002/T003/T013).
//
// Covers the duck-typed OnePort interface: portResistance() read accessor,
// incident(a) absorption contract, reflected() return value contract, and the
// isAdaptable trait. Test cases added by T003+.
//
// T013 (US5, SC-005) extends this file with a generic, TEMPLATED up-sweep/
// down-sweep driver exercised over all 8 leaves via a heterogeneous
// std::tuple + compile-time parameter-pack fold, proving the uniform ABI the
// adaptor node will consume: no virtual dispatch (I3), and the isAdaptable
// call-ordering contract (I2) honored per-leaf via `if constexpr`.
//
// T004 (core/primitives/circuit/wdf/one-port.h, namespace acfx::wdf) MUST
// define EXACTLY the following names, matched by this test:
//
//   template <typename T>
//   struct is_one_port { static constexpr bool value = /* ... */; };
//
//   template <typename T>
//   inline constexpr bool is_one_port_v = is_one_port<T>::value;
//
//     A C++17 concept-emulation trait (SFINAE / std::void_t) that is `true`
//     iff T exposes all of:
//       double portResistance() const noexcept;
//       double reflected()      const noexcept;
//       void   incident(double) noexcept;
//       static constexpr bool isAdaptable;   // (any type contextually
//                                             //  convertible to bool)
//     and `false` otherwise (missing member, wrong signature, or a member
//     that is not noexcept).
//
//   double waveToVoltage(double a, double b) noexcept;
//     Voltage-wave inverse helper (contract W1): v = (a + b) / 2.
//
//   double waveToCurrent(double a, double b, double Rp) noexcept;
//     Voltage-wave inverse helper (contract W1): i = (a - b) / (2 * Rp).
//
// This test file intentionally does NOT compile until T004 lands
// (`primitives/circuit/wdf/one-port.h` does not exist yet) -- that is the
// expected RED state for this task.

namespace {

// --- (a) compile-time / trait check -----------------------------------

// A trivial stub type that satisfies the intended OnePort duck-type: all
// three wave-path members present with the contracted noexcept signatures,
// plus the isAdaptable trait.
struct ValidOnePortStub {
    double portResistance() const noexcept { return 1.0; }
    double reflected() const noexcept { return 0.0; }
    void incident(double /*a*/) noexcept {}
    static constexpr bool isAdaptable = true;
};

// Missing incident(double) entirely -- must NOT satisfy the trait.
struct MissingIncidentStub {
    double portResistance() const noexcept { return 1.0; }
    double reflected() const noexcept { return 0.0; }
    static constexpr bool isAdaptable = true;
};

// Missing portResistance() entirely -- must NOT satisfy the trait.
struct MissingPortResistanceStub {
    double reflected() const noexcept { return 0.0; }
    void incident(double /*a*/) noexcept {}
    static constexpr bool isAdaptable = false;
};

// Missing the isAdaptable trait entirely -- must NOT satisfy the trait.
struct MissingIsAdaptableStub {
    double portResistance() const noexcept { return 1.0; }
    double reflected() const noexcept { return 0.0; }
    void incident(double /*a*/) noexcept {}
};

// reflected() is not noexcept -- must NOT satisfy the trait (I1 requires
// every wave-path member to be noexcept).
struct NotNoexceptReflectedStub {
    double portResistance() const noexcept { return 1.0; }
    double reflected() const { return 0.0; }  // NOT noexcept -- disqualifying
    void incident(double /*a*/) noexcept {}
    static constexpr bool isAdaptable = true;
};

// isAdaptable is a NON-STATIC data member -- otherwise fully conforming, but
// `if constexpr (T::isAdaptable)` in sibling adaptor code needs a compile-time
// constant, so this MUST NOT satisfy the trait (AUDIT-20260710-01). A plain
// `decltype(T::isAdaptable)` is still well-formed for a non-static member, so
// the tightened std::bool_constant<T::isAdaptable> gate is what rejects it.
struct NonStaticIsAdaptableStub {
    double portResistance() const noexcept { return 1.0; }
    double reflected() const noexcept { return 0.0; }
    void incident(double /*a*/) noexcept {}
    bool isAdaptable = true;  // NON-static -- not a compile-time constant
};

// isAdaptable is a NON-constexpr static -- otherwise fully conforming, but it is
// NOT a compile-time constant, so `if constexpr (T::isAdaptable)` would fail at
// the adaptor site; this MUST NOT satisfy the trait (AUDIT-20260710-01).
struct NonConstexprStaticIsAdaptableStub {
    double portResistance() const noexcept { return 1.0; }
    double reflected() const noexcept { return 0.0; }
    void incident(double /*a*/) noexcept {}
    static bool isAdaptable;  // NON-constexpr static -- not a compile-time constant
};
bool NonConstexprStaticIsAdaptableStub::isAdaptable = true;

static_assert(acfx::wdf::is_one_port_v<ValidOnePortStub>,
              "ValidOnePortStub exposes the full noexcept OnePort surface "
              "and must satisfy is_one_port_v");
static_assert(!acfx::wdf::is_one_port_v<MissingIncidentStub>,
              "MissingIncidentStub lacks incident(double) and must NOT "
              "satisfy is_one_port_v");
static_assert(!acfx::wdf::is_one_port_v<MissingPortResistanceStub>,
              "MissingPortResistanceStub lacks portResistance() and must "
              "NOT satisfy is_one_port_v");
static_assert(!acfx::wdf::is_one_port_v<MissingIsAdaptableStub>,
              "MissingIsAdaptableStub lacks the isAdaptable trait and must "
              "NOT satisfy is_one_port_v");
static_assert(!acfx::wdf::is_one_port_v<NotNoexceptReflectedStub>,
              "NotNoexceptReflectedStub's reflected() is not noexcept and "
              "must NOT satisfy is_one_port_v");
static_assert(!acfx::wdf::is_one_port_v<NonStaticIsAdaptableStub>,
              "NonStaticIsAdaptableStub's isAdaptable is a non-static data "
              "member (not a compile-time constant) and must NOT satisfy "
              "is_one_port_v (AUDIT-20260710-01)");
static_assert(!acfx::wdf::is_one_port_v<NonConstexprStaticIsAdaptableStub>,
              "NonConstexprStaticIsAdaptableStub's isAdaptable is a "
              "non-constexpr static (not a compile-time constant) and must NOT "
              "satisfy is_one_port_v (AUDIT-20260710-01)");

}  // namespace

TEST_CASE("OnePort concept trait accepts a conforming duck-typed stub") {
    CHECK(acfx::wdf::is_one_port_v<ValidOnePortStub>);
}

TEST_CASE("OnePort concept trait rejects stubs missing required members") {
    CHECK_FALSE(acfx::wdf::is_one_port_v<MissingIncidentStub>);
    CHECK_FALSE(acfx::wdf::is_one_port_v<MissingPortResistanceStub>);
    CHECK_FALSE(acfx::wdf::is_one_port_v<MissingIsAdaptableStub>);
    CHECK_FALSE(acfx::wdf::is_one_port_v<NotNoexceptReflectedStub>);
    CHECK_FALSE(acfx::wdf::is_one_port_v<NonStaticIsAdaptableStub>);
    CHECK_FALSE(acfx::wdf::is_one_port_v<NonConstexprStaticIsAdaptableStub>);
}

// --- (b) runtime check of the voltage-wave inverse helpers --------------

TEST_CASE("waveToVoltage recovers v = (a + b) / 2") {
    CHECK(acfx::wdf::waveToVoltage(1.0, 3.0) == doctest::Approx(2.0));
    CHECK(acfx::wdf::waveToVoltage(-2.0, 2.0) == doctest::Approx(0.0));
    CHECK(acfx::wdf::waveToVoltage(0.0, 0.0) == doctest::Approx(0.0));
}

TEST_CASE("waveToCurrent recovers i = (a - b) / (2 * Rp)") {
    const double Rp = 5.0;
    CHECK(acfx::wdf::waveToCurrent(1.0, 3.0, Rp) == doctest::Approx((1.0 - 3.0) / (2.0 * Rp)));
    CHECK(acfx::wdf::waveToCurrent(4.0, 0.0, 2.0) == doctest::Approx(1.0));
    CHECK(acfx::wdf::waveToCurrent(0.0, 0.0, 1.0) == doctest::Approx(0.0));
}

// --- (c) T013 (US5, SC-005): generic templated port-interface sweep over ALL leaves ---

namespace {

using acfx::wdf::Capacitor;
using acfx::wdf::Inductor;
using acfx::wdf::OpenCircuit;
using acfx::wdf::ResistiveCurrentSource;
using acfx::wdf::ResistiveTermination;
using acfx::wdf::ResistiveVoltageSource;
using acfx::wdf::Resistor;
using acfx::wdf::ShortCircuit;
using acfx::wdf::is_one_port_v;

// I3 (static dispatch) + OnePort-concept confirmation, restated here (in
// addition to the per-header static_asserts) so this generic-sweep test file
// stands alone as the SC-005 proof: all 8 leaves satisfy is_one_port_v, and
// NONE is polymorphic -- there is no vtable anywhere on the wave path, so any
// dispatch through the templated driver below is necessarily static.
static_assert(is_one_port_v<Resistor> && !std::is_polymorphic_v<Resistor>,
              "Resistor must satisfy OnePort with no vtable");
static_assert(is_one_port_v<Capacitor> && !std::is_polymorphic_v<Capacitor>,
              "Capacitor must satisfy OnePort with no vtable");
static_assert(is_one_port_v<Inductor> && !std::is_polymorphic_v<Inductor>,
              "Inductor must satisfy OnePort with no vtable");
static_assert(is_one_port_v<ResistiveVoltageSource> && !std::is_polymorphic_v<ResistiveVoltageSource>,
              "ResistiveVoltageSource must satisfy OnePort with no vtable");
static_assert(is_one_port_v<ResistiveCurrentSource> && !std::is_polymorphic_v<ResistiveCurrentSource>,
              "ResistiveCurrentSource must satisfy OnePort with no vtable");
static_assert(is_one_port_v<ResistiveTermination> && !std::is_polymorphic_v<ResistiveTermination>,
              "ResistiveTermination must satisfy OnePort with no vtable");
static_assert(is_one_port_v<ShortCircuit> && !std::is_polymorphic_v<ShortCircuit>,
              "ShortCircuit must satisfy OnePort with no vtable");
static_assert(is_one_port_v<OpenCircuit> && !std::is_polymorphic_v<OpenCircuit>,
              "OpenCircuit must satisfy OnePort with no vtable");

// driveOneSample<Leaf> -- the single templated up-sweep/down-sweep driver
// (I3): dispatches through the uniform portResistance()/reflected()/
// incident() surface for ANY leaf that satisfies is_one_port_v, resolved
// entirely at compile time (a template instantiation per Leaf, `if
// constexpr` branching on the leaf's own static isAdaptable, NEVER a runtime
// vtable lookup). Honors the I2 call-ordering contract:
//   - isAdaptable == true  (adaptable):  reflected() is read BEFORE this
//       sample's incident() -- it observes stored state / a constant, THEN
//       the sample's incident wave is absorbed.
//   - isAdaptable == false (reflective): incident(a) is called FIRST, THEN
//       reflected() is read -- it returns f(a) (root-evaluation order).
// Returns the reflected wave b produced by this sample.
template <class Leaf>
double driveOneSample(Leaf& leaf, double a) noexcept {
    static_assert(is_one_port_v<Leaf>,
                  "driveOneSample requires Leaf to satisfy the OnePort concept trait (I1)");
    static_assert(!std::is_polymorphic_v<Leaf>,
                  "driveOneSample requires a non-polymorphic Leaf -- no vtable / virtual "
                  "dispatch is permitted on the wave path (I3)");
    if constexpr (Leaf::isAdaptable) {
        const double b = leaf.reflected();  // I2: valid BEFORE this sample's incident()
        leaf.incident(a);
        return b;
    } else {
        leaf.incident(a);        // I2: incident(a) called FIRST for reflective leaves
        return leaf.reflected();  // then reflected() = f(a)
    }
}

}  // namespace

TEST_CASE(
    "Generic templated driver sweeps all 8 leaves through the uniform OnePort "
    "interface with no virtual dispatch, honoring the isAdaptable call-ordering "
    "contract (T013, US5, SC-005)") {
    // A heterogeneous std::tuple of all 8 leaves in the family -- exactly the
    // "uniform ABI the adaptor node will consume" (SC-005). Order:
    // Resistor, Capacitor, Inductor, ResistiveVoltageSource,
    // ResistiveCurrentSource, ResistiveTermination, ShortCircuit, OpenCircuit.
    auto leaves = std::make_tuple(Resistor(100.0),
                                   Capacitor(1.0e-6, 1.0 / 48000.0),
                                   Inductor(1.0e-3, 1.0 / 48000.0),
                                   ResistiveVoltageSource(600.0, 5.0),
                                   ResistiveCurrentSource(600.0, 0.01),
                                   ResistiveTermination(50.0),
                                   ShortCircuit(50.0),
                                   OpenCircuit(50.0));

    const double a1 = 3.0;

    // Sample 1, driven through the SAME templated driver for every leaf via a
    // compile-time parameter-pack fold over the heterogeneous tuple
    // (std::apply): adaptable leaves reflect their stored/constant state
    // BEFORE absorbing a1; reflective leaves absorb a1 first, then reflect a
    // function of it (I2).
    auto b1 = std::apply(
        [a1](auto&... leaf) { return std::make_tuple(driveOneSample(leaf, a1)...); }, leaves);

    CHECK(std::get<0>(b1) == doctest::Approx(0.0));    // Resistor R2: adapted, always 0
    CHECK(std::get<1>(b1) == doctest::Approx(0.0));    // Capacitor C2: b[0] = 0 (initial state)
    CHECK(std::get<2>(b1) == doctest::Approx(0.0));    // Inductor L2: b[0] = 0 (initial state)
    CHECK(std::get<3>(b1) == doctest::Approx(5.0));    // VoltageSource VS1: b = E
    CHECK(std::get<4>(b1) == doctest::Approx(6.0));    // CurrentSource CS1: b = R*I = 600*0.01
    CHECK(std::get<5>(b1) == doctest::Approx(0.0));    // Termination RT1: matched load
    CHECK(std::get<6>(b1) == doctest::Approx(-a1));    // ShortCircuit SH2: b = -a (reflective)
    CHECK(std::get<7>(b1) == doctest::Approx(a1));     // OpenCircuit OP1: b = +a (reflective)

    const double a2 = -1.5;

    // Sample 2, driven through the same generic fold. The stateful leaves
    // (Capacitor/Inductor) now reflect sample 1's stored incident wave a1,
    // confirming the unit delay is observed correctly THROUGH the generic
    // templated driver, not just the leaf directly.
    auto b2 = std::apply(
        [a2](auto&... leaf) { return std::make_tuple(driveOneSample(leaf, a2)...); }, leaves);

    CHECK(std::get<0>(b2) == doctest::Approx(0.0));    // Resistor: still 0
    CHECK(std::get<1>(b2) == doctest::Approx(a1));     // Capacitor: b[1] = a[0]
    CHECK(std::get<2>(b2) == doctest::Approx(-a1));    // Inductor: b[1] = -a[0]
    CHECK(std::get<3>(b2) == doctest::Approx(5.0));    // VoltageSource: still E (memoryless)
    CHECK(std::get<4>(b2) == doctest::Approx(6.0));    // CurrentSource: still R*I (memoryless)
    CHECK(std::get<5>(b2) == doctest::Approx(0.0));    // Termination: still 0
    CHECK(std::get<6>(b2) == doctest::Approx(-a2));    // ShortCircuit: b = -a2
    CHECK(std::get<7>(b2) == doctest::Approx(a2));     // OpenCircuit: b = +a2
}
