#include <doctest/doctest.h>

#include <cmath>
#include <utility>

#include "primitives/circuit/wdf/one-port.h"
#include "primitives/circuit/wdf/series-adaptor.h"
#include "primitives/circuit/wdf/parallel-adaptor.h"
#include "primitives/circuit/wdf/wave-elements.h"

// WDF adaptor-tree suite (wdf-adaptors feature, US3/T007).
//
// Proves adaptors NEST recursively purely through the OnePort seam: a
// SeriesAdaptor and a ParallelAdaptor are each themselves OnePorts
// (contracts/wdf-adaptors.md C1), so a composite like
// SeriesAdaptor<Resistor, ParallelAdaptor<Capacitor, Inductor>> requires NO
// new mechanism -- it is exactly the same template machinery instantiated one
// level deeper. This file is validation-only: no production code changes.
//
// Three angles:
//   1. Composite-is-a-OnePort + recursive portResistance() combination, using
//      the real shipped leaves (Resistor/Capacitor/Inductor).
//   2. Exact nested scattering (up-sweep + down-sweep) against a hand-computed
//      oracle, using test-local Probe leaves for full control over
//      reflected()/incident() (the recursion oracle).
//   3. Single-child pass-through: SeriesAdaptor<T> / ParallelAdaptor<T> both
//      degenerate to portResistance() == T's own R.
//
// A commented-out negative-compilation block (SC-007) documents the
// compile-time delay-free-loop guard (adaptor-detail.h) that rejects a
// non-adaptable child (e.g. ShortCircuit) as a series/parallel child.

namespace {

// Test-local PROBE one-port (same shape as wdf-series-adaptor-test.cpp):
// observes exactly what incident wave each child receives from the adaptor's
// down-sweep, with a fixed pre-set reflected() value so the up-sweep is fully
// controlled by the test. Adapted / reflection-free (reflected() is
// independent of prior incident() calls), satisfying the adaptor child
// constraints (C2).
struct Probe {
    double Rp{1.0};
    double refl{0.0};
    mutable double lastIncident{0.0};

    double portResistance() const noexcept { return Rp; }
    double reflected() const noexcept { return refl; }
    void incident(double a) noexcept { lastIncident = a; }
    static constexpr bool isAdaptable = true;
};

constexpr double kTightTol = 1e-12;
constexpr double kRelTol = 1e-9;

}  // namespace

static_assert(acfx::wdf::is_one_port_v<Probe>,
              "test-local Probe must satisfy the OnePort concept trait");

// -----------------------------------------------------------------------------
// 1. Composite is a OnePort + recursive port-resistance combination (real
//    shipped leaves: Resistor, Capacitor, Inductor).
// -----------------------------------------------------------------------------

namespace {
using InnerParallel = acfx::wdf::ParallelAdaptor<acfx::wdf::Capacitor, acfx::wdf::Inductor>;
using OuterSeries = acfx::wdf::SeriesAdaptor<acfx::wdf::Resistor, InnerParallel>;
}  // namespace

// The composite nests via the OnePort concept trait with NO new mechanism:
// SeriesAdaptor<Resistor, ParallelAdaptor<Capacitor, Inductor>> is itself a
// OnePort exactly because ParallelAdaptor<Capacitor, Inductor> already is one
// (contract C1), so SeriesAdaptor's own compile-time guards (adaptor-detail.h)
// accept it as an ordinary child.
static_assert(acfx::wdf::is_one_port_v<OuterSeries>,
              "SeriesAdaptor<Resistor, ParallelAdaptor<Capacitor, Inductor>> "
              "must itself satisfy the OnePort concept trait (recursive composition, contract C1)");

TEST_CASE("adaptor tree: nested composite (real leaves) portResistance() combines recursively "
          "(Rp_C || Rp_L, then R + that)") {
    const double R = 1000.0;
    const double C = 1e-6;
    const double dt = 1.0 / 48000.0;
    const double L = 0.5;

    // Shipped leaf port resistances (wave-elements.h): Rp_C = dt/(2C), Rp_L = 2L/dt.
    const double RpC = dt / (2.0 * C);
    const double RpL = 2.0 * L / dt;
    const double expectedInner = 1.0 / (1.0 / RpC + 1.0 / RpL);
    const double expectedOuter = R + expectedInner;

    InnerParallel inner(acfx::wdf::Capacitor(C, dt), acfx::wdf::Inductor(L, dt));
    const double innerRp = inner.portResistance();
    CHECK(std::abs(innerRp - expectedInner) / std::abs(expectedInner) <= kRelTol);

    OuterSeries outer(acfx::wdf::Resistor(R),
                       InnerParallel(acfx::wdf::Capacitor(C, dt), acfx::wdf::Inductor(L, dt)));
    const double outerRp = outer.portResistance();
    CHECK(std::abs(outerRp - expectedOuter) / std::abs(expectedOuter) <= kRelTol);

    // Cross-check: the outer's own recursive combination reuses the SAME
    // inner-parallel port resistance computed independently above.
    CHECK(std::abs(outerRp - (R + innerRp)) / std::abs(R + innerRp) <= kRelTol);
}

