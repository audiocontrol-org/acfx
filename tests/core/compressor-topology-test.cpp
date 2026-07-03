#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <vector>

#include "effects/compressor/compressor-core.h"     // CompressorCore, Detection, BallisticsSite
#include "primitives/dynamics/gain-computer.h"       // GainComputer (analytic reference curve)

// T021 (US5) — detection topology: feedforward vs feedback.
// T023 (US6) — ballistics site: level vs gain.
//
// Written to the SPEC's intended behavior (spec.md US5/US6, SC-002, SC-004;
// research.md Decisions 3 & 4), NOT to the current stub, so these are the
// intended TDD red until T022 (feedback path) and T024 (gain-site path) land.
//
// The two hard-to-pin behaviors are asserted the robust way:
//   * Feedback (T021): the exact settled level is loop-and-hardware sensitive,
//     so we assert STABILITY (a converged, flat, bounded, finite late window),
//     COMPRESSION (output below input above threshold), and the qualitative
//     FIXED POINT (the detected OUTPUT level, mapped through the SAME curve,
//     reproduces the applied gain) — plus that feedforward and feedback reach
//     DIFFERENT steady states, each matching its analytic model (SC-004).
//   * Ballistics site (T023): for each of level/gain we step between two
//     ABOVE-threshold levels with a HARD knee, so the whole trajectory lives in
//     the curve's linear region and the smoothed gain reduction is an exact
//     one-pole. We assert engagement reaches ~63% of the excursion in the
//     ATTACK time and recovery in the RELEASE time (SC-002) — proving both
//     sites smooth and neither swaps attack/release.
//
// Design choices that keep the assertions analytic:
//   * DC input for the level detector (peak mode): |x| is constant, so the
//     detector settles to a flat value with no inter-peak release ripple —
//     essential for the feedback stability/fixed-point checks.
//   * makeup = 0, mix = 1, output = 0 dB, lookahead = 0: then process() returns
//     exactly the post-makeup/pre-mix feedback tap (comp), and detectGainLin()
//     returns exactly dbToLin(grDb), so GR (dB) = 20*log10(gainLin).
//   * HARD knee (0 dB) with both step endpoints above threshold: the curve is
//     exactly linear across the whole trajectory, so a GR one-pole crossing is a
//     clean tau measurement free of threshold dead-time or knee curvature.

using acfx::BallisticsSite;
using acfx::CompressorCore;
using acfx::Detection;
using acfx::DetectMode;
using acfx::GainComputer;
using acfx::GainMode;

