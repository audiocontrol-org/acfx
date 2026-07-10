#include <doctest/doctest.h>

#include <stdexcept>

#include "primitives/circuit/wdf/one-port.h"
#include "primitives/circuit/wdf/wave-sources.h"

// WDF ideal source one-ports suite (wdf-primitives feature, T009/T010).
//
// Covers the two resistive (Thevenin/Norton) source one-ports under wave
// domain: per-sample source-value injection, reflected/incident contracts,
// and the OnePort concept trait (contracts/wdf-one-ports.md
// "ResistiveVoltageSource" VS1-VS2, "ResistiveCurrentSource" CS1;
// data-model.md "ResistiveVoltageSource" / "ResistiveCurrentSource").
//
// T010 (core/primitives/circuit/wdf/wave-sources.h, namespace acfx::wdf)
// MUST define `ResistiveVoltageSource` and `ResistiveCurrentSource`:
//
//   explicit ResistiveVoltageSource(double R, double E = 0.0);
//   double portResistance() const noexcept;   // R
//   double reflected()      const noexcept;    // E  (adapted)
//   void   incident(double a) noexcept;        // no-op
//   void   setVoltage(double E) noexcept;      // per-sample drive
//   static constexpr bool isAdaptable = true;
//
//   explicit ResistiveCurrentSource(double R, double I = 0.0);
//   double portResistance() const noexcept;   // R
//   double reflected()      const noexcept;    // R * I  (adapted)
//   void   incident(double a) noexcept;        // no-op
//   void   setCurrent(double I) noexcept;      // per-sample drive
//   static constexpr bool isAdaptable = true;
//
// This test file intentionally does NOT compile until T010 lands
// (`primitives/circuit/wdf/wave-sources.h` does not exist yet, and
// `acfx::wdf::ResistiveVoltageSource` / `acfx::wdf::ResistiveCurrentSource`
// are undefined) -- that is the expected RED state for T009.

static_assert(acfx::wdf::is_one_port_v<acfx::wdf::ResistiveVoltageSource>,
              "ResistiveVoltageSource must satisfy the OnePort concept trait");
static_assert(acfx::wdf::is_one_port_v<acfx::wdf::ResistiveCurrentSource>,
              "ResistiveCurrentSource must satisfy the OnePort concept trait");

// ---------------------------------------------------------------------------
// ResistiveVoltageSource (Thevenin: E in series with R) -- VS1/VS2.
// ---------------------------------------------------------------------------

TEST_CASE("ResistiveVoltageSource portResistance() reports the constructed R") {
    const acfx::wdf::ResistiveVoltageSource vs(600.0, 5.0);
    CHECK(vs.portResistance() == doctest::Approx(600.0));
}

TEST_CASE("ResistiveVoltageSource reflected() is the constructed E by default") {
    const acfx::wdf::ResistiveVoltageSource vs(100.0, 3.5);
    CHECK(vs.reflected() == doctest::Approx(3.5));
}

TEST_CASE("ResistiveVoltageSource defaults E to 0 when omitted") {
    const acfx::wdf::ResistiveVoltageSource vs(100.0);
    CHECK(vs.reflected() == doctest::Approx(0.0));
}

TEST_CASE("ResistiveVoltageSource setVoltage(E') updates reflected() (per-sample drive)") {
    acfx::wdf::ResistiveVoltageSource vs(50.0, 1.0);
    CHECK(vs.reflected() == doctest::Approx(1.0));

    vs.setVoltage(9.0);
    CHECK(vs.reflected() == doctest::Approx(9.0));
    // Rp is unaffected by setVoltage (VS2).
    CHECK(vs.portResistance() == doctest::Approx(50.0));

    vs.setVoltage(-4.25);
    CHECK(vs.reflected() == doctest::Approx(-4.25));
}

