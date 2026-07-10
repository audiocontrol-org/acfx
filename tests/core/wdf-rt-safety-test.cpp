#include <doctest/doctest.h>

#include <cmath>
#include <cstddef>
#include <stdexcept>
#include <type_traits>
#include <utility>

#include "primitives/circuit/wdf/one-port.h"
#include "primitives/circuit/wdf/wave-elements.h"
#include "primitives/circuit/wdf/wave-sources.h"
#include "primitives/circuit/wdf/wave-terminations.h"
#include "support/allocation-sentinel.h"

// WDF real-time-safety + no-fallback suite (wdf-primitives feature).
//
// T014 (US6, contract G1): the per-sample wave path (reflected()/incident())
// performs ZERO heap allocations/deallocations across a large batch of
// samples for every one of the 8 leaves (Resistor, Capacitor, Inductor,
// ResistiveVoltageSource, ResistiveCurrentSource, ResistiveTermination,
// ShortCircuit, OpenCircuit); a reactive leaf's portResistance() (Rp) is
// readable IMMEDIATELY after construction (computed once in the
// constructor, research R4 -- never lazily on first wave-path call); and
// there is NO per-leaf prepare()/setSampleRate() member in v1 (R4 -- a leaf
// is (re)configured only by reconstruction, off the hot path).
//
// T016 (US8, contract G2): every leaf throws std::invalid_argument at
// CONSTRUCTION (never a fallback/clamped/substituted value) when given a
// non-physical parameter (R/C/L/dt/Rp <= 0), and no leaf ever clamps its
// reflected wave to force apparent passivity -- an intentionally huge
// incident wave produces the exact (unclamped) scattering value.

using namespace acfx::wdf;
using acfx::test::AllocationSentinel;

// -----------------------------------------------------------------------------
// T014 -- API-surface (compile-time) detection: no per-leaf prepare()/
// setSampleRate() member exists in v1 (research R4). Detection is by member
// NAME (via &T::member), independent of signature, so it rejects a
// prepare()/setSampleRate() of ANY arity/return type -- not just a
// specific guessed signature.
// -----------------------------------------------------------------------------

namespace {

template <typename T, typename = void>
struct has_prepare_member : std::false_type {};
template <typename T>
struct has_prepare_member<T, std::void_t<decltype(&T::prepare)>> : std::true_type {};

template <typename T, typename = void>
struct has_set_sample_rate_member : std::false_type {};
template <typename T>
struct has_set_sample_rate_member<T, std::void_t<decltype(&T::setSampleRate)>>
    : std::true_type {};

}  // namespace

static_assert(!has_prepare_member<Resistor>::value, "Resistor must not have a prepare() member (R4)");
static_assert(!has_prepare_member<Capacitor>::value, "Capacitor must not have a prepare() member (R4)");
static_assert(!has_prepare_member<Inductor>::value, "Inductor must not have a prepare() member (R4)");
static_assert(!has_prepare_member<ResistiveVoltageSource>::value,
              "ResistiveVoltageSource must not have a prepare() member (R4)");
static_assert(!has_prepare_member<ResistiveCurrentSource>::value,
              "ResistiveCurrentSource must not have a prepare() member (R4)");
static_assert(!has_prepare_member<ResistiveTermination>::value,
              "ResistiveTermination must not have a prepare() member (R4)");
static_assert(!has_prepare_member<ShortCircuit>::value, "ShortCircuit must not have a prepare() member (R4)");
static_assert(!has_prepare_member<OpenCircuit>::value, "OpenCircuit must not have a prepare() member (R4)");

static_assert(!has_set_sample_rate_member<Resistor>::value,
              "Resistor must not have a setSampleRate() member (R4)");
static_assert(!has_set_sample_rate_member<Capacitor>::value,
              "Capacitor must not have a setSampleRate() member (R4)");
static_assert(!has_set_sample_rate_member<Inductor>::value,
              "Inductor must not have a setSampleRate() member (R4)");
