#include <doctest/doctest.h>

#include <array>
#include <cmath>
#include <random>

#include "primitives/circuit/wdf/one-port.h"
#include "primitives/circuit/wdf/series-adaptor.h"
#include "primitives/circuit/wdf/parallel-adaptor.h"

// WDF adaptor LOSSLESSNESS suite (wdf-adaptors feature, T012 / US7,
// contracts/wdf-adaptors.md C7). Both SeriesAdaptor and ParallelAdaptor are
// LOSSLESS scattering junctions: over ALL ports (the N children AND the upward
// port), with per-port conductance G_k = 1/R_k, the CONDUCTANCE-WEIGHTED
// pseudo-power balance holds for every admissible input:
//
//   Σ_k G_k · a_k²  ==  Σ_k G_k · b_k²
//
// where a_k is the wave incident INTO the junction at port k and b_k the wave
// the junction reflects back OUT at port k. Port wave bookkeeping (junction's
// perspective):
//   - child port k: a_k = child.reflected() (= Probe.refl), b_k = the wave the
//       junction delivers to child.incident() (= Probe.lastIncident after the
//       down-sweep); G_k = 1/R_child_k.
//   - upward port u: a_u = the value passed to adaptor.incident(a_u),
//       b_u = adaptor.reflected() (read in the up-sweep, BEFORE incident());
//       G_u = 1/adaptor.portResistance().
//
// The UNWEIGHTED Σ a_k² == Σ b_k² is NOT the invariant — it holds only when all
// branch resistances are equal. The final discriminating case demonstrates that
// explicitly so a future maintainer cannot "simplify" the test to the wrong
// invariant.

namespace {

// Test-local PROBE one-port (mirrors wdf-series-adaptor-test.cpp): carries a
// preset port resistance Rp and reflected wave refl, and records the last
// incident wave the adaptor delivers in lastIncident. Adapted / reflection-free.
struct Probe {
    double Rp{1.0};
    double refl{0.0};
    mutable double lastIncident{0.0};

    double portResistance() const noexcept { return Rp; }
    double reflected() const noexcept { return refl; }
    void incident(double a) noexcept { lastIncident = a; }
    static constexpr bool isAdaptable = true;
};

constexpr double kBalanceTol = 1e-12;

// One weighted-balance trial for a 2-child adaptor of type Adaptor
// (SeriesAdaptor<Probe,Probe> or ParallelAdaptor<Probe,Probe>). Returns the
// relative imbalance |weightedIn - weightedOut| / max(weightedIn, 1e-30).
template <class Adaptor>
double weightedImbalance2(double R0, double R1, double a0, double a1, double au) {
    Adaptor adaptor(Probe{R0, a0, 0.0}, Probe{R1, a1, 0.0});

    const double bu = adaptor.reflected();  // upward reflected-out, before incident
    adaptor.incident(au);                   // down-sweep delivers child incidents
    const double b0 = adaptor.template child<0>().lastIncident;
    const double b1 = adaptor.template child<1>().lastIncident;

    const double Rup = adaptor.portResistance();
    const double Gup = 1.0 / Rup;
    const double G0 = 1.0 / R0;
    const double G1 = 1.0 / R1;

    const double weightedIn = G0 * a0 * a0 + G1 * a1 * a1 + Gup * au * au;
    const double weightedOut = G0 * b0 * b0 + G1 * b1 * b1 + Gup * bu * bu;

    return std::abs(weightedIn - weightedOut) / std::max(weightedIn, 1e-30);
}

// One weighted-balance trial for a 3-child adaptor.
template <class Adaptor>
double weightedImbalance3(double R0, double R1, double R2,
                          double a0, double a1, double a2, double au) {
    Adaptor adaptor(Probe{R0, a0, 0.0}, Probe{R1, a1, 0.0}, Probe{R2, a2, 0.0});

    const double bu = adaptor.reflected();
    adaptor.incident(au);
    const double b0 = adaptor.template child<0>().lastIncident;
    const double b1 = adaptor.template child<1>().lastIncident;
    const double b2 = adaptor.template child<2>().lastIncident;

    const double Rup = adaptor.portResistance();
    const double Gup = 1.0 / Rup;
    const double G0 = 1.0 / R0;
    const double G1 = 1.0 / R1;
    const double G2 = 1.0 / R2;

    const double weightedIn = G0 * a0 * a0 + G1 * a1 * a1 + G2 * a2 * a2 + Gup * au * au;
    const double weightedOut = G0 * b0 * b0 + G1 * b1 * b1 + G2 * b2 * b2 + Gup * bu * bu;

    return std::abs(weightedIn - weightedOut) / std::max(weightedIn, 1e-30);
}

constexpr int kTrialsPerConfig = 1000;

}  // namespace

