#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>

#include "core/hysteresis-test-support.h"
#include "effects/tape-dynamics/tape-dynamics-core.h"

using namespace acfx;
using namespace acfx::hysteresistest;

// T019 -- US3/SC-002/contract C4: solver-selection agreement and
// convergence-under-oversampling, exercised through TapeDynamicsCore<Factor>
// so the OVERSAMPLED integrator dt (Factor * base rate, per
// tape-dynamics-core.h's prepare()) is genuinely on the test's axis, not just
// the raw Hysteresis primitive's base-rate dt already covered by T007/T008.
//
// hysteresis-solver-test.cpp (T007/T008) already pins each stepper's
// individual behavior and a "loose" Newton-vs-RK4 sanity check at the
// primitive's base rate. This file's job is narrower and specific to T019:
// (1) name a tolerance and show ALL THREE solvers agree within it at a fixed
//     Factor, (2) show the rk2/rk4 disagreement TIGHTENS as Factor rises
//     (SC-002's actual numeric claim), (3) a light finiteness check on the
//     solver-selection path under a hot transient.
namespace {

constexpr double kFs = 48000.0;

// JA shape mirroring hysteresis-solver-test.cpp's T007/T008 loop-widening
// convention (k narrower than the JAParams default of 1, "widen the loop so
// the branch split is unambiguous"), driven with +12 dB of pre-magnetics
// drive so the anhysteretic/irreversible split is genuinely exercised across
// a wide swing of the Langevin curve's curvature (not just its near-linear
// center). Deliberately NOT a T008-style "mild drive" (Ms=1, k=1(default),
// amp=0.6, 0 dB): that combination measured empirically (see probe sweep in
// the T019 authoring notes) at only ~1e-7..1e-8 rk2/rk4 disagreement -- i.e.
// within a float ULP of the ~0.5-scale output, which is float-rounding
// noise, not a meaningful discretization-error signal to show tightening
// with Factor. The config below keeps peak |output| well inside the T009
// guard's bound (measured peak ~0.82, guard clamps at 4*Ms = 4) -- neither
// stepper is stiff/clamped here -- while giving genuine, well-above-noise
// rk2/rk4 disagreement (~3e-5 at Factor 2) that empirically tightens by
// almost exactly 4x per Factor doubling, matching RK2's O(dt^2) local error
// theory (halving dt should quarter a leading-order dt^2 error term).
struct DriveConfig {
    float driveDb = 12.0f;
    float saturation = 1.0f;  // -> JAParams.Ms
    float width = 0.3f;       // -> JAParams.k (narrower loop, per T007/T008)
    double amp = 1.5;
    int cycles = 3;
    int stepsPerCycle = 512;  // BASE-RATE samples/cycle -- identical across
                               // Factor, so only the internal integrator dt
                               // (osRate = kFs * Factor) changes between runs.
};

constexpr DriveConfig kCfg{};

template <int Factor>
void configure(TapeDynamicsCore<Factor>& core, const DriveConfig& cfg,
               Solver solver) {
    core.prepare(kFs, 1);
    core.setDrive(cfg.driveDb);
    core.setSaturation(cfg.saturation);
    core.setWidth(cfg.width);
    core.setSolver(solver);
    core.setMix(1.0f);
    core.setOutput(0.0f);
}

// Per-sample max/RMS disagreement between two identically-configured cores
// (same Factor, same drive, different solver) driven with the SAME input
// sequence. Both cores share the Oversampler's group delay (a function of
// Factor alone -- see oversampler.h groupDelaySamples()), so same-index
// per-sample comparison is delay-neutral: any nonzero diff is genuinely the
// solvers disagreeing, not a phase artifact.
struct Disagreement {
    double maxAbs = 0.0;
    double rms = 0.0;
};

template <int Factor>
Disagreement measureDisagreement(Solver solverA, Solver solverB,
                                 const DriveConfig& cfg) {
    TapeDynamicsCore<Factor> a;
    TapeDynamicsCore<Factor> b;
    configure(a, cfg, solverA);
    configure(b, cfg, solverB);

    const int total = cfg.cycles * cfg.stepsPerCycle;
    double sumSq = 0.0;
    double maxAbs = 0.0;
    for (int n = 0; n <= total; ++n) {
        const double phase =
            2.0 * M_PI * (static_cast<double>(n) / cfg.stepsPerCycle);
        const double x = cfg.amp * std::sin(phase);
        const double ya =
            static_cast<double>(a.processSample(static_cast<float>(x), 0));
        const double yb =
            static_cast<double>(b.processSample(static_cast<float>(x), 0));
        REQUIRE(std::isfinite(ya));
        REQUIRE(std::isfinite(yb));
        const double diff = std::fabs(ya - yb);
        maxAbs = std::max(maxAbs, diff);
        sumSq += diff * diff;
    }
    Disagreement d;
    d.maxAbs = maxAbs;
    d.rms = std::sqrt(sumSq / static_cast<double>(total + 1));
    return d;
}

}  // namespace

