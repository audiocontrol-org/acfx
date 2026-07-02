// envelope-follower-harness.cpp
// Host-only lab evidence harness: never included by portable code (C-1 gate
// in scripts/check-portability.sh).
//
// Drives the graduated acfx::EnvelopeFollower (core/primitives/dynamics/
// envelope-follower.h) through representative stimuli at fs = 48000 Hz and
// prints measured evidence of its behavior to stdout:
//   1. Attack/release timing (branching, peak): unit step; measured vs
//      configured attack (63.2% crossing) and release (36.8% crossing).
//   2. Branching vs decoupled ballistics on a transient-then-decay signal:
//      sampled envelope values from each topology side by side.
//   3. RMS vs peak detection on a steady 1 kHz sine of amplitude 1.0:
//      settled peak (~1.0) vs settled RMS (~0.7071 = 1/sqrt(2)).
//   4. Peak-hold dwell: impulse then silence with a 50 ms hold; measured
//      number of samples the envelope stays latched before releasing.
//   5. dB domain: settled dB envelope for a steady 0.5 input (~-6.02 dB)
//      and the floor for silence (-120 dB).
//
// This harness is self-contained (a direct sample-loop against the
// primitive's public API) rather than routing through
// tests/core/measurement-support.h -- none of the measurements here need
// spectral analysis, so a second dependency on the shared measurement
// infrastructure would add indirection without adding evidence.
//
// Include roots: core/ (see the acfx_lab_envelope_follower_harness CMake
// target). Compile with -std=c++20. May allocate, loop, printf.

#include "primitives/dynamics/envelope-follower.h"

#include <cmath>
#include <cstdio>
#include <initializer_list>

// ---------------------------------------------------------------------------
// Constants
// ---------------------------------------------------------------------------

namespace {

constexpr float kSampleRate = 48000.0f;
constexpr float kPi         = 3.14159265358979323846f;

// One-pole step-response crossings: 1 - 1/e (attack, rising to a step) and
// 1/e (release, falling from a step), used to locate the sample at which a
// smoothed envelope reaches its nominal time constant.
constexpr float kOneMinusInvE = 0.63212055f;
constexpr float kInvE         = 0.36787944f;

} // namespace

// ---------------------------------------------------------------------------
// 1. Attack/release timing (branching, peak)
// ---------------------------------------------------------------------------

