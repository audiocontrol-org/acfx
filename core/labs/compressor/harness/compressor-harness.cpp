// compressor-harness.cpp
// Host-only lab evidence harness: never included by portable code (C-1 gate
// in scripts/check-portability.sh).
//
// Drives the graduated acfx::GainComputer (core/primitives/dynamics/gain-computer.h)
// and the acfx::CompressorCore composition kernel (core/effects/compressor/
// compressor-core.h) through representative stimuli and prints measured
// evidence of their behavior to stdout:
//   1. Compressor static curve: gain reduction across input levels.
//   2. Limiter static curve: hard knee at threshold.
//   3. Expander static curve: with range floor.
//   4. Gate static curve: hard drop to the range floor below threshold.
//   5. CompressorCore step response (level ballistics site): a key step
//      above threshold, then back to silence, tracing the gain-reduction
//      trajectory through attack and release -- cross-checked against the
//      GainComputer static-curve value the ballistics converge to.
//   6. Lookahead latency: a main-path impulse (at unity gain, via a silent
//      detection key) reappears at the output exactly lookaheadSamples later.
//
// This harness is self-contained (direct computation / a direct sample-loop
// against the primitives' public API) rather than routing through other
// shared measurement infrastructure -- no spectral analysis needed here.
//
// Include roots: core/ (see the acfx_lab_compressor_harness CMake
// target). Compile with -std=c++20. May allocate, loop, printf.

#include "primitives/dynamics/gain-computer.h"
#include "primitives/dynamics/envelope-follower.h"
#include "effects/compressor/compressor-core.h"

#include <cmath>
#include <cstdio>

// ---------------------------------------------------------------------------
// Constants
// ---------------------------------------------------------------------------

namespace {

constexpr float kMinLevelDb = -60.0f;
constexpr float kMaxLevelDb = 0.0f;
constexpr float kStepDb     = 6.0f;

constexpr float kSampleRate = 48000.0f;

// One-pole step-response crossings: 1 - 1/e (attack, rising toward a target)
// and 1/e (release, falling from a peak), used to locate the sample at which
// a smoothed gain-reduction trajectory reaches its nominal time constant.
constexpr float kOneMinusInvE = 0.63212055f;
constexpr float kInvE         = 0.36787944f;

} // namespace

// ---------------------------------------------------------------------------
// 1. Compressor static curve
// ---------------------------------------------------------------------------

namespace {

void runCompressorCurve() {
    acfx::GainComputer gc;
    gc.setMode(acfx::GainMode::compress);
    gc.setThreshold(-18.0f);
    gc.setRatio(4.0f);
    gc.setKnee(6.0f);
    gc.setRange(-40.0f);

    std::printf("=== 1. Compressor static curve (threshold=-18dB, ratio=4:1, knee=6dB) ===\n");
    std::printf("  %10s %10s\n", "level (dB)", "gain (dB)");

    for (float levelDb = kMinLevelDb; levelDb <= kMaxLevelDb; levelDb += kStepDb) {
        const float gainDb = gc.computeGainDb(levelDb);
        std::printf("  %10.1f %10.4f\n", static_cast<double>(levelDb), static_cast<double>(gainDb));
    }
    std::printf("\n");
}

} // namespace

// ---------------------------------------------------------------------------
// 2. Limiter static curve
// ---------------------------------------------------------------------------

namespace {

void runLimiterCurve() {
    acfx::GainComputer gc;
    gc.setMode(acfx::GainMode::limit);
    gc.setThreshold(-12.0f);
    gc.setRatio(8.0f); // ignored by limiter (infinite ratio)
    gc.setKnee(0.0f);  // hard corner
    gc.setRange(-40.0f);

    std::printf("=== 2. Limiter static curve (threshold=-12dB, hard knee) ===\n");
    std::printf("  %10s %10s\n", "level (dB)", "gain (dB)");

    for (float levelDb = kMinLevelDb; levelDb <= kMaxLevelDb; levelDb += kStepDb) {
        const float gainDb = gc.computeGainDb(levelDb);
        std::printf("  %10.1f %10.4f\n", static_cast<double>(levelDb), static_cast<double>(gainDb));
    }
    std::printf("\n");
}

} // namespace

// ---------------------------------------------------------------------------
// 3. Expander static curve
// ---------------------------------------------------------------------------