static_assert(!has_set_sample_rate_member<ResistiveVoltageSource>::value,
              "ResistiveVoltageSource must not have a setSampleRate() member (R4)");
static_assert(!has_set_sample_rate_member<ResistiveCurrentSource>::value,
              "ResistiveCurrentSource must not have a setSampleRate() member (R4)");
static_assert(!has_set_sample_rate_member<ResistiveTermination>::value,
              "ResistiveTermination must not have a setSampleRate() member (R4)");
static_assert(!has_set_sample_rate_member<ShortCircuit>::value,
              "ShortCircuit must not have a setSampleRate() member (R4)");
static_assert(!has_set_sample_rate_member<OpenCircuit>::value,
              "OpenCircuit must not have a setSampleRate() member (R4)");

// -----------------------------------------------------------------------------
// T014 -- zero-heap wave path (G1). Drives a MANY-sample reflected()/
// incident() loop over a leaf, honoring its I2 call ordering (adaptable:
// reflected() before incident(); reflective: incident() before reflected()),
// entirely inside an AllocationSentinel scope. `drive` supplies the
// per-sample incident wave (and, for the two sources, also updates the
// per-sample drive value via `setDrive`, since VS2/the source setters are
// themselves audio-path calls).
// -----------------------------------------------------------------------------

namespace {

constexpr int kManySamples = 100000;

// a[n] sweep: varying sign and magnitude, deterministic (no RNG needed for
// an allocation-counting test).
double sampleWave(int n) {
    return 1000.0 * std::sin(0.001 * static_cast<double>(n)) - 0.5 * static_cast<double>(n % 7);
}

template <typename Leaf>
void runWavePathLoop(Leaf& leaf, int nSamples) {
    for (int n = 0; n < nSamples; ++n) {
        const double a = sampleWave(n);
        if constexpr (Leaf::isAdaptable) {
            const double b = leaf.reflected();  // valid BEFORE this sample's incident() (I2)
            leaf.incident(a);
            static_cast<void>(b);
        } else {
            leaf.incident(a);  // reflective: incident() first, THEN reflected() (I2)
            const double b = leaf.reflected();
            static_cast<void>(b);
        }
    }
}

template <typename Leaf>
void checkZeroHeapWavePath(Leaf& leaf, const char* leafName) {
    AllocationSentinel::reset();
    runWavePathLoop(leaf, kManySamples);
    const std::size_t allocations = AllocationSentinel::allocations();
    const std::size_t deallocations = AllocationSentinel::deallocations();

    CHECK_MESSAGE(allocations == 0, leafName, " wave path allocated ", allocations, " times");
    CHECK_MESSAGE(deallocations == 0, leafName, " wave path deallocated ", deallocations, " times");
}

}  // namespace

TEST_CASE("Resistor reflected()/incident() wave path allocates nothing over many samples") {
    Resistor r(600.0);
    checkZeroHeapWavePath(r, "Resistor");
}

TEST_CASE("Capacitor reflected()/incident() wave path allocates nothing over many samples") {
    Capacitor cap(1.0e-6, 1.0 / 48000.0);
    checkZeroHeapWavePath(cap, "Capacitor");
}

TEST_CASE("Inductor reflected()/incident() wave path allocates nothing over many samples") {
    Inductor ind(1.0e-3, 1.0 / 48000.0);
    checkZeroHeapWavePath(ind, "Inductor");
}

TEST_CASE("ResistiveVoltageSource reflected()/incident()/setVoltage() wave path allocates nothing") {
    ResistiveVoltageSource vs(50.0);
    AllocationSentinel::reset();
    for (int n = 0; n < kManySamples; ++n) {
        const double a = sampleWave(n);
        vs.setVoltage(0.5 * a);      // per-sample drive update -- also audio-path (VS2)
        const double b = vs.reflected();
        vs.incident(a);
        static_cast<void>(b);
    }
    const std::size_t allocations = AllocationSentinel::allocations();
    const std::size_t deallocations = AllocationSentinel::deallocations();
    CHECK_MESSAGE(allocations == 0, "ResistiveVoltageSource wave path allocated ", allocations, " times");
    CHECK_MESSAGE(deallocations == 0, "ResistiveVoltageSource wave path deallocated ", deallocations,
                  " times");
}