namespace {

// ---------------------------------------------------------------------------
// Named constants / tolerances
// ---------------------------------------------------------------------------
inline constexpr double kSampleRate  = 48000.0;
inline constexpr float  kSampleRateF = 48000.0f;

// Shared static curve (compress, hard knee): parameters mirrored into an
// independent reference GainComputer below so the tests never reach into the
// core's private state.
inline constexpr float kThresholdDb = -18.0f;
inline constexpr float kRatio       = 4.0f;
inline constexpr float kHardKnee    = 0.0f; // hard corner: curve linear above thr

// Ballistics for the timing suite: deliberately DISTINCT so a swapped
// attack/release would move a crossing by ~8x and blow the ±30% band.
inline constexpr double kAttackSec  = 0.010; // 10 ms
inline constexpr double kReleaseSec = 0.080; // 80 ms

// One-pole step-response crossing fraction of the excursion: 1 - 1/e (~63.2%).
// For engagement this is "63% of the way to steady"; for recovery the same
// fraction covered corresponds to 1/e of the excursion remaining — so the SAME
// fraction times the release tau.
inline constexpr double kOnePoleCrossFrac = 0.63212055882855767;

// Timing tolerance: ±30% of the target time constant (SC-002 guidance). The
// underlying one-poles are analytically exact here, so this is generous slack
// for the <=1-sample crossing quantization while still rejecting an
// instantaneous (crossing ~ 0) or swapped (crossing ~ the other tau) response.
inline constexpr double kTimingTolFrac = 0.30;

// T023 step endpoints — both ABOVE threshold (hard knee -> linear region).
inline constexpr double kLevelLowDb  = -12.0; // above thr by 6 dB   (GR = -4.5 dB)
inline constexpr double kLevelHighDb = -6.0;  // above thr by 12 dB  (GR = -9.0 dB)

// A sample must have covered < this fraction of the excursion on the FIRST
// post-step sample — i.e. the response ramps, it does not jump. One sample of a
// 10 ms one-pole at 48 kHz covers ~1/480 ~ 0.002, far under this bound.
inline constexpr double kNonInstantMaxFirstFrac = 0.10;

// T021 feedback / topology-comparison constants.
inline constexpr double kFeedbackInputDb = -6.0; // well above threshold
// Fixed-point / analytic-model self-consistency tolerance, as a fraction of the
// linear input amplitude. The loop converges to ~machine residual over the
// settle window, so this is comfortably slack yet still meaningful.
inline constexpr double kFixedPointRelTol = 0.02;
// Peak-to-peak bound over the late window: a converged, non-oscillating loop is
// essentially flat there. Far below any real oscillation amplitude.
inline constexpr double kStabilityBand = 1.0e-4;
// Absolute output bound: catches divergence at any point in the run.
inline constexpr double kBoundedAbs = 4.0;
// Minimum separation between feedforward and feedback steady outputs — they must
// be characterizably different (feedback senses the compressed signal, so it
// reduces less). Analytic gap for these params is ~0.10 (linear); 0.02 is safe.
inline constexpr double kMinTopologySeparation = 0.02;

inline constexpr int kSettleSamplesFeedback = 96000; // 2 s >> attack+release
inline constexpr int kLateWindowSamples     = 2048;
inline constexpr int kSettleSamplesStep     = 24000; // 0.5 s: detector fully settled
inline constexpr int kRecordSamplesStep     = 48000; // 1 s: covers the 80 ms release

// dB <-> linear helpers mirroring CompressorCore::dbToLin exactly.
double dbToLin(double db) noexcept { return std::pow(10.0, db * 0.05); }
double linToDb(double lin) noexcept { return 20.0 * std::log10(lin); }

// Build a fully-configured core: compress / hard knee / peak detector, the given
// topology + ballistics site, unit mix, 0 dB output, no makeup, no lookahead.
// Freshly reset (cold start) on return.
CompressorCore makeCore(Detection detection, BallisticsSite site,
                        double attackSec, double releaseSec) {
    CompressorCore c;
    c.prepare(kSampleRateF, /*maxLookaheadSamples=*/0);
    c.setMode(GainMode::compress);
    c.setThreshold(kThresholdDb);
    c.setRatio(kRatio);
    c.setKnee(kHardKnee);
    c.setDetector(DetectMode::peak);
    c.setBallisticsSite(site);
    c.setAttack(static_cast<float>(attackSec));
    c.setRelease(static_cast<float>(releaseSec));
    c.setDetection(detection);
    c.setAutoMakeup(false);
    c.setMakeup(0.0f);
    c.setMix(1.0f);
    c.setOutput(0.0f);
    c.setLookahead(0);
    c.reset();
    return c;
}

// Independent reference for the static curve, so a fixed-point / analytic-model
// assertion never depends on the core's private GainComputer.
GainComputer makeRefGain() {
    GainComputer g;
    g.setMode(GainMode::compress);
    g.setThreshold(kThresholdDb);
    g.setRatio(kRatio);
    g.setKnee(kHardKnee);
    return g;
}

// Gain reduction (dB) recovered from a linear gain (makeup == 0 here).
double grDbOf(float gainLin) noexcept { return linToDb(static_cast<double>(gainLin)); }

// Result of a step: the settled pre-step GR and the post-step GR trajectory.
struct StepRun {
    double              grStart = 0.0;
    std::vector<double> traj;
};

// Feedforward drive of detectGainLin(): settle at startLevelDb, snapshot the
// settled GR, then step to endLevelDb and record the GR trajectory. Feedforward
// so the detection path is isolated (no feedback loop); detectGainLin() alone
// advances the detector + gain smoother exactly once per sample.
StepRun stepRun(BallisticsSite site, double startLevelDb, double endLevelDb) {
    CompressorCore c = makeCore(Detection::feedForward, site, kAttackSec, kReleaseSec);
    const float startLin = static_cast<float>(dbToLin(startLevelDb));
    const float endLin   = static_cast<float>(dbToLin(endLevelDb));

    float g = 1.0f;
    for (int n = 0; n < kSettleSamplesStep; ++n) {
        g = c.detectGainLin(startLin);
    }
    StepRun run;
    run.grStart = grDbOf(g);
    run.traj.reserve(kRecordSamplesStep);
    for (int n = 0; n < kRecordSamplesStep; ++n) {
        run.traj.push_back(grDbOf(c.detectGainLin(endLin)));
    }
    return run;
}

// First index at which the trajectory has covered >= frac of the (signed)
// excursion grStart -> grEnd; -1 if never. Sign-agnostic: covered runs 0 -> 1
// for both a rising (engage) and a falling (recover) excursion.
int coveredCrossing(const std::vector<double>& traj, double grStart, double grEnd,
                    double frac) {
    const double span = grEnd - grStart;
    if (std::fabs(span) < 1e-9) return -1; // degenerate: no excursion to time
    for (std::size_t n = 0; n < traj.size(); ++n) {
        const double covered = (traj[n] - grStart) / span;
        if (covered >= frac) return static_cast<int>(n);
    }
    return -1;
}

// Assert a crossing sample index corresponds to tauSeconds within ±kTimingTolFrac.
void checkTiming(int crossingSample, double tauSeconds) {
    REQUIRE(crossingSample >= 0); // a crossing must exist at all
    const double measuredSeconds = static_cast<double>(crossingSample) / kSampleRate;
    CHECK(std::fabs(measuredSeconds - tauSeconds) <= kTimingTolFrac * tauSeconds);
}

// Drive process() with a constant DC input for the whole run, returning the last
// output plus flags for the invariants that must hold across every sample.
struct SteadyRun {
    double last      = 0.0;
    bool   allFinite = true;
    bool   bounded   = true;
    double latePkPk  = 0.0; // peak-to-peak over the final kLateWindowSamples
};

SteadyRun runConstant(CompressorCore& c, double xLin, int samples) {
    std::vector<float> tail;
    tail.reserve(kLateWindowSamples);
    SteadyRun r;
    const int tailStart = samples - kLateWindowSamples;
    for (int n = 0; n < samples; ++n) {
        const float y = c.process(static_cast<float>(xLin), static_cast<float>(xLin));
        if (!std::isfinite(y)) r.allFinite = false;
        if (std::fabs(static_cast<double>(y)) > kBoundedAbs) r.bounded = false;
        if (n >= tailStart) tail.push_back(y);
        r.last = static_cast<double>(y);
    }
    const auto mm = std::minmax_element(tail.begin(), tail.end());
    r.latePkPk = static_cast<double>(*mm.second) - static_cast<double>(*mm.first);
    return r;
}

} // namespace