TEST_CASE("SeriesAdaptor passivity: conductance-weighted pseudo-power balance over randomized trials") {
    using SeriesAdaptor2 = acfx::wdf::SeriesAdaptor<Probe, Probe>;
    using SeriesAdaptor3 = acfx::wdf::SeriesAdaptor<Probe, Probe, Probe>;

    std::mt19937 rng(12345);  // FIXED seed — deterministic, never time-based
    std::uniform_real_distribution<double> resDist(10.0, 10000.0);
    std::uniform_real_distribution<double> waveDist(-1.0, 1.0);

    for (int t = 0; t < kTrialsPerConfig; ++t) {
        const double imbalance = weightedImbalance2<SeriesAdaptor2>(
            resDist(rng), resDist(rng), waveDist(rng), waveDist(rng), waveDist(rng));
        CHECK(imbalance < kBalanceTol);
    }
    for (int t = 0; t < kTrialsPerConfig; ++t) {
        const double imbalance = weightedImbalance3<SeriesAdaptor3>(
            resDist(rng), resDist(rng), resDist(rng),
            waveDist(rng), waveDist(rng), waveDist(rng), waveDist(rng));
        CHECK(imbalance < kBalanceTol);
    }
}

TEST_CASE("ParallelAdaptor passivity: conductance-weighted pseudo-power balance over randomized trials") {
    using ParallelAdaptor2 = acfx::wdf::ParallelAdaptor<Probe, Probe>;
    using ParallelAdaptor3 = acfx::wdf::ParallelAdaptor<Probe, Probe, Probe>;

    std::mt19937 rng(12345);  // FIXED seed — deterministic, never time-based
    std::uniform_real_distribution<double> resDist(10.0, 10000.0);
    std::uniform_real_distribution<double> waveDist(-1.0, 1.0);

    for (int t = 0; t < kTrialsPerConfig; ++t) {
        const double imbalance = weightedImbalance2<ParallelAdaptor2>(
            resDist(rng), resDist(rng), waveDist(rng), waveDist(rng), waveDist(rng));
        CHECK(imbalance < kBalanceTol);
    }
    for (int t = 0; t < kTrialsPerConfig; ++t) {
        const double imbalance = weightedImbalance3<ParallelAdaptor3>(
            resDist(rng), resDist(rng), resDist(rng),
            waveDist(rng), waveDist(rng), waveDist(rng), waveDist(rng));
        CHECK(imbalance < kBalanceTol);
    }
}

TEST_CASE("Adaptor passivity discriminator: UNWEIGHTED balance FAILS for unequal branch resistances") {
    // With clearly UNEQUAL child resistances and nonzero waves, the UNWEIGHTED
    // sum Σ a_k² over all ports does NOT equal Σ b_k² — proving the weighted
    // form (which DOES balance, checked above) is the real invariant and the
    // unweighted form is not. Chosen values make the gap large (>> 1e-9).
    const double R0 = 100.0;
    const double R1 = 9000.0;
    const double a0 = 0.7;
    const double a1 = -0.4;
    const double au = 0.9;

    // --- Series ---
    {
        using SeriesAdaptor2 = acfx::wdf::SeriesAdaptor<Probe, Probe>;
        SeriesAdaptor2 adaptor(Probe{R0, a0, 0.0}, Probe{R1, a1, 0.0});
        const double bu = adaptor.reflected();
        adaptor.incident(au);
        const double b0 = adaptor.child<0>().lastIncident;
        const double b1 = adaptor.child<1>().lastIncident;

        // Weighted balance holds (sanity: this is the real invariant).
        const double Gup = 1.0 / adaptor.portResistance();
        const double weightedIn = (1.0 / R0) * a0 * a0 + (1.0 / R1) * a1 * a1 + Gup * au * au;
        const double weightedOut = (1.0 / R0) * b0 * b0 + (1.0 / R1) * b1 * b1 + Gup * bu * bu;
        CHECK(std::abs(weightedIn - weightedOut) / std::max(weightedIn, 1e-30) < kBalanceTol);

        // Unweighted sums DIFFER (the wrong invariant).
        const double unweightedIn = a0 * a0 + a1 * a1 + au * au;
        const double unweightedOut = b0 * b0 + b1 * b1 + bu * bu;
        CHECK(std::abs(unweightedIn - unweightedOut) > 1e-9);
    }

    // --- Parallel ---
    {
        using ParallelAdaptor2 = acfx::wdf::ParallelAdaptor<Probe, Probe>;
        ParallelAdaptor2 adaptor(Probe{R0, a0, 0.0}, Probe{R1, a1, 0.0});
        const double bu = adaptor.reflected();
        adaptor.incident(au);
        const double b0 = adaptor.child<0>().lastIncident;
        const double b1 = adaptor.child<1>().lastIncident;

        const double Gup = 1.0 / adaptor.portResistance();
        const double weightedIn = (1.0 / R0) * a0 * a0 + (1.0 / R1) * a1 * a1 + Gup * au * au;
        const double weightedOut = (1.0 / R0) * b0 * b0 + (1.0 / R1) * b1 * b1 + Gup * bu * bu;
        CHECK(std::abs(weightedIn - weightedOut) / std::max(weightedIn, 1e-30) < kBalanceTol);

        const double unweightedIn = a0 * a0 + a1 * a1 + au * au;
        const double unweightedOut = b0 * b0 + b1 * b1 + bu * bu;
        CHECK(std::abs(unweightedIn - unweightedOut) > 1e-9);
    }
}