TEST_CASE("ResistiveCurrentSource reflected()/incident()/setCurrent() wave path allocates nothing") {
    ResistiveCurrentSource cs(50.0);
    AllocationSentinel::reset();
    for (int n = 0; n < kManySamples; ++n) {
        const double a = sampleWave(n);
        cs.setCurrent(0.01 * a);     // per-sample drive update -- also audio-path
        const double b = cs.reflected();
        cs.incident(a);
        static_cast<void>(b);
    }
    const std::size_t allocations = AllocationSentinel::allocations();
    const std::size_t deallocations = AllocationSentinel::deallocations();
    CHECK_MESSAGE(allocations == 0, "ResistiveCurrentSource wave path allocated ", allocations, " times");
    CHECK_MESSAGE(deallocations == 0, "ResistiveCurrentSource wave path deallocated ", deallocations,
                  " times");
}

TEST_CASE("ResistiveTermination reflected()/incident() wave path allocates nothing over many samples") {
    ResistiveTermination term(600.0);
    checkZeroHeapWavePath(term, "ResistiveTermination");
}

TEST_CASE("ShortCircuit reflected()/incident() wave path allocates nothing over many samples") {
    ShortCircuit sc(600.0);
    checkZeroHeapWavePath(sc, "ShortCircuit");
}

TEST_CASE("OpenCircuit reflected()/incident() wave path allocates nothing over many samples") {
    OpenCircuit oc(600.0);
    checkZeroHeapWavePath(oc, "OpenCircuit");
}

// -----------------------------------------------------------------------------
// T014 -- portResistance() readable immediately after construction (R4:
// Rp is computed ONCE in the constructor, never lazily).
// -----------------------------------------------------------------------------

TEST_CASE("Capacitor portResistance() == T/(2C) immediately after construction") {
    const double C = 2.2e-6;
    const double dt = 1.0 / 96000.0;
    CHECK(Capacitor(C, dt).portResistance() == doctest::Approx(dt / (2.0 * C)));
}

TEST_CASE("Inductor portResistance() == 2L/T immediately after construction") {
    const double L = 4.7e-3;
    const double dt = 1.0 / 96000.0;
    CHECK(Inductor(L, dt).portResistance() == doctest::Approx(2.0 * L / dt));
}

// =============================================================================
// T016 (US8, contract G2) -- no-fallback parameter validation.
// =============================================================================

TEST_CASE("Resistor throws std::invalid_argument for non-positive R (0 and negative)") {
    CHECK_THROWS_AS(Resistor(0.0), std::invalid_argument);
    CHECK_THROWS_AS(Resistor(-1.0), std::invalid_argument);
}

TEST_CASE("Capacitor throws std::invalid_argument for non-positive C or dt (0 and negative)") {
    const double dt = 1.0 / 48000.0;
    const double C = 1.0e-6;
    CHECK_THROWS_AS(Capacitor(0.0, dt), std::invalid_argument);
    CHECK_THROWS_AS(Capacitor(-1.0e-6, dt), std::invalid_argument);
    CHECK_THROWS_AS(Capacitor(C, 0.0), std::invalid_argument);
    CHECK_THROWS_AS(Capacitor(C, -dt), std::invalid_argument);
}

TEST_CASE("Inductor throws std::invalid_argument for non-positive L or dt (0 and negative)") {
    const double dt = 1.0 / 48000.0;
    const double L = 1.0e-3;
    CHECK_THROWS_AS(Inductor(0.0, dt), std::invalid_argument);
    CHECK_THROWS_AS(Inductor(-1.0e-3, dt), std::invalid_argument);
    CHECK_THROWS_AS(Inductor(L, 0.0), std::invalid_argument);
    CHECK_THROWS_AS(Inductor(L, -dt), std::invalid_argument);
}

