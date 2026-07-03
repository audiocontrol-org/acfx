#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <vector>

#include "effects/program-dependent-saturation/program-dependent-saturation-core.h"

// T023 (US6) — detection topology: feedforward vs feedback (SC-006).
//
// Written to the SPEC's intended behavior (spec.md US6 / AC-1 / AC-2, SC-006;
// research.md Decision 3 — the feedback tap is the previous FINAL output y, a
// bounded, gain-compensated, one-sample-delayed loop that cannot algebraically
// loop and is asserted to converge), driving ProgramDependentSaturationCore
// directly via process(x, key) so a controlled STEADY input can be fed and the
// loop watched for convergence.
//
// The three behaviors under test, each asserted the ROBUST way (no brittle exact
// output value — the settled point is loop/hardware sensitive):
//
//   * FEEDBACK stability + convergence (AC-1, SC-006): with feedback detection
//     and a non-zero drive + bias depth, a steady constant-amplitude sine is fed
//     for > 200k samples. We assert the per-block output RMS SETTLES to a fixed
//     point — successive late-block RMS stops changing beyond a tiny fraction of
//     the RMS — and that every sample stays FINITE and BOUNDED (no oscillation /
//     divergence) across the whole run.
//
//   * COLD START (AC-2, SC-014): a freshly prepared/reset core in feedback mode
//     has prevOutput = 0 (defined floor, never uninitialized). The FIRST sample,
//     and the whole first block, are finite — the loop reads a defined initial
//     output from sample 0.
//
//   * FEEDFORWARD vs FEEDBACK differ (SC-006): for the SAME input and SAME depth,
//     feedforward reads the raw input level while feedback reads the (saturated,
//     level-shifted) output level, so the two map to different modulated drives.
//     We assert the two output trajectories are measurably different (RMS of the
//     difference is a non-trivial fraction of the signal, and they are not
//     sample-identical) yet BOTH stable and finite.
//
// Robustness choices:
//   * A constant-amplitude sine at an INTEGER number of cycles per analysis block
//     (220 Hz -> exactly 22 cycles per 4800-sample block) gives a per-block RMS
//     that is flat across blocks once the loop settles, so successive-block RMS
//     is a clean fixed-point probe free of windowing ripple.
//   * Convergence is a RELATIVE bound (delta as a fraction of the RMS), never an
//     absolute magic number, so it survives gain-compensation / voicing changes.

using acfx::Detection;
using acfx::ModTarget;
using acfx::ProgramDependentSaturationCore;

