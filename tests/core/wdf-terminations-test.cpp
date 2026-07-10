#include <doctest/doctest.h>

#include <stdexcept>

#include "primitives/circuit/wdf/one-port.h"
#include "primitives/circuit/wdf/wave-terminations.h"

// WDF termination and boundary condition suite (wdf-primitives feature,
// T011/T012).
//
// Covers wave-domain terminations (matched load / open / short circuits),
// boundary behavior at tree leaves, reflected/incident contracts, and the
// OnePort concept trait (contracts/wdf-one-ports.md "ResistiveTermination"
// RT1, "ShortCircuit" SH1-SH2, "OpenCircuit" OP1; data-model.md "Reflective
// one-ports").
//
// T012 (core/primitives/circuit/wdf/wave-terminations.h, namespace
// acfx::wdf) MUST define `ResistiveTermination`, `ShortCircuit`,
// `OpenCircuit`:
//
//   explicit ResistiveTermination(double R);
//   double portResistance() const noexcept;   // R
//   double reflected()      const noexcept;    // 0  (matched load)
//   void   incident(double a) noexcept;        // no-op
//   static constexpr bool isAdaptable = true;
//
//   explicit ShortCircuit(double Rp);
//   double portResistance() const noexcept;   // Rp (externally-imposed)
//   double reflected()      const noexcept;    // -a_ (from last incident)
//   void   incident(double a) noexcept;        // a_ := a
//   static constexpr bool isAdaptable = false;
//
//   explicit OpenCircuit(double Rp);
//   double portResistance() const noexcept;   // Rp
//   double reflected()      const noexcept;    // +a_
//   void   incident(double a) noexcept;        // a_ := a
//   static constexpr bool isAdaptable = false;
//
// This test file intentionally does NOT compile until T012 lands
// (`primitives/circuit/wdf/wave-terminations.h` does not exist yet, and
// `acfx::wdf::ResistiveTermination` / `acfx::wdf::ShortCircuit` /
// `acfx::wdf::OpenCircuit` are undefined) -- that is the expected RED state
// for T011.

static_assert(acfx::wdf::is_one_port_v<acfx::wdf::ResistiveTermination>,
              "ResistiveTermination must satisfy the OnePort concept trait");
static_assert(acfx::wdf::is_one_port_v<acfx::wdf::ShortCircuit>,
              "ShortCircuit must satisfy the OnePort concept trait");
static_assert(acfx::wdf::is_one_port_v<acfx::wdf::OpenCircuit>,
              "OpenCircuit must satisfy the OnePort concept trait");

// ---------------------------------------------------------------------------
// ResistiveTermination (matched load) -- RT1.
// ---------------------------------------------------------------------------

TEST_CASE("ResistiveTermination portResistance() reports the constructed R") {
    const acfx::wdf::ResistiveTermination rt(600.0);
    CHECK(rt.portResistance() == doctest::Approx(600.0));
}

TEST_CASE("ResistiveTermination reflected() is 0 (matched load) for any prior incident") {
    acfx::wdf::ResistiveTermination rt(100.0);

    CHECK(rt.reflected() == doctest::Approx(0.0));

    for (double a : {-10.0, 0.0, 1.0, 3.5, 1000.0}) {
        rt.incident(a);
        CHECK(rt.reflected() == doctest::Approx(0.0));
    }
}

TEST_CASE("ResistiveTermination is adapted (isAdaptable == true)") {
    CHECK(acfx::wdf::ResistiveTermination::isAdaptable == true);
}

TEST_CASE("ResistiveTermination throws std::invalid_argument for non-positive R") {
    CHECK_THROWS_AS(acfx::wdf::ResistiveTermination(0.0), std::invalid_argument);
    CHECK_THROWS_AS(acfx::wdf::ResistiveTermination(-1.0), std::invalid_argument);
    CHECK_THROWS_AS(acfx::wdf::ResistiveTermination(-600.0), std::invalid_argument);
}

TEST_CASE("ResistiveTermination incident(a) is a noexcept no-op") {
    acfx::wdf::ResistiveTermination rt(50.0);
    static_assert(noexcept(rt.incident(1.0)), "incident(double) must be noexcept");

    rt.incident(42.0);
    CHECK(rt.portResistance() == doctest::Approx(50.0));
    CHECK(rt.reflected() == doctest::Approx(0.0));
}

// ---------------------------------------------------------------------------
// ShortCircuit (v = 0 -> b = -a) -- SH1/SH2, reflective (I2 call ordering).
// ---------------------------------------------------------------------------