// -----------------------------------------------------------------------
// (1) Cross-solver agreement at a fixed Factor (contract C4).
// -----------------------------------------------------------------------
//
// Tolerance: 5.0e-2 on max-abs per-sample disagreement, on an output whose
// magnitude is Ms-scale (peak |output| measures ~0.82; kCfg.saturation ==
// 1.0). This is the SAME numeric tolerance
// hysteresis-solver-test.cpp's T008 "newtonRaphson stays close to RK4" case
// already uses for an equivalent mild-drive comparison at the RAW base rate
// (dt = 1/48000). Here the comparison runs at Factor=8's OVERSAMPLED dt
// (dt = 1/384000, 8x finer), so if anything the true disagreement should sit
// well UNDER that base-rate-derived bound, not against it -- the 5e-2 figure
// is carried over as a deliberately generous, previously-justified ceiling
// rather than a value tuned to this specific run.
TEST_CASE("TapeDynamicsCore<8> -- cross-solver agreement (T019, C4)") {
    constexpr double kTolerance = 5.0e-2;

    const Disagreement rk2VsRk4 =
        measureDisagreement<8>(Solver::rk2, Solver::rk4, kCfg);
    const Disagreement rk2VsNewton =
        measureDisagreement<8>(Solver::rk2, Solver::newtonRaphson, kCfg);
    const Disagreement rk4VsNewton =
        measureDisagreement<8>(Solver::rk4, Solver::newtonRaphson, kCfg);

    CAPTURE(rk2VsRk4.maxAbs);
    CAPTURE(rk2VsNewton.maxAbs);
    CAPTURE(rk4VsNewton.maxAbs);
    CHECK(rk2VsRk4.maxAbs < kTolerance);
    CHECK(rk2VsNewton.maxAbs < kTolerance);
    CHECK(rk4VsNewton.maxAbs < kTolerance);
}

