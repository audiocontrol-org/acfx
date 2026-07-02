#include <doctest/doctest.h>

#include <cmath>

#include "primitives/dynamics/envelope-follower.h"

// T010 — US1 branching attack/release TIMING accuracy (TDD, written to spec).
//
// Asserts that acfx::EnvelopeFollower, in PEAK detection mode with BRANCHING
// ballistics, reaches the analytic one-pole crossings in the time the caller
// asked for. The contract (spec.md SC-001/SC-002, research.md Decision 1):
//
//   * A time constant tau (SECONDS) maps to a one-pole coefficient
//         a = exp(-1 / (tau * fs))
//     driving the update
//         y[n] = a * y[n-1] + (1 - a) * x[n].
//   * For a UNIT STEP from a zeroed state (y[-1] = 0) this gives the closed
//     form  y[n] = 1 - a^(n+1), so the envelope first crosses
//         1 - 1/e  (~= 0.63212)      at  n ~= tau * fs - 1  (i.e. t ~= tau).
//   * After settling at ~1.0 and switching the input to 0, the release path is
//         y[n] = a_rel^n * y0,
//     so the envelope decays THROUGH  1/e (~= 0.36788) of its starting value
//     at  n ~= tau_rel * fs  (i.e. t ~= tau_rel).
//
// EXPECTED-RED: the branching one-pole smoother is NOT yet implemented —
// EnvelopeFollower::process() currently passes the detected level straight
// through applySmoothing() (a passthrough stub). Under that stub a unit step
// makes the envelope jump to 1.0 at n = 0 (crossing time ~0 s, not tau) and a
// step to 0 makes it drop to 0 at n = 0, so BOTH timing suites below FAIL now
// and will pass once T012/T013 land the branching smoother. These tests are
// written to the SPEC's intended behavior, not the current stub — that is the
// intended TDD red.

using acfx::Ballistics;
using acfx::DetectMode;
using acfx::EnvelopeFollower;

namespace {

// House named tolerance for the timing assertions.
//
// The analytic crossing sits at exactly tau*fs samples up to (a) a <=1-sample
// off-by-one from the y[-1]=0 initial condition and (b) the sub-sample
// quantization of a discrete crossing. +/-10% of tau comfortably covers both
// for every time constant exercised here (the tightest, tau_a = 5 ms at
// 48 kHz, is 240 samples, so 10% = 24 samples >> the 1-sample ambiguity) while
// staying far tighter than the stub's failure mode (a crossing at t ~= 0),
// so the tolerance still makes the red tests fail loudly.
inline constexpr double kTimingTolFrac = 0.10;

inline constexpr double kSampleRate = 48000.0;

// One-pole step-response crossing fraction of a unit step: 1 - 1/e.
inline constexpr double kAttackCrossFrac = 0.63212055882855767;
// One-pole decay crossing fraction of the starting value: 1/e.
inline constexpr double kReleaseCrossFrac = 0.36787944117144233;

// Configure a fresh peak/branching follower at kSampleRate with the given
// attack/release seconds, freshly reset. Kept local so both suites stay DRY.
EnvelopeFollower makeFollower(float attackSeconds, float releaseSeconds) {
    EnvelopeFollower ef;
    ef.init(static_cast<float>(kSampleRate));
    ef.setMode(DetectMode::peak);
    ef.setBallistics(Ballistics::branching);
    ef.setAttack(attackSeconds);
    ef.setRelease(releaseSeconds);
    ef.reset();
    return ef;
}

// Drive a unit step (x = 1.0 every sample) from the zeroed state and return the
// first sample index n where the envelope crosses kAttackCrossFrac, or -1 if it
// never does within maxSamples. maxSamples bounds the search generously.
int attackCrossingSample(float attackSeconds, float releaseSeconds, int maxSamples) {
    EnvelopeFollower ef = makeFollower(attackSeconds, releaseSeconds);
    for (int n = 0; n < maxSamples; ++n) {
        const float env = ef.process(1.0f);
        if (static_cast<double>(env) >= kAttackCrossFrac) {
            return n;
        }
    }
    return -1;
}

// Drive the envelope up with a unit step for `settleSamples` (long enough to
// reach ~1.0 given the fast attack), snapshot the settled level, then feed
// x = 0.0 and return the first index n (counted from the first zero sample)
// where the envelope decays through kReleaseCrossFrac * startEnv, or -1.
int releaseCrossingSample(float attackSeconds, float releaseSeconds,
                          int settleSamples, int maxSamples, double& startEnvOut) {
    EnvelopeFollower ef = makeFollower(attackSeconds, releaseSeconds);

    float settled = 0.0f;
    for (int n = 0; n < settleSamples; ++n) {
        settled = ef.process(1.0f);
    }
    startEnvOut = static_cast<double>(settled);
    const double threshold = kReleaseCrossFrac * startEnvOut;

    for (int n = 0; n < maxSamples; ++n) {
        const float env = ef.process(0.0f);
        if (static_cast<double>(env) <= threshold) {
            return n;
        }
    }
    return -1;
}

// Assert a measured crossing sample index corresponds to the target time
// constant within kTimingTolFrac.
void checkTiming(int crossingSample, double tauSeconds) {
    REQUIRE(crossingSample >= 0); // a crossing must have been found at all
    const double measuredSeconds = static_cast<double>(crossingSample) / kSampleRate;
    const double tolerance = kTimingTolFrac * tauSeconds;
    CHECK(std::fabs(measuredSeconds - tauSeconds) <= tolerance);
}

} // namespace