TEST_CASE("ResistiveVoltageSource throws std::invalid_argument for non-positive R") {
    CHECK_THROWS_AS(acfx::wdf::ResistiveVoltageSource(0.0), std::invalid_argument);
    CHECK_THROWS_AS(acfx::wdf::ResistiveVoltageSource(-1.0), std::invalid_argument);
    CHECK_THROWS_AS(acfx::wdf::ResistiveVoltageSource(-600.0, 5.0), std::invalid_argument);
}

TEST_CASE("ResistiveVoltageSource incident(a) is a noexcept no-op") {
    acfx::wdf::ResistiveVoltageSource vs(50.0, 2.0);
    static_assert(noexcept(vs.incident(1.0)), "incident(double) must be noexcept");

    vs.incident(42.0);
    // No observable state change: portResistance and reflected are
    // unaffected by incident() (memoryless, adapted).
    CHECK(vs.portResistance() == doctest::Approx(50.0));
    CHECK(vs.reflected() == doctest::Approx(2.0));
}

TEST_CASE("ResistiveVoltageSource is a OnePort (static_assert already checked at file scope)") {
    CHECK(acfx::wdf::is_one_port_v<acfx::wdf::ResistiveVoltageSource>);
}

// ---------------------------------------------------------------------------
// ResistiveCurrentSource (Norton: I in parallel with R) -- CS1.
// ---------------------------------------------------------------------------

TEST_CASE("ResistiveCurrentSource portResistance() reports the constructed R") {
    const acfx::wdf::ResistiveCurrentSource cs(600.0, 0.02);
    CHECK(cs.portResistance() == doctest::Approx(600.0));
}

TEST_CASE("ResistiveCurrentSource reflected() is R*I by default") {
    const acfx::wdf::ResistiveCurrentSource cs(100.0, 0.5);
    CHECK(cs.reflected() == doctest::Approx(100.0 * 0.5));
}

TEST_CASE("ResistiveCurrentSource defaults I to 0 when omitted") {
    const acfx::wdf::ResistiveCurrentSource cs(100.0);
    CHECK(cs.reflected() == doctest::Approx(0.0));
}

TEST_CASE("ResistiveCurrentSource setCurrent(I') updates reflected() = R*I (per-sample drive)") {
    acfx::wdf::ResistiveCurrentSource cs(50.0, 0.1);
    CHECK(cs.reflected() == doctest::Approx(5.0));

    cs.setCurrent(0.4);
    CHECK(cs.reflected() == doctest::Approx(20.0));
    // Rp is unaffected by setCurrent.
    CHECK(cs.portResistance() == doctest::Approx(50.0));

    cs.setCurrent(-0.2);
    CHECK(cs.reflected() == doctest::Approx(-10.0));
}

TEST_CASE("ResistiveCurrentSource throws std::invalid_argument for non-positive R") {
    CHECK_THROWS_AS(acfx::wdf::ResistiveCurrentSource(0.0), std::invalid_argument);
    CHECK_THROWS_AS(acfx::wdf::ResistiveCurrentSource(-1.0), std::invalid_argument);
    CHECK_THROWS_AS(acfx::wdf::ResistiveCurrentSource(-600.0, 0.02), std::invalid_argument);
}

TEST_CASE("ResistiveCurrentSource incident(a) is a noexcept no-op") {
    acfx::wdf::ResistiveCurrentSource cs(50.0, 0.3);
    static_assert(noexcept(cs.incident(1.0)), "incident(double) must be noexcept");

    cs.incident(42.0);
    // No observable state change: portResistance and reflected are
    // unaffected by incident() (memoryless, adapted).
    CHECK(cs.portResistance() == doctest::Approx(50.0));
    CHECK(cs.reflected() == doctest::Approx(15.0));
}

TEST_CASE("ResistiveCurrentSource is a OnePort (static_assert already checked at file scope)") {
    CHECK(acfx::wdf::is_one_port_v<acfx::wdf::ResistiveCurrentSource>);
}