// ===========================================================================
// T021 (US5) — feedback: stable, compressing fixed point (SC-004)
// ===========================================================================

TEST_CASE("feedback topology settles to a stable, compressing fixed point (SC-004)") {
    const double xLin = dbToLin(kFeedbackInputDb);

    CompressorCore fb = makeCore(Detection::feedBack, BallisticsSite::level,
                                 kAttackSec, kReleaseSec);
    const SteadyRun run = runConstant(fb, xLin, kSettleSamplesFeedback);

    // Finiteness + boundedness across the ENTIRE run (SC-013 + no divergence).
    CHECK(run.allFinite);
    CHECK(run.bounded);

    // Stability: the late window is essentially flat — converged, no oscillation.
    CHECK(run.latePkPk <= kStabilityBand);

    const double settledComp = run.last;
    CHECK(std::isfinite(settledComp));
    CHECK(settledComp > 0.0);              // positive DC in -> positive out
    CHECK(settledComp < xLin);             // COMPRESSION: output below input

    // Fixed point: the detected OUTPUT level, mapped through the SAME curve, must
    // reproduce the applied gain — i.e. comp == x * dbToLin(curve(20log10(comp))).
    // (The feedback tap is comp = x * gainLin with makeup 0, mix 1, output 0 dB;
    // at steady state the dB-domain detector reads exactly 20log10(comp).)
    const GainComputer ref = makeRefGain();
    const double settledDb    = linToDb(settledComp);
    const double refGrDb      = static_cast<double>(ref.computeGainDb(static_cast<float>(settledDb)));
    const double predictedComp = xLin * dbToLin(refGrDb);
    CHECK(std::fabs(settledComp - predictedComp) <= kFixedPointRelTol * xLin);
}

