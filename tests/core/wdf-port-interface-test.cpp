#include <doctest/doctest.h>

#include "primitives/circuit/wdf/one-port.h"

// WDF port interface contract suite (wdf-primitives feature, T002/T003).
//
// Covers the duck-typed OnePort interface: portResistance() read accessor,
// incident(a) absorption contract, reflected() return value contract, and the
// isAdaptable trait. Test cases added by T003+.
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

}  // namespace

TEST_CASE("OnePort concept trait accepts a conforming duck-typed stub") {
    CHECK(acfx::wdf::is_one_port_v<ValidOnePortStub>);
}

TEST_CASE("OnePort concept trait rejects stubs missing required members") {
    CHECK_FALSE(acfx::wdf::is_one_port_v<MissingIncidentStub>);
    CHECK_FALSE(acfx::wdf::is_one_port_v<MissingPortResistanceStub>);
    CHECK_FALSE(acfx::wdf::is_one_port_v<MissingIsAdaptableStub>);
    CHECK_FALSE(acfx::wdf::is_one_port_v<NotNoexceptReflectedStub>);
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