namespace {

void runExpanderCurve() {
    acfx::GainComputer gc;
    gc.setMode(acfx::GainMode::expand);
    gc.setThreshold(-24.0f);
    gc.setRatio(2.0f);
    gc.setKnee(6.0f);
    gc.setRange(-12.0f); // floor at -12 dB

    std::printf("=== 3. Expander static curve (threshold=-24dB, ratio=1:2, range=-12dB) ===\n");
    std::printf("  %10s %10s\n", "level (dB)", "gain (dB)");

    for (float levelDb = kMinLevelDb; levelDb <= kMaxLevelDb; levelDb += kStepDb) {
        const float gainDb = gc.computeGainDb(levelDb);
        std::printf("  %10.1f %10.4f\n", static_cast<double>(levelDb), static_cast<double>(gainDb));
    }
    std::printf("\n");
}

} // namespace

// ---------------------------------------------------------------------------
// 4. Gate static curve
// ---------------------------------------------------------------------------

namespace {

void runGateCurve() {
    acfx::GainComputer gc;
    gc.setMode(acfx::GainMode::gate);
    gc.setThreshold(-30.0f);
    gc.setKnee(3.0f);
    gc.setRange(-60.0f); // floor; gate ignores ratio (mLow derives from range/knee alone)

    std::printf("=== 4. Gate static curve (threshold=-30dB, knee=3dB, range=-60dB) ===\n");
    std::printf("  %10s %10s\n", "level (dB)", "gain (dB)");

    for (float levelDb = kMinLevelDb; levelDb <= kMaxLevelDb; levelDb += kStepDb) {
        const float gainDb = gc.computeGainDb(levelDb);
        std::printf("  %10.1f %10.4f\n", static_cast<double>(levelDb), static_cast<double>(gainDb));
    }
    std::printf("\n");
}

} // namespace

// ---------------------------------------------------------------------------
// 5. CompressorCore step response (level ballistics site)
// ---------------------------------------------------------------------------

namespace {

void runLevelSiteBallistics() {
    // Cross-check target: the static curve's steady-state value at -6 dBFS
    // (12 dB above the -18 dB threshold), independent of ballistics -- the
    // trajectory below should settle near this value.
    acfx::GainComputer refGain;
    refGain.setMode(acfx::GainMode::compress);
    refGain.setThreshold(-18.0f);
    refGain.setRatio(4.0f);
    refGain.setKnee(6.0f);
    constexpr float kAboveDb     = -6.0f;
    const float     staticTargetDb = refGain.computeGainDb(kAboveDb);

    acfx::CompressorCore core;
    core.prepare(kSampleRate, 0); // no lookahead needed to observe ballistics alone
    core.setMode(acfx::GainMode::compress);
    core.setThreshold(-18.0f);
    core.setRatio(4.0f);
    core.setKnee(6.0f);
    core.setDetector(acfx::DetectMode::peak);
    core.setDetection(acfx::Detection::feedForward);
    core.setBallisticsSite(acfx::BallisticsSite::level);

    constexpr float kAttackSec  = 0.010f;
    constexpr float kReleaseSec = 0.100f;
    core.setAttack(kAttackSec);
    core.setRelease(kReleaseSec);
    core.setMakeup(0.0f);
    core.setAutoMakeup(false);

    const float keyAbove = std::pow(10.0f, kAboveDb / 20.0f);

    std::printf("=== 5. CompressorCore step response (level-site ballistics) ===\n");
    std::printf("  compress thr=-18dB ratio=4:1 knee=6dB attack=%.0fms release=%.0fms\n",
                static_cast<double>(kAttackSec * 1000.0f), static_cast<double>(kReleaseSec * 1000.0f));
    std::printf("  key steps to %.0f dBFS (static-curve target %.4f dB) then to silence\n",
                static_cast<double>(kAboveDb), static_cast<double>(staticTargetDb));
    std::printf("  %8s %8s %14s\n", "sample", "t(ms)", "gain reduction (dB)");

    // Rising phase: key steps to -6 dBFS (above threshold). 500 ms is
    // comfortably past 5x the 10 ms attack time constant.
    constexpr int kAttackPhaseSamples = 24000;
    constexpr int kPrintStride        = 2000;
    int   attackCrossSample = -1;
    float grDbAtEndOfAttack = 0.0f;
    for (int n = 0; n < kAttackPhaseSamples; ++n) {
        const float gainLin = core.detectGainLin(keyAbove);
        const float grDb    = 20.0f * std::log10(gainLin);
        if (n % kPrintStride == 0) {
            std::printf("  %8d %8.2f %14.4f\n", n,
                        static_cast<double>(static_cast<float>(n) / kSampleRate * 1000.0f),
                        static_cast<double>(grDb));
        }
        if (attackCrossSample < 0 && grDb <= kOneMinusInvE * staticTargetDb) {
            attackCrossSample = n;
        }
        grDbAtEndOfAttack = grDb;
    }

    // Falling phase: key steps down to silence (deep below threshold). 500 ms
    // is comfortably past 5x the 100 ms release time constant.
    constexpr int kReleasePhaseSamples = 24000;
    int         releaseCrossSample = -1;
    const float releaseThresholdDb = kInvE * grDbAtEndOfAttack;
    for (int n = 0; n < kReleasePhaseSamples; ++n) {
        const float gainLin = core.detectGainLin(0.0f);
        const float grDb    = 20.0f * std::log10(gainLin);
        if (n % kPrintStride == 0) {
            std::printf("  %8d %8.2f %14.4f\n", kAttackPhaseSamples + n,
                        static_cast<double>(static_cast<float>(kAttackPhaseSamples + n) / kSampleRate * 1000.0f),
                        static_cast<double>(grDb));
        }
        if (releaseCrossSample < 0 && grDb >= releaseThresholdDb) {
            releaseCrossSample = n;
        }
    }

    const float attackMs =
        (attackCrossSample >= 0)
            ? static_cast<float>(attackCrossSample + 1) / kSampleRate * 1000.0f
            : -1.0f;
    const float releaseMs =
        (releaseCrossSample >= 0)
            ? static_cast<float>(releaseCrossSample + 1) / kSampleRate * 1000.0f
            : -1.0f;

    std::printf("  configured attack:  %.2f ms\n", static_cast<double>(kAttackSec * 1000.0f));
    std::printf("  measured attack:    %.4f ms (sample %d, gr=%.4f dB crosses %.4f = (1-1/e) of static target)\n",
                static_cast<double>(attackMs), attackCrossSample,
                static_cast<double>(grDbAtEndOfAttack), static_cast<double>(kOneMinusInvE * staticTargetDb));
    std::printf("  configured release: %.2f ms\n", static_cast<double>(kReleaseSec * 1000.0f));
    std::printf("  measured release:   %.4f ms (sample %d, crosses %.4f = 1/e of peak %.4f dB)\n",
                static_cast<double>(releaseMs), releaseCrossSample,
                static_cast<double>(releaseThresholdDb), static_cast<double>(grDbAtEndOfAttack));
    std::printf("\n");
}

} // namespace