TEST_CASE("feedforward and feedback compress the same input to different steady states (SC-004)") {
    const double xLin = dbToLin(kFeedbackInputDb);

    auto settledOutput = [&](Detection detection) {
        CompressorCore c = makeCore(detection, BallisticsSite::level,
                                    kAttackSec, kReleaseSec);
        const SteadyRun run = runConstant(c, xLin, kSettleSamplesFeedback);
        REQUIRE(run.allFinite);
        REQUIRE(run.bounded);
        CHECK(run.latePkPk <= kStabilityBand); // both topologies converge & are flat
        return run.last;
    };

    const double ff = settledOutput(Detection::feedForward);
    const double fb = settledOutput(Detection::feedBack);

    // Both compress: steady output below the input, above threshold.
    CHECK(ff > 0.0);
    CHECK(fb > 0.0);
    CHECK(ff < xLin);
    CHECK(fb < xLin);

    // Feedback senses the ALREADY-compressed signal -> detects a lower level ->
    // applies LESS gain reduction -> a HIGHER steady output than feedforward.
    // Their steady values (and trajectories) differ — the characterized topology
    // distinction, asserted as an ordering + a minimum separation rather than a
    // brittle exact number.
    CHECK(fb > ff);
    CHECK((fb - ff) >= kMinTopologySeparation);

    // Each steady state matches its own analytic model via the reference curve.
    const GainComputer ref = makeRefGain();

    // Feedforward: detector sees the raw input level X -> GR = curve(X).
    const double xDb        = linToDb(xLin);
    const double ffGrDb      = static_cast<double>(ref.computeGainDb(static_cast<float>(xDb)));
    const double ffPredicted = xLin * dbToLin(ffGrDb);
    CHECK(std::fabs(ff - ffPredicted) <= kFixedPointRelTol * xLin);

    // Feedback: fixed point of the detector->curve->gain loop on the output level.
    const double fbDb        = linToDb(fb);
    const double fbGrDb      = static_cast<double>(ref.computeGainDb(static_cast<float>(fbDb)));
    const double fbPredicted = xLin * dbToLin(fbGrDb);
    CHECK(std::fabs(fb - fbPredicted) <= kFixedPointRelTol * xLin);
}

// ===========================================================================
// T023 (US6) — ballistics site timing: level vs gain (SC-002)
// ===========================================================================

TEST_CASE("ballistics site: engagement uses attack, recovery uses release (SC-002)") {
    // For each site, step between two above-threshold levels (hard knee -> the
    // GR is an exact one-pole over the linear region) and assert the GR crosses
    // ~63% of the excursion in the ATTACK time on the way up and in the RELEASE
    // time on the way down. Distinct attack/release means a swapped mapping
    // would miss both ±30% bands.

    auto exerciseSite = [](BallisticsSite site) {
        // Engagement: quiet-ish (above thr) -> louder. GR magnitude RISES.
        {
            const StepRun up = stepRun(site, kLevelLowDb, kLevelHighDb);
            const double grEnd = up.traj.back();

            // Must actually ramp, not jump: the first post-step sample has barely
            // moved off the pre-step steady GR.
            const double firstCovered =
                (up.traj.front() - up.grStart) / (grEnd - up.grStart);
            CHECK(firstCovered < kNonInstantMaxFirstFrac);

            // ...but the excursion is genuinely traversed (curve engaged more GR).
            CHECK(std::fabs(grEnd - up.grStart) > 1.0); // > 1 dB of extra reduction

            const int cross = coveredCrossing(up.traj, up.grStart, grEnd,
                                               kOnePoleCrossFrac);
            checkTiming(cross, kAttackSec); // engagement follows ATTACK
        }

        // Recovery: louder -> quieter (still above thr). GR magnitude FALLS.
        {
            const StepRun down = stepRun(site, kLevelHighDb, kLevelLowDb);
            const double grEnd = down.traj.back();

            const double firstCovered =
                (down.traj.front() - down.grStart) / (grEnd - down.grStart);
            CHECK(firstCovered < kNonInstantMaxFirstFrac);
            CHECK(std::fabs(grEnd - down.grStart) > 1.0);

            const int cross = coveredCrossing(down.traj, down.grStart, grEnd,
                                              kOnePoleCrossFrac);
            checkTiming(cross, kReleaseSec); // recovery follows RELEASE
        }
    };

    SUBCASE("level site — smoothing on the detected level path") {
        exerciseSite(BallisticsSite::level);
    }
    SUBCASE("gain site — smoothing on the gain-reduction signal") {
        exerciseSite(BallisticsSite::gain);
    }
}