TEST_CASE("branching peak attack reaches 63.2% in the configured attack time") {
    // Release deliberately long (1 s) so the rising path is governed purely by
    // the attack coefficient and the release path can never interfere.
    constexpr float kLongReleaseSeconds = 1.0f;

    SUBCASE("attack = 5 ms") {
        constexpr double kTauA = 0.005; // 5 ms
        const int maxSamples = static_cast<int>(5.0 * kTauA * kSampleRate) + 16;
        const int n = attackCrossingSample(static_cast<float>(kTauA),
                                           kLongReleaseSeconds, maxSamples);
        checkTiming(n, kTauA);
    }

    SUBCASE("attack = 20 ms") {
        constexpr double kTauA = 0.020; // 20 ms
        const int maxSamples = static_cast<int>(5.0 * kTauA * kSampleRate) + 16;
        const int n = attackCrossingSample(static_cast<float>(kTauA),
                                           kLongReleaseSeconds, maxSamples);
        checkTiming(n, kTauA);
    }
}

TEST_CASE("branching peak release decays through 1/e in the configured release time") {
    // Attack deliberately fast (0.1 ms) so the envelope reaches ~1.0 well
    // before the release phase and the decay is governed purely by the
    // release coefficient.
    constexpr float kFastAttackSeconds = 0.0001f; // 0.1 ms
    constexpr int kSettleSamples = 4096;          // >> attack; env settles to ~1.0

    SUBCASE("release = 100 ms") {
        constexpr double kTauR = 0.100; // 100 ms
        const int maxSamples = static_cast<int>(5.0 * kTauR * kSampleRate) + 16;
        double startEnv = 0.0;
        const int n = releaseCrossingSample(kFastAttackSeconds,
                                            static_cast<float>(kTauR),
                                            kSettleSamples, maxSamples, startEnv);
        // The envelope must actually have risen close to unity first, else the
        // 1/e-of-start threshold would be meaningless.
        CHECK(startEnv > 0.9);
        checkTiming(n, kTauR);
    }

    SUBCASE("release = 200 ms") {
        constexpr double kTauR = 0.200; // 200 ms
        const int maxSamples = static_cast<int>(5.0 * kTauR * kSampleRate) + 16;
        double startEnv = 0.0;
        const int n = releaseCrossingSample(kFastAttackSeconds,
                                            static_cast<float>(kTauR),
                                            kSettleSamples, maxSamples, startEnv);
        CHECK(startEnv > 0.9);
        checkTiming(n, kTauR);
    }
}

TEST_CASE("branching peak unit-step response is monotonically non-decreasing") {
    // A one-pole low-pass step response never overshoots, so sample-to-sample
    // the envelope must never decrease. Uses a mid attack (10 ms) and a long
    // release; the step is purely rising so only the attack path is exercised.
    EnvelopeFollower ef = makeFollower(0.010f, 1.0f);

    float previous = 0.0f;
    constexpr double kMonotoneEpsilon = 1.0e-6; // absorb float rounding only
    // ~4 attack time constants: long enough to be well into the plateau.
    const int samples = static_cast<int>(4.0 * 0.010 * kSampleRate);
    for (int n = 0; n < samples; ++n) {
        const float env = ef.process(1.0f);
        CHECK(static_cast<double>(env) >= static_cast<double>(previous) - kMonotoneEpsilon);
        previous = env;
    }
}