// -----------------------------------------------------------------------------
// 2. Exact nested scattering, driven with Probes (the recursion oracle).
// -----------------------------------------------------------------------------

TEST_CASE("adaptor tree: nested composite (Probes) exact up-sweep + down-sweep scattering "
          "matches a hand-computed oracle") {
    // Fixture: R0=2.0, Ra=1.0, Rb=1.0, r0=0.3, ra=0.5, rb=-0.1, a_u=1.0.
    const double R0 = 2.0;
    const double Ra = 1.0;
    const double Rb = 1.0;
    const double r0 = 0.3;
    const double ra = 0.5;
    const double rb = -0.1;
    const double au = 1.0;

    using InnerParallelProbe = acfx::wdf::ParallelAdaptor<Probe, Probe>;
    using OuterSeriesProbe = acfx::wdf::SeriesAdaptor<Probe, InnerParallelProbe>;

    InnerParallelProbe inner(Probe{Ra, ra, 0.0}, Probe{Rb, rb, 0.0});
    OuterSeriesProbe outer(Probe{R0, r0, 0.0}, std::move(inner));

    // portResistance(): inner = 1/(1/Ra + 1/Rb) = 0.5; outer = R0 + inner = 2.5.
    CHECK(outer.child<1>().portResistance() == doctest::Approx(0.5).epsilon(kTightTol));
    CHECK(outer.portResistance() == doctest::Approx(2.5).epsilon(kTightTol));

    // Up-sweep then down-sweep, in the mandated order (contract C5).
    const double bu = outer.reflected();
    // b_uInner = (ra + rb) / 2 = 0.2 (equal Ra == Rb, so coeff_a == coeff_b == 0.5).
    const double buInnerExpected = (ra + rb) / 2.0;
    // outer.reflected() = -(r0 + b_uInner) = -(0.3 + 0.2) = -0.5.
    CHECK(bu == doctest::Approx(-(r0 + buInnerExpected)).epsilon(kTightTol));
    CHECK(bu == doctest::Approx(-0.5).epsilon(kTightTol));

    outer.incident(au);

    // Root probe leaf (outer.child<0>()): b0 = r0 - (R0/Rup)*S, S = au + r0 + b_uInner.
    CHECK(outer.child<0>().lastIncident == doctest::Approx(-0.9).epsilon(kTightTol));

    // Reach the inner ParallelAdaptor via the composition seam
    // (outer.child<1>()) and inspect its own probe children -- these MUST be
    // the SAME owned objects the sweep just wrote (by-value tuple ownership,
    // no copies made mid-sweep).
    const auto& innerRef = outer.child<1>();
    CHECK(innerRef.child<0>().lastIncident == doctest::Approx(-0.4).epsilon(kTightTol));
    CHECK(innerRef.child<1>().lastIncident == doctest::Approx(0.2).epsilon(kTightTol));
}

// -----------------------------------------------------------------------------
// 3. Single-child pass-through.
// -----------------------------------------------------------------------------

TEST_CASE("adaptor tree: single-child SeriesAdaptor<Resistor> and ParallelAdaptor<Resistor> "
          "both pass through portResistance() == R") {
    const double R = 1234.5;

    acfx::wdf::SeriesAdaptor<acfx::wdf::Resistor> series{acfx::wdf::Resistor(R)};
    acfx::wdf::ParallelAdaptor<acfx::wdf::Resistor> parallel{acfx::wdf::Resistor(R)};

    CHECK(series.portResistance() == doctest::Approx(R).epsilon(kTightTol));
    CHECK(parallel.portResistance() == doctest::Approx(R).epsilon(kTightTol));
}

// -----------------------------------------------------------------------------
// 4. Negative compilation (SC-007), documented but NOT compiled.
// -----------------------------------------------------------------------------
//
// static_assert would FAIL to compile -- a non-adaptable child (ShortCircuit,
// isAdaptable==false) creates a delay-free loop and is rejected by
// adaptor-detail.h's static_assert:
//   acfx::wdf::SeriesAdaptor<acfx::wdf::Resistor, acfx::wdf::ShortCircuit> bad{acfx::wdf::Resistor(1000.0), acfx::wdf::ShortCircuit(1000.0)};