// -----------------------------------------------------------------------
// (2) Convergence tightens with oversampling -- the core SC-002 claim.
// -----------------------------------------------------------------------
//
// rk2 (O(dt^2) local truncation error) vs rk4 (O(dt^4)) is the pair with the
// most room to disagree, so it is the most sensitive probe of "does raising
// Factor actually shrink discretization error". TapeDynamicsCore<Factor>
// configures the composed Hysteresis with dt = 1/(kFs * Factor) (see
// tape-dynamics-core.h prepare()), so doubling Factor halves dt; RK2's
// leading error term scales like dt^2, so each Factor doubling should shrink
// the rk2/rk4 gap by roughly 4x if the two steppers are genuinely converging
// to the same underlying trajectory as claimed by SC-002.
TEST_CASE("TapeDynamicsCore -- rk2/rk4 disagreement tightens with "
          "oversampling (T019, SC-002)") {
    const Disagreement d2 = measureDisagreement<2>(Solver::rk2, Solver::rk4, kCfg);
    const Disagreement d4 = measureDisagreement<4>(Solver::rk2, Solver::rk4, kCfg);
    const Disagreement d8 = measureDisagreement<8>(Solver::rk2, Solver::rk4, kCfg);

    CAPTURE(d2.maxAbs);
    CAPTURE(d4.maxAbs);
    CAPTURE(d8.maxAbs);
    CAPTURE(d2.rms);
    CAPTURE(d4.rms);
    CAPTURE(d8.rms);

    // The core SC-002 assertion: raising the oversampling factor must not
    // leave the solvers MORE divergent, and the two extremes (Factor 2 vs
    // Factor 8, a 4x dt reduction) must show a REAL, not marginal,
    // improvement. Monotonic strict decrease at every step is asserted too --
    // if it doesn't hold, that is the "possible defect" this test exists to
    // surface (NOTES), not a threshold to relax.
    CHECK(d4.maxAbs < d2.maxAbs);
    CHECK(d8.maxAbs < d4.maxAbs);
    CHECK(d8.maxAbs < d2.maxAbs);

    CHECK(d4.rms < d2.rms);
    CHECK(d8.rms < d4.rms);
    CHECK(d8.rms < d2.rms);

    // Lower bound on the IMPROVEMENT (not just the direction): Factor 8 should
    // disagree by well under a quarter of Factor 2's max-abs gap. The O(dt^2)
    // argument above predicts ~16x over two doublings (dt/4 -> (dt/4)^2 =
    // dt^2/16); the measured ratio at this config is ~16x (2.99e-5 -> 1.88e-6),
    // matching the theory closely. The 4x bound asserted here still leaves a
    // 4x margin under that measurement so the check is robust to the JA ODE's
    // nonlinearity from run to run, while requiring a substantive, not
    // cosmetic, tightening.
    if (d2.maxAbs > 0.0) {
        CHECK(d8.maxAbs < 0.25 * d2.maxAbs);
    }
}

// -----------------------------------------------------------------------
// (3) No divergence on a hot transient, for every solver, at the
//     solver-SELECTION path (TapeDynamicsCore, not the bare primitive).
// -----------------------------------------------------------------------
//
// hysteresis-solver-test.cpp's T009 suite already owns the exhaustive
// primitive-level guard proof; this is intentionally light -- it exists only
// to confirm the guard's finiteness property survives being driven THROUGH
// the drive-gain + Oversampler composition for all three solver selections,
// which is a genuinely different code path (drive scaling + interpolation/
// decimation around the JA step) than calling Hysteresis::process() directly.
TEST_CASE("TapeDynamicsCore<8> -- hot transient stays finite under every "
          "solver (T019)") {
    const Solver solvers[] = {Solver::rk2, Solver::rk4, Solver::newtonRaphson};
    for (Solver solver : solvers) {
        TapeDynamicsCore<8> core;
        core.prepare(kFs, 1);
        core.setDrive(24.0f);       // heavy drive into the magnetics
        core.setSaturation(1.0f);
        core.setWidth(0.3f);        // narrower loop -> stiffer JA response
        core.setSolver(solver);
        core.setMix(1.0f);
        core.setOutput(0.0f);

        // Adversarial alternating full-scale steps -- a hot transient at the
        // effect's audio boundary (post-drive-gain, pre-oversampler).
        const float transientSteps[] = {1.0f, -1.0f, 1.0f, -1.0f, 0.7f, -0.7f};
        for (float x : transientSteps) {
            const float y = core.processSample(x, 0);
            CHECK(std::isfinite(y));
        }

        // Recovery: resume gentle drive and confirm continued finiteness.
        for (int n = 0; n < 1000; ++n) {
            const float x =
                0.4f * static_cast<float>(std::sin(2.0 * M_PI * n / 256.0));
            const float y = core.processSample(x, 0);
            CHECK(std::isfinite(y));
        }
    }
}