TEST_CASE("ShortCircuit is reflective (isAdaptable == false)") {
    CHECK(acfx::wdf::ShortCircuit::isAdaptable == false);
}

TEST_CASE("ShortCircuit portResistance() reports the constructed Rp") {
    const acfx::wdf::ShortCircuit sc(600.0);
    CHECK(sc.portResistance() == doctest::Approx(600.0));
}

TEST_CASE("ShortCircuit reflected() == -a after incident(a) (I2: incident before reflected)") {
    acfx::wdf::ShortCircuit sc(100.0);

    for (double a : {-10.0, 0.0, 1.0, 3.5, 1000.0}) {
        sc.incident(a);
        CHECK(sc.reflected() == doctest::Approx(-a));
    }
}

TEST_CASE("ShortCircuit reflection is independent of Rp (SH1)") {
    acfx::wdf::ShortCircuit scLowRp(1.0);
    acfx::wdf::ShortCircuit scHighRp(1.0e6);

    for (double a : {-7.0, 0.0, 2.25, 99.0}) {
        scLowRp.incident(a);
        scHighRp.incident(a);
        CHECK(scLowRp.reflected() == doctest::Approx(scHighRp.reflected()));
        CHECK(scLowRp.reflected() == doctest::Approx(-a));
    }

    // The two instances still report their own distinct, externally-imposed
    // Rp -- only the reflection is Rp-independent.
    CHECK(scLowRp.portResistance() == doctest::Approx(1.0));
    CHECK(scHighRp.portResistance() == doctest::Approx(1.0e6));
}

TEST_CASE("ShortCircuit throws std::invalid_argument for non-positive Rp") {
    CHECK_THROWS_AS(acfx::wdf::ShortCircuit(0.0), std::invalid_argument);
    CHECK_THROWS_AS(acfx::wdf::ShortCircuit(-1.0), std::invalid_argument);
    CHECK_THROWS_AS(acfx::wdf::ShortCircuit(-600.0), std::invalid_argument);
}

TEST_CASE("ShortCircuit incident(a) is noexcept") {
    acfx::wdf::ShortCircuit sc(50.0);
    static_assert(noexcept(sc.incident(1.0)), "incident(double) must be noexcept");
}

// ---------------------------------------------------------------------------
// OpenCircuit (i = 0 -> b = +a) -- OP1, reflective (I2 call ordering).
// ---------------------------------------------------------------------------

TEST_CASE("OpenCircuit is reflective (isAdaptable == false)") {
    CHECK(acfx::wdf::OpenCircuit::isAdaptable == false);
}

TEST_CASE("OpenCircuit portResistance() reports the constructed Rp") {
    const acfx::wdf::OpenCircuit oc(600.0);
    CHECK(oc.portResistance() == doctest::Approx(600.0));
}

TEST_CASE("OpenCircuit reflected() == +a after incident(a) (I2: incident before reflected)") {
    acfx::wdf::OpenCircuit oc(100.0);

    for (double a : {-10.0, 0.0, 1.0, 3.5, 1000.0}) {
        oc.incident(a);
        CHECK(oc.reflected() == doctest::Approx(a));
    }
}

TEST_CASE("OpenCircuit reflection is independent of Rp (OP1)") {
    acfx::wdf::OpenCircuit ocLowRp(1.0);
    acfx::wdf::OpenCircuit ocHighRp(1.0e6);

    for (double a : {-7.0, 0.0, 2.25, 99.0}) {
        ocLowRp.incident(a);
        ocHighRp.incident(a);
        CHECK(ocLowRp.reflected() == doctest::Approx(ocHighRp.reflected()));
        CHECK(ocLowRp.reflected() == doctest::Approx(a));
    }

    CHECK(ocLowRp.portResistance() == doctest::Approx(1.0));
    CHECK(ocHighRp.portResistance() == doctest::Approx(1.0e6));
}

TEST_CASE("OpenCircuit throws std::invalid_argument for non-positive Rp") {
    CHECK_THROWS_AS(acfx::wdf::OpenCircuit(0.0), std::invalid_argument);
    CHECK_THROWS_AS(acfx::wdf::OpenCircuit(-1.0), std::invalid_argument);
    CHECK_THROWS_AS(acfx::wdf::OpenCircuit(-600.0), std::invalid_argument);
}

TEST_CASE("OpenCircuit incident(a) is noexcept") {
    acfx::wdf::OpenCircuit oc(50.0);
    static_assert(noexcept(oc.incident(1.0)), "incident(double) must be noexcept");
}
