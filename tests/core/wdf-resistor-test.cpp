#include <doctest/doctest.h>

#include <stdexcept>

#include "primitives/circuit/wdf/one-port.h"
#include "primitives/circuit/wdf/wave-elements.h"

// WDF resistor primitive suite (wdf-primitives feature, T005/T006).
//
// Covers resistor one-port construction, parameter validation (R > 0),
// reflected/incident wave contracts, and the OnePort concept trait
// (contracts/wdf-one-ports.md "Resistor" R1-R3; data-model.md "Resistor").
//
// T006 (core/primitives/circuit/wdf/wave-elements.h, namespace acfx::wdf)
// MUST define `Resistor`:
//
//   explicit Resistor(double R);
//   double portResistance() const noexcept;   // R
//   double reflected()      const noexcept;   // 0  (adapted)
//   void   incident(double a) noexcept;        // no-op (a absorbed)
//   static constexpr bool isAdaptable = true;
//
// This test file intentionally does NOT compile until T006 lands
// (`primitives/circuit/wdf/wave-elements.h` does not exist yet, and
// `acfx::wdf::Resistor` is undefined) -- that is the expected RED state
// for T005.

namespace {

// R3: the general unadapted reflection is an analytical TEST-LOCAL oracle
// only (NOT a public Resistor method) -- arbitrary reference resistance is
// the adaptor layer's concern (spec FR-002), not this leaf's.
double unadaptedReflection(double a, double R, double Rp) {
    return a * (R - Rp) / (R + Rp);
}

}  // namespace

static_assert(acfx::wdf::is_one_port_v<acfx::wdf::Resistor>,
              "Resistor must satisfy the OnePort concept trait");

TEST_CASE("Resistor portResistance() reports the constructed R") {
    const acfx::wdf::Resistor r(600.0);
    CHECK(r.portResistance() == doctest::Approx(600.0));
}

TEST_CASE("Resistor reflected() is 0 (adapted, reflection-free) for any prior incident") {
    acfx::wdf::Resistor r(100.0);

    // Before any incident() call.
    CHECK(r.reflected() == doctest::Approx(0.0));

    // After a variety of prior incident(a) calls -- still 0 (memoryless,
    // adapted: R2/data-model "reflected() -> 0 for any incident history").
    for (double a : {-10.0, 0.0, 1.0, 3.5, 1000.0}) {
        r.incident(a);
        CHECK(r.reflected() == doctest::Approx(0.0));
    }
}

TEST_CASE("Resistor throws std::invalid_argument for non-positive R") {
    CHECK_THROWS_AS(acfx::wdf::Resistor(0.0), std::invalid_argument);
    CHECK_THROWS_AS(acfx::wdf::Resistor(-1.0), std::invalid_argument);
    CHECK_THROWS_AS(acfx::wdf::Resistor(-600.0), std::invalid_argument);
}

TEST_CASE("Resistor incident(a) is a noexcept no-op") {
    acfx::wdf::Resistor r(50.0);
    static_assert(noexcept(r.incident(1.0)), "incident(double) must be noexcept");

    r.incident(42.0);
    // No observable state change: portResistance and reflected are
    // unaffected by incident() (memoryless, adapted).
    CHECK(r.portResistance() == doctest::Approx(50.0));
    CHECK(r.reflected() == doctest::Approx(0.0));
}

TEST_CASE("Resistor is a OnePort (static_assert already checked at file scope)") {
    CHECK(acfx::wdf::is_one_port_v<acfx::wdf::Resistor>);
}

TEST_CASE("Test-local oracle: general unadapted reflection b = a*(R-Rp)/(R+Rp) vanishes at Rp==R") {
    // This documents FR-002/R3: the general (unadapted) reflection formula
    // is exercised here ONLY as a local test oracle, never as a Resistor
    // member -- arbitrary reference resistance is the adaptor layer's
    // concern, not this leaf's.
    const double R = 100.0;

    CHECK(unadaptedReflection(5.0, R, R) == doctest::Approx(0.0));
    CHECK(unadaptedReflection(-3.0, R, R) == doctest::Approx(0.0));

    // Away from the matched reference resistance, the oracle is nonzero
    // (sanity check that the oracle itself is not trivially always 0).
    CHECK(unadaptedReflection(5.0, R, 50.0) != doctest::Approx(0.0));
    CHECK(unadaptedReflection(5.0, R, 200.0) != doctest::Approx(0.0));
}