namespace {

// ---------------------------------------------------------------------------
// Stimulus / analysis geometry
// ---------------------------------------------------------------------------
inline constexpr float  kSampleRate = 48000.0f;
inline constexpr double kTwoPi      = 6.283185307179586476925286766559;

// Steady sine: 220 Hz over a 4800-sample block is exactly 22 cycles, so each
// block's RMS is stable and successive-block RMS convergence is leakage-free.
inline constexpr double kSineHz    = 220.0;
inline constexpr double kSineAmp   = 0.1;  // ~ -20 dBFS: mid of the -60..0 dB norm window
inline constexpr int    kBlockLen  = 4800; // 0.1 s
inline constexpr int    kNumBlocks = 42;   // 201600 samples (> 200k, > 4 s >> ballistics)
inline constexpr int    kTotalSamples = kBlockLen * kNumBlocks;

// ---------------------------------------------------------------------------
// Modulation config that makes the topology fork consequential: a positive base
// drive with a strong positive drive depth (plus a bias depth, per AC), so the
// shared envelope genuinely re-shapes the nonlinearity and the feedforward
// (input-level) vs feedback (output-level) detection map to different drives.
// ---------------------------------------------------------------------------
inline constexpr float kBaseDriveDb = 6.0f;
inline constexpr float kDriveDepth  = 0.6f;
inline constexpr float kBiasDepth   = 0.3f;

// ---------------------------------------------------------------------------
// Bounds / tolerances (robust, relative — never brittle exact numbers)
// ---------------------------------------------------------------------------
// Gain-compensated waveshaper output stays comfortably within this; catches any
// divergence at any point in the run.
inline constexpr double kBoundedAbs = 8.0;
// Blocks to let the loop settle before sampling the fixed point (> 3 s here).
inline constexpr int    kSettleBlocks = 30;
// Fixed point: successive late-block RMS deltas are a tiny fraction of the RMS.
inline constexpr double kConvergeRelEps = 2.0e-3;
// Whole-window flatness (peak-to-peak) is slightly looser than the per-step delta
// to tolerate a negligible residual drift.
inline constexpr double kStableRelBand = 5.0e-3;
// Feedforward-vs-feedback trajectory difference: RMS of (ff - fb) relative to the
// feedforward signal RMS must clear this floor (the characterized topology gap).
inline constexpr double kMinTrajSeparation = 1.0e-3;

// ---------------------------------------------------------------------------
// Build a core wired for the topology comparison, freshly reset (cold start).
// ---------------------------------------------------------------------------
ProgramDependentSaturationCore makeCore(Detection detection) {
    ProgramDependentSaturationCore core;
    core.prepare(kSampleRate);
    core.setStaticDrive(kBaseDriveDb);
    core.setDepth(ModTarget::drive, kDriveDepth);
    core.setDepth(ModTarget::bias, kBiasDepth);
    core.setDetection(detection);
    core.reset(); // prevOutput_ -> 0: defined feedback cold start
    return core;
}

// ---------------------------------------------------------------------------
// Drive a steady sine through the core, capturing per-sample output, per-block
// RMS, and the run-wide finiteness / boundedness invariants.
// ---------------------------------------------------------------------------
struct SteadyRun {
    std::vector<float>  y;        // full per-sample output
    std::vector<double> blockRms; // RMS per kBlockLen block
    bool allFinite = true;
    bool bounded   = true;
};

SteadyRun driveSteady(Detection detection, int totalSamples) {
    ProgramDependentSaturationCore core = makeCore(detection);

    SteadyRun r;
    r.y.reserve(static_cast<std::size_t>(totalSamples));
    r.blockRms.reserve(static_cast<std::size_t>(totalSamples / kBlockLen + 1));

    const double w = kTwoPi * kSineHz / static_cast<double>(kSampleRate);
    double blockSumSq = 0.0;
    int inBlock = 0;
    for (int n = 0; n < totalSamples; ++n) {
        const float x = static_cast<float>(kSineAmp * std::sin(w * static_cast<double>(n)));
        const float y = core.process(x, 0.0f);
        if (!std::isfinite(y)) r.allFinite = false;
        if (std::fabs(static_cast<double>(y)) > kBoundedAbs) r.bounded = false;
        r.y.push_back(y);
        blockSumSq += static_cast<double>(y) * static_cast<double>(y);
        if (++inBlock == kBlockLen) {
            r.blockRms.push_back(std::sqrt(blockSumSq / static_cast<double>(kBlockLen)));
            blockSumSq = 0.0;
            inBlock = 0;
        }
    }
    return r;
}

// Late-window convergence statistics over blocks [settle, end).
struct Convergence {
    double maxSuccessiveDelta = 0.0; // largest |rms[i] - rms[i-1]|
    double meanRms            = 0.0;
    double latePkPk           = 0.0; // max - min over the late window
};

Convergence lateConvergence(const std::vector<double>& blockRms, int settle) {
    Convergence c;
    double sum = 0.0;
    double lo  = 0.0, hi = 0.0;
    double prev = 0.0;
    bool   have = false;
    int    cnt  = 0;
    for (std::size_t i = static_cast<std::size_t>(settle); i < blockRms.size(); ++i) {
        const double v = blockRms[i];
        if (have) c.maxSuccessiveDelta = std::max(c.maxSuccessiveDelta, std::fabs(v - prev));
        if (!have) { lo = hi = v; } else { lo = std::min(lo, v); hi = std::max(hi, v); }
        prev = v;
        have = true;
        sum += v;
        ++cnt;
    }
    c.meanRms  = (cnt > 0) ? sum / static_cast<double>(cnt) : 0.0;
    c.latePkPk = have ? (hi - lo) : 0.0;
    return c;
}

// Assert a run converged to a stable, non-trivial fixed point.
void checkConverged(const SteadyRun& run) {
    REQUIRE(run.allFinite);
    REQUIRE(run.bounded);
    REQUIRE(run.blockRms.size() > static_cast<std::size_t>(kSettleBlocks));

    const Convergence c = lateConvergence(run.blockRms, kSettleBlocks);
    REQUIRE(c.meanRms > 0.0); // a real, audible steady state (not collapsed to silence)

    // Fixed point: the modulation/output stops changing block-to-block.
    CHECK(c.maxSuccessiveDelta <= kConvergeRelEps * c.meanRms);
    // ...and the whole late window is flat (no slow drift, no oscillation).
    CHECK(c.latePkPk <= kStableRelBand * c.meanRms);
}

double signalRms(const std::vector<float>& v) {
    if (v.empty()) return 0.0;
    double s = 0.0;
    for (float x : v) s += static_cast<double>(x) * static_cast<double>(x);
    return std::sqrt(s / static_cast<double>(v.size()));
}

} // namespace