namespace {

void runAttackReleaseTiming() {
    acfx::EnvelopeFollower follower;
    follower.init(kSampleRate);
    follower.setMode(acfx::DetectMode::peak);
    follower.setBallistics(acfx::Ballistics::branching);
    follower.setDomain(acfx::DetectDomain::linear);

    constexpr float kAttackSec  = 0.010f;
    constexpr float kReleaseSec = 0.100f;
    follower.setAttack(kAttackSec);
    follower.setRelease(kReleaseSec);

    // Rising phase: unit step to 1.0. 500 ms is comfortably past 5x the
    // 10 ms attack time constant, so the envelope is fully settled by the
    // end of the loop (crossing itself happens near sample 480).
    constexpr int kAttackPhaseSamples = 24000;
    int   attackCrossSample   = -1;
    float peakAtEndOfAttack   = 0.0f;
    for (int n = 0; n < kAttackPhaseSamples; ++n) {
        const float env = follower.process(1.0f);
        if (attackCrossSample < 0 && env >= kOneMinusInvE) {
            attackCrossSample = n;
        }
        peakAtEndOfAttack = env;
    }

    // Falling phase: step down to 0.0 from the settled peak. 500 ms is
    // comfortably past 5x the 100 ms release time constant.
    constexpr int kReleasePhaseSamples = 24000;
    int         releaseCrossSample = -1;
    const float releaseThreshold   = kInvE * peakAtEndOfAttack;
    for (int n = 0; n < kReleasePhaseSamples; ++n) {
        const float env = follower.process(0.0f);
        if (releaseCrossSample < 0 && env <= releaseThreshold) {
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

    std::printf("=== 1. Attack/release timing (branching, peak) ===\n");
    std::printf("  configured attack:  %.2f ms\n", static_cast<double>(kAttackSec * 1000.0f));
    std::printf("  measured attack:    %.4f ms (sample %d, env=%.4f crosses %.4f = 1-1/e)\n",
                static_cast<double>(attackMs), attackCrossSample,
                static_cast<double>(peakAtEndOfAttack), static_cast<double>(kOneMinusInvE));
    std::printf("  configured release: %.2f ms\n", static_cast<double>(kReleaseSec * 1000.0f));
    std::printf("  measured release:   %.4f ms (sample %d, crosses %.4f = 1/e of peak %.4f)\n",
                static_cast<double>(releaseMs), releaseCrossSample,
                static_cast<double>(releaseThreshold), static_cast<double>(peakAtEndOfAttack));
    std::printf("\n");
}

} // namespace

// ---------------------------------------------------------------------------
// 2. Branching vs decoupled ballistics (transient-then-decay)
// ---------------------------------------------------------------------------

namespace {

void runTopologyComparison() {
    acfx::EnvelopeFollower branching;
    acfx::EnvelopeFollower decoupled;
    branching.init(kSampleRate);
    decoupled.init(kSampleRate);

    for (acfx::EnvelopeFollower* f : {&branching, &decoupled}) {
        f->setMode(acfx::DetectMode::peak);
        f->setDomain(acfx::DetectDomain::linear);
        f->setAttack(0.005f);
        f->setRelease(0.050f);
    }
    branching.setBallistics(acfx::Ballistics::branching);
    decoupled.setBallistics(acfx::Ballistics::decoupled);

    std::printf("=== 2. Branching vs decoupled ballistics (transient-then-decay) ===\n");
    std::printf("  peak detect, attack=5ms release=50ms; input decays with tau=3ms (faster than release)\n");
    std::printf("  %8s %8s %10s %16s %16s\n", "sample", "t(ms)", "input", "branching env", "decoupled env");

    constexpr int   kNumSamples      = 1000;
    constexpr float kDecayTauSamples = 144.0f; // 3 ms @ 48 kHz
    constexpr int   kPrintStride     = 50;
    for (int n = 0; n < kNumSamples; ++n) {
        const float x    = std::exp(-static_cast<float>(n) / kDecayTauSamples);
        const float envB = branching.process(x);
        const float envD = decoupled.process(x);
        if (n % kPrintStride == 0) {
            std::printf("  %8d %8.2f %10.4f %16.4f %16.4f\n", n,
                        static_cast<double>(static_cast<float>(n) / kSampleRate * 1000.0f),
                        static_cast<double>(x), static_cast<double>(envB), static_cast<double>(envD));
        }
    }
    std::printf("\n");
}

} // namespace

// ---------------------------------------------------------------------------
// 3. RMS vs peak detection (steady sine, amplitude 1.0)
// ---------------------------------------------------------------------------

namespace {

void runRmsVsPeak() {
    acfx::EnvelopeFollower peakFollower;
    acfx::EnvelopeFollower rmsFollower;
    peakFollower.init(kSampleRate);
    rmsFollower.init(kSampleRate);

    peakFollower.setMode(acfx::DetectMode::peak);
    peakFollower.setBallistics(acfx::Ballistics::branching);
    peakFollower.setDomain(acfx::DetectDomain::linear);
    // Attack tau (0.1 ms = 4.8 samples) must be well under a quarter-period
    // of the 1 kHz sine (12 samples) so the envelope actually reaches the
    // peak on every cycle; release tau (200 ms) must be well over the period
    // (48 samples) so it barely decays before the next peak arrives.
    peakFollower.setAttack(0.0001f);
    peakFollower.setRelease(0.200f);

    rmsFollower.setMode(acfx::DetectMode::rms);
    rmsFollower.setBallistics(acfx::Ballistics::branching);
    rmsFollower.setDomain(acfx::DetectDomain::linear);
    rmsFollower.setRmsWindow(0.020f);
    rmsFollower.setAttack(0.005f);
    rmsFollower.setRelease(0.005f);

    constexpr float kFreqHz       = 1000.0f;
    constexpr int   kNumSamples   = 9600; // 200 ms: well past every time constant above
    constexpr int   kAverageTail  = 480;  // average the last 10 ms to cancel residual ripple

    float peakSum = 0.0f;
    float rmsSum  = 0.0f;
    for (int n = 0; n < kNumSamples; ++n) {
        const float t    = static_cast<float>(n) / kSampleRate;
        const float x    = std::sin(2.0f * kPi * kFreqHz * t);
        const float envP = peakFollower.process(x);
        const float envR = rmsFollower.process(x);
        if (n >= kNumSamples - kAverageTail) {
            peakSum += envP;
            rmsSum  += envR;
        }
    }
    const float peakAvg = peakSum / static_cast<float>(kAverageTail);
    const float rmsAvg  = rmsSum / static_cast<float>(kAverageTail);

    std::printf("=== 3. RMS vs peak detection (steady 1 kHz sine, amplitude 1.0) ===\n");
    std::printf("  settled peak envelope: %.4f (expected ~1.0000)\n", static_cast<double>(peakAvg));
    std::printf("  settled RMS envelope:  %.4f (expected ~0.7071 = 1/sqrt(2))\n", static_cast<double>(rmsAvg));
    std::printf("\n");
}

} // namespace

// ---------------------------------------------------------------------------
// 4. Peak-hold dwell
// ---------------------------------------------------------------------------

namespace {

void runPeakHoldDwell() {
    acfx::EnvelopeFollower follower;
    follower.init(kSampleRate);
    follower.setMode(acfx::DetectMode::peakHold);
    follower.setDomain(acfx::DetectDomain::linear);
    // Leave attack/release at their 0 s default (passthrough coefficient) so
    // the measured dwell reflects the detect()-stage hold logic alone,
    // undistorted by any ballistics smoothing on top of it.
    constexpr float kHoldSec = 0.050f;
    follower.setHold(kHoldSec);

    constexpr int kNumSamples = 6000; // 125 ms: comfortably longer than the 50 ms hold
    int   dwellSamples = -1;
    float peakValue    = 0.0f;
    for (int n = 0; n < kNumSamples; ++n) {
        const float x   = (n == 0) ? 1.0f : 0.0f;
        const float env = follower.process(x);
        if (n == 0) {
            peakValue = env;
        } else if (dwellSamples < 0 && env < 0.999f * peakValue) {
            dwellSamples = n;
        }
    }

    const int configuredHoldSamples = static_cast<int>(std::lround(kHoldSec * kSampleRate));
    std::printf("=== 4. Peak-hold dwell ===\n");
    std::printf("  configured hold: %.1f ms (%d samples)\n",
                static_cast<double>(kHoldSec * 1000.0f), configuredHoldSamples);
    std::printf("  measured dwell:  %.4f ms (%d samples) latched near peak %.4f before release\n",
                static_cast<double>(static_cast<float>(dwellSamples) / kSampleRate * 1000.0f),
                dwellSamples, static_cast<double>(peakValue));
    std::printf("\n");
}

} // namespace

// ---------------------------------------------------------------------------
// 5. dB domain
// ---------------------------------------------------------------------------

namespace {

void runDbDomain() {
    acfx::EnvelopeFollower follower;
    follower.init(kSampleRate);
    follower.setMode(acfx::DetectMode::peak);
    follower.setBallistics(acfx::Ballistics::branching);
    follower.setDomain(acfx::DetectDomain::decibel);
    follower.setAttack(0.001f);
    follower.setRelease(0.005f);

    constexpr int kSettleSamples = 4800; // 100 ms: >> 5x either time constant above

    float settledDb = 0.0f;
    for (int n = 0; n < kSettleSamples; ++n) {
        settledDb = follower.process(0.5f);
    }

    float floorDb = 0.0f;
    for (int n = 0; n < kSettleSamples; ++n) {
        floorDb = follower.process(0.0f);
    }

    std::printf("=== 5. dB domain (peak, branching) ===\n");
    std::printf("  settled dB @ input 0.5:  %.4f dB (expected ~-6.02 dB = 20*log10(0.5))\n",
                static_cast<double>(settledDb));
    std::printf("  floor dB @ silence:      %.4f dB (expected -120.00 dB floor)\n",
                static_cast<double>(floorDb));
    std::printf("\n");
}

} // namespace

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------

int main() {
    std::printf("=== acfx EnvelopeFollower lab harness (fs = %.0f Hz) ===\n\n",
                static_cast<double>(kSampleRate));

    runAttackReleaseTiming();
    runTopologyComparison();
    runRmsVsPeak();
    runPeakHoldDwell();
    runDbDomain();

    std::printf("=== Done ===\n");
    return 0;
}