TEST_CASE("ResistiveVoltageSource throws std::invalid_argument for non-positive R (0 and negative)") {
    CHECK_THROWS_AS(ResistiveVoltageSource(0.0), std::invalid_argument);
    CHECK_THROWS_AS(ResistiveVoltageSource(-50.0), std::invalid_argument);
}

TEST_CASE("ResistiveCurrentSource throws std::invalid_argument for non-positive R (0 and negative)") {
    CHECK_THROWS_AS(ResistiveCurrentSource(0.0), std::invalid_argument);
    CHECK_THROWS_AS(ResistiveCurrentSource(-50.0), std::invalid_argument);
}

TEST_CASE("ResistiveTermination throws std::invalid_argument for non-positive R (0 and negative)") {
    CHECK_THROWS_AS(ResistiveTermination(0.0), std::invalid_argument);
    CHECK_THROWS_AS(ResistiveTermination(-600.0), std::invalid_argument);
}

TEST_CASE("ShortCircuit throws std::invalid_argument for non-positive Rp (0 and negative)") {
    CHECK_THROWS_AS(ShortCircuit(0.0), std::invalid_argument);
    CHECK_THROWS_AS(ShortCircuit(-600.0), std::invalid_argument);
}

TEST_CASE("OpenCircuit throws std::invalid_argument for non-positive Rp (0 and negative)") {
    CHECK_THROWS_AS(OpenCircuit(0.0), std::invalid_argument);
    CHECK_THROWS_AS(OpenCircuit(-600.0), std::invalid_argument);
}

// -----------------------------------------------------------------------------
// T016 -- no leaf clamps its reflected wave: an intentionally LARGE incident
// wave produces the EXACT (unclamped) scattering value, never a value
// limited to force apparent |b| <= |a| passivity (G2, spec FR-016).
// -----------------------------------------------------------------------------

TEST_CASE("ShortCircuit reflected() == -a exactly, unclamped, for a huge incident wave") {
    ShortCircuit sc(600.0);
    for (double a : {1.0e9, -1.0e9, 1.0e300}) {
        sc.incident(a);
        CHECK(sc.reflected() == -a);  // exact, not Approx: pure negation, no clamping
    }
}

TEST_CASE("OpenCircuit reflected() == +a exactly, unclamped, for a huge incident wave") {
    OpenCircuit oc(600.0);
    for (double a : {1.0e9, -1.0e9, 1.0e300}) {
        oc.incident(a);
        CHECK(oc.reflected() == a);  // exact, not Approx: identity, no clamping
    }
}

TEST_CASE("Capacitor reflected() == a[n-1] exactly, unclamped, for a huge stored incident wave") {
    Capacitor cap(1.0e-6, 1.0 / 48000.0);
    for (double a : {1.0e9, -1.0e9, 1.0e300}) {
        cap.incident(a);
        CHECK(cap.reflected() == a);  // exact, not Approx: unit delay, no clamping
    }
}

TEST_CASE("Inductor reflected() == -a[n-1] exactly, unclamped, for a huge stored incident wave") {
    Inductor ind(1.0e-3, 1.0 / 48000.0);
    for (double a : {1.0e9, -1.0e9, 1.0e300}) {
        ind.incident(a);
        CHECK(ind.reflected() == -a);  // exact, not Approx: sign-inverted unit delay, no clamping
    }
}

TEST_CASE("ResistiveVoltageSource reflected() == E exactly, unclamped, for a huge drive voltage") {
    ResistiveVoltageSource vs(50.0);
    for (double E : {1.0e9, -1.0e9, 1.0e300}) {
        vs.setVoltage(E);
        CHECK(vs.reflected() == E);  // exact: no clamping of the drive value
    }
}

TEST_CASE("ResistiveCurrentSource reflected() == R*I exactly, unclamped, for a huge drive current") {
    const double R = 50.0;
    ResistiveCurrentSource cs(R);
    for (double I : {1.0e9, -1.0e9, 1.0e300}) {
        cs.setCurrent(I);
        CHECK(cs.reflected() == R * I);  // exact: no clamping of the drive value
    }
}