// ---------------------------------------------------------------------------
// 6. Lookahead latency
// ---------------------------------------------------------------------------

namespace {

void runLookaheadLatency() {
    constexpr float kLookaheadSec  = 0.005f; // 5 ms
    const int       expectedSamples = static_cast<int>(std::lround(kLookaheadSec * kSampleRate));

    acfx::CompressorCore core;
    core.prepare(kSampleRate, expectedSamples);
    core.setMode(acfx::GainMode::compress);
    core.setThreshold(-18.0f);
    core.setRatio(4.0f);
    core.setMix(1.0f);
    core.setOutput(0.0f);
    core.setLookahead(expectedSamples);

    // Detection key held at silence throughout, so gain reduction stays 0
    // (unity gain) for the whole run -- the only thing under measurement here
    // is the lookahead delay applied to the main path.
    constexpr float kKey = 0.0f;

    constexpr int kNumSamples = 4800; // 100 ms: comfortably longer than the lookahead
    int measuredDelaySamples  = -1;
    for (int n = 0; n < kNumSamples; ++n) {
        const float x       = (n == 0) ? 1.0f : 0.0f; // impulse on the main path
        const float gainLin = core.detectGainLin(kKey);
        const float y       = core.applyGain(x, gainLin);
        if (measuredDelaySamples < 0 && std::fabs(y) > 0.5f) {
            measuredDelaySamples = n;
        }
    }

    std::printf("=== 6. Lookahead latency ===\n");
    std::printf("  configured lookahead: %.2f ms @ fs=%.0f Hz\n",
                static_cast<double>(kLookaheadSec * 1000.0f), static_cast<double>(kSampleRate));
    std::printf("  expected latency:     %d samples (round(lookaheadSeconds * fs))\n", expectedSamples);
    std::printf("  measured latency:     %d samples (main-path impulse reappears here)\n",
                measuredDelaySamples);
    std::printf("\n");
}

} // namespace

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------

int main() {
    std::printf("=== acfx Compressor lab harness (fs = %.0f Hz) ===\n\n",
                static_cast<double>(kSampleRate));

    runCompressorCurve();
    runLimiterCurve();
    runExpanderCurve();
    runGateCurve();
    runLevelSiteBallistics();
    runLookaheadLatency();

    std::printf("=== Done ===\n");
    return 0;
}