// ===========================================================================
// T023 (US6) — feedback settles to a stable fixed point (AC-1, SC-006)
// ===========================================================================

TEST_CASE("T023/US6 feedback topology settles to a stable, bounded fixed point (SC-006)") {
    const SteadyRun run = driveSteady(Detection::feedBack, kTotalSamples);

    // Finite + bounded across the ENTIRE > 200k-sample run: the one-sample-delayed,
    // gain-compensated loop neither diverges nor oscillates (research.md Decision 3).
    CHECK(run.allFinite);
    CHECK(run.bounded);

    // Converged to a stable, non-trivial steady state (successive-block RMS flat).
    checkConverged(run);
}

// ===========================================================================
// T023 (US6) — feedback cold start (AC-2, SC-014)
// ===========================================================================

TEST_CASE("T023/US6 feedback cold start yields finite output from sample 0 (AC-2)") {
    // A freshly prepared/reset core: prevOutput_ == 0 (a defined floor, never
    // uninitialized). The first sample reads that floor through the detector; the
    // whole first block must be finite with no NaN/Inf from the cold feedback tap.
    ProgramDependentSaturationCore core = makeCore(Detection::feedBack);

    const double w = kTwoPi * kSineHz / static_cast<double>(kSampleRate);

    const float y0 = core.process(static_cast<float>(kSineAmp * std::sin(0.0)), 0.0f);
    CHECK(std::isfinite(y0)); // sample 0 is defined

    bool firstBlockFinite = std::isfinite(y0);
    for (int n = 1; n < kBlockLen; ++n) {
        const float x = static_cast<float>(kSineAmp * std::sin(w * static_cast<double>(n)));
        const float y = core.process(x, 0.0f);
        if (!std::isfinite(y)) firstBlockFinite = false;
    }
    CHECK(firstBlockFinite);
}

// ===========================================================================
// T023 (US6) — feedforward vs feedback differ, both stable (SC-006)
// ===========================================================================

TEST_CASE("T023/US6 feedforward and feedback trajectories differ yet both converge (SC-006)") {
    const SteadyRun ff = driveSteady(Detection::feedForward, kTotalSamples);
    const SteadyRun fb = driveSteady(Detection::feedBack, kTotalSamples);

    // Both topologies are finite, bounded, and settle to a stable fixed point.
    checkConverged(ff);
    checkConverged(fb);

    // Trajectories must be measurably DIFFERENT: feedforward detects the raw input
    // level, feedback detects the saturated output level, so the shared envelope —
    // and thus the modulated drive/bias — differs, driving different outputs.
    REQUIRE(ff.y.size() == fb.y.size());
    std::vector<float> diff;
    diff.reserve(ff.y.size());
    bool anyDifferent = false;
    for (std::size_t n = 0; n < ff.y.size(); ++n) {
        const float d = ff.y[n] - fb.y[n];
        if (d != 0.0f) anyDifferent = true;
        diff.push_back(d);
    }
    CHECK(anyDifferent); // not sample-for-sample identical

    const double refRms  = signalRms(ff.y);
    REQUIRE(refRms > 0.0);
    const double diffRms = signalRms(diff);
    // The topology distinction, asserted as a relative separation rather than a
    // brittle exact number.
    CHECK((diffRms / refRms) >= kMinTrajSeparation);
}
