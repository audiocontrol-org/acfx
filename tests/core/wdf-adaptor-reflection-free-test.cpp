#include <doctest/doctest.h>

#include <array>
#include <cmath>

#include "primitives/circuit/wdf/one-port.h"
#include "primitives/circuit/wdf/parallel-adaptor.h"
#include "primitives/circuit/wdf/series-adaptor.h"

// wdf-adaptor-reflection-free-test.cpp (wdf-adaptors feature, T008, US4).
//
// Validation-only: proves the adapted UPWARD port of both SeriesAdaptor and
// ParallelAdaptor is reflection-free per contract C4 -- reflected() (the
// up-sweep output b_u) does NOT depend on this sample's upward incident a_u.
//
// Method: read b0 = adaptor.reflected() before any incident() call, then for
// a range of very different a_u values call adaptor.incident(a_u) and
// re-read adaptor.reflected(); it must equal b0 every time (the up-sweep
// cache is recomputed from the (unchanged) child reflected() values, never
// from the delivered a_u). Also checks the C4 closed forms for b_u.

namespace {

// Test-local PROBE one-port (see tests/core/wdf-series-adaptor-test.cpp):
// reflected() is a FIXED `refl`, independent of any prior incident() call, so
// any dependence of the adaptor's reflected() on a_u would be a bug this test
// catches (Probe itself cannot introduce a spurious a_u dependency).
struct Probe {
    double Rp{1.0};
    double refl{0.0};
    mutable double lastIncident{0.0};

    double portResistance() const noexcept { return Rp; }
    double reflected() const noexcept { return refl; }
    void incident(double a) noexcept { lastIncident = a; }
    static constexpr bool isAdaptable = true;
};

constexpr double kAbsTol = 1e-15;
constexpr double kClosedFormTolParallel = 1e-12;

// A spread of very different upward incidents, including large magnitudes,
// to stress any hidden a_u dependence in reflected().
constexpr std::array<double, 5> kAupSweep = {-1.0e6, -1.0, 0.0, 1.0, 1.0e6};

}  // namespace

static_assert(acfx::wdf::is_one_port_v<Probe>,
              "test-local Probe must satisfy the OnePort concept trait");
static_assert(acfx::wdf::is_one_port_v<acfx::wdf::SeriesAdaptor<Probe, Probe>>,
              "SeriesAdaptor<Probe, Probe> must satisfy the OnePort concept trait (contract C1)");
static_assert(acfx::wdf::is_one_port_v<acfx::wdf::ParallelAdaptor<Probe, Probe>>,
              "ParallelAdaptor<Probe, Probe> must satisfy the OnePort concept trait (contract C1)");

TEST_CASE("SeriesAdaptor adapted upward port is reflection-free: b_u invariant under a_u") {
    const double Ra = 1000.0;
    const double Rb = 3000.0;
    const double ra = 0.5;
    const double rb = -0.2;

    acfx::wdf::SeriesAdaptor<Probe, Probe> adaptor(Probe{Ra, ra, 0.0}, Probe{Rb, rb, 0.0});

    // Up-sweep before any incident() call in this sample.
    const double b0 = adaptor.reflected();

    // C4 closed form: b_u = -(a0 + a1) = -(ra + rb).
    CHECK(std::abs(b0 - (-(ra + rb))) < kAbsTol);

    for (double aUp : kAupSweep) {
        adaptor.incident(aUp);
        const double bAgain = adaptor.reflected();
        CHECK(std::abs(bAgain - b0) < kAbsTol);
    }
}

TEST_CASE("ParallelAdaptor adapted upward port is reflection-free: b_u invariant under a_u") {
    const double Ra = 1000.0;
    const double Rb = 3000.0;
    const double ra = 0.5;
    const double rb = -0.2;
    const double Ga = 1.0 / Ra;
    const double Gb = 1.0 / Rb;
    const double Gup = Ga + Gb;

    acfx::wdf::ParallelAdaptor<Probe, Probe> adaptor(Probe{Ra, ra, 0.0}, Probe{Rb, rb, 0.0});

    // Up-sweep before any incident() call in this sample.
    const double b0 = adaptor.reflected();

    // C4 closed form: b_u = (Ga*ra + Gb*rb) / Gup.
    const double expectedB0 = (Ga * ra + Gb * rb) / Gup;
    CHECK(std::abs(b0 - expectedB0) < kClosedFormTolParallel);

    for (double aUp : kAupSweep) {
        adaptor.incident(aUp);
        const double bAgain = adaptor.reflected();
        CHECK(std::abs(bAgain - b0) < kAbsTol);
    }
}
