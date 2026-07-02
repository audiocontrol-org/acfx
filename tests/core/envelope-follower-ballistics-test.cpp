#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <vector>

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

// T018 — US4 DECOUPLED + SMOOTH-DECOUPLED ballistics (TDD, written to spec).
//
// The decoupled topology (spec.md US4) tracks the level with a release-only
// one-pole peak state y1, then applies attack smoothing with a second one-pole
// env state. Peak mode, linear domain, per input sample with level = |x|:
//
//   Base decoupled  (setSmooth(false)):
//       y1  = max(level, aRel * y1);
//       env = aAtk * env + (1 - aAtk) * y1;      // env is the returned value
//
//   Smooth decoupled (setSmooth(true)):
//       y1  = max(level, aRel * y1 + (1 - aRel) * level);
//       env = aAtk * env + (1 - aAtk) * y1;
//
//   aAtk = exp(-1 / (attackSeconds  * fs)),
//   aRel = exp(-1 / (releaseSeconds * fs)).
//
// EXPECTED-RED: EnvelopeFollower::applySmoothing() currently passes the
// decoupled branch straight through (no y1/env stages), so every case below
// FAILS now and goes green once T019 (base decoupled) and T020 (smooth
// variant) land the recurrences above. These tests are written to the SPEC's
// intended behavior — the reference model below IS the authoritative contract.

namespace {

// Sample rate in float, matching EnvelopeFollower's internal fs (init() is
// called with static_cast<float>(kSampleRate)). Using the identical type and
// expression as the primitive keeps the reference coefficients bit-for-bit
// comparable, so the reference-model checks below can be tight (1e-5).
inline constexpr float kSampleRateF = static_cast<float>(kSampleRate);

// Tolerance for the reference-model equality checks. The reference computes the
// same float recurrence with the same float coefficients as the primitive, so
// the two should agree to within accumulated float rounding over the driven
// sequence; 1e-5 absolute is comfortably above that yet far below the passthrough
// stub's divergence, so the red tests still fail loudly.
inline constexpr double kRefTolAbs = 1.0e-5;

// Time-constant -> one-pole coefficient, mirroring EnvelopeFollower::coeffFor()
// EXACTLY (same float exp expression, same clamp, same guards) so the reference
// model and the primitive share identical coefficients.
float refCoeff(float seconds) noexcept {
    if (seconds <= 0.0f) {
        return 0.0f;
    }
    const float a = std::exp(-1.0f / (seconds * kSampleRateF));
    if (!std::isfinite(a) || a < 0.0f) {
        return 0.0f;
    }
    constexpr float kMaxCoeff = 0.99999994f; // nextafter(1.0f, 0.0f)
    return (a > kMaxCoeff) ? kMaxCoeff : a;
}

// Authoritative decoupled reference model. Holds the two states (y1, env), both
// starting at 0, and advances them per sample with the base or smooth
// recurrence. This is the contract T019/T020 implement; the primitive's
// per-sample output must equal process() here.
struct DecoupledReference {
    float aAtk = 0.0f;
    float aRel = 0.0f;
    bool  smooth = false;
    float y1  = 0.0f;
    float env = 0.0f;

    float process(float x) noexcept {
        const float level = std::fabs(x);
        if (smooth) {
            y1 = std::fmax(level, aRel * y1 + (1.0f - aRel) * level);
        } else {
            y1 = std::fmax(level, aRel * y1);
        }
        env = aAtk * env + (1.0f - aAtk) * y1;
        return env;
    }
};

// Configure a fresh peak/decoupled follower at kSampleRate with the given
// attack/release seconds and smooth flag, freshly reset.
EnvelopeFollower makeDecoupledFollower(float attackSeconds, float releaseSeconds,
                                       bool smooth) {
    EnvelopeFollower ef;
    ef.init(kSampleRateF);
    ef.setMode(DetectMode::peak);
    ef.setBallistics(Ballistics::decoupled);
    ef.setSmooth(smooth);
    ef.setAttack(attackSeconds);
    ef.setRelease(releaseSeconds);
    ef.reset();
    return ef;
}

// A single input sequence that exercises BOTH attack and release tracking:
// a linear ramp up to 1.0, a hold at 1.0, a transient dip, a partial recovery,
// then a decay back to 0. Reused by the two reference-model cases so the base
// and smooth variants see identical, revealing input.
std::vector<float> makeExerciseSignal() {
    std::vector<float> sig;
    const int kRamp = 480;    // 10 ms ramp 0 -> 1 (reveals attack tracking)
    const int kHold = 480;    // 10 ms hold at 1.0
    const int kDip  = 240;    // 5 ms transient dip (reveals release tracking)
    const int kBack = 240;    // 5 ms partial recovery
    const int kDecay = 960;   // 20 ms decay back to 0
    for (int n = 0; n < kRamp; ++n) {
        sig.push_back(static_cast<float>(n) / static_cast<float>(kRamp - 1));
    }
    for (int n = 0; n < kHold; ++n) {
        sig.push_back(1.0f);
    }
    for (int n = 0; n < kDip; ++n) {
        sig.push_back(0.2f);
    }
    for (int n = 0; n < kBack; ++n) {
        sig.push_back(0.8f);
    }
    for (int n = 0; n < kDecay; ++n) {
        sig.push_back(0.8f * (1.0f - static_cast<float>(n) /
                                          static_cast<float>(kDecay - 1)));
    }
    return sig;
}

} // namespace

TEST_CASE("decoupled peak matches the reference recurrence (base)") {
    // Pin the primitive to the base-decoupled recurrence by driving both it and
    // the local reference model with the same attack/release-exercising signal.
    constexpr float kAttack  = 0.010f; // 10 ms
    constexpr float kRelease = 0.100f; // 100 ms

    EnvelopeFollower ef = makeDecoupledFollower(kAttack, kRelease, /*smooth=*/false);
    DecoupledReference ref{refCoeff(kAttack), refCoeff(kRelease), /*smooth=*/false,
                           0.0f, 0.0f};

    const std::vector<float> sig = makeExerciseSignal();
    for (const float x : sig) {
        const double got  = static_cast<double>(ef.process(x));
        const double want = static_cast<double>(ref.process(x));
        CHECK(std::fabs(got - want) <= kRefTolAbs);
    }
}

TEST_CASE("smooth decoupled peak matches the reference recurrence") {
    // Same as above but for the SMOOTH variant (setSmooth(true)); the reference
    // uses the smooth y1 recurrence.
    constexpr float kAttack  = 0.010f; // 10 ms
    constexpr float kRelease = 0.100f; // 100 ms

    EnvelopeFollower ef = makeDecoupledFollower(kAttack, kRelease, /*smooth=*/true);
    DecoupledReference ref{refCoeff(kAttack), refCoeff(kRelease), /*smooth=*/true,
                           0.0f, 0.0f};

    const std::vector<float> sig = makeExerciseSignal();
    for (const float x : sig) {
        const double got  = static_cast<double>(ef.process(x));
        const double want = static_cast<double>(ref.process(x));
        CHECK(std::fabs(got - want) <= kRefTolAbs);
    }
}

TEST_CASE("decoupled attack reaches ~63% in the attack time") {
    // Unit step from a zeroed state: y1 = max(1, aRel*y1) jumps to 1 at n = 0
    // and stays there, so env one-poles from 0 toward 1 at aAtk and crosses
    // 1 - 1/e in ~attackSeconds. Release is deliberately long (1 s) and idle.
    constexpr double kTauA = 0.010; // 10 ms
    EnvelopeFollower ef = makeDecoupledFollower(static_cast<float>(kTauA), 1.0f,
                                                /*smooth=*/false);

    const int maxSamples = static_cast<int>(5.0 * kTauA * kSampleRate) + 16;
    int crossing = -1;
    for (int n = 0; n < maxSamples; ++n) {
        const float env = ef.process(1.0f);
        if (static_cast<double>(env) >= kAttackCrossFrac) {
            crossing = n;
            break;
        }
    }
    checkTiming(crossing, kTauA);
}

TEST_CASE("decoupled release with fast attack decays at the release rate") {
    // Very fast attack (0.1 ms) so env sits on top of y1; settle on a steady 1.0
    // input, then feed 0. y1 = max(0, aRel*y1) = aRel^n is a pure exponential at
    // the release rate, env tracks it closely, so the envelope decays through
    // 1/e of its settled value in ~releaseSeconds.
    constexpr float  kFastAttack = 0.0001f; // 0.1 ms
    constexpr double kTauR       = 0.100;   // 100 ms
    constexpr int    kSettle     = 4096;    // >> attack; env settles to ~1.0

    EnvelopeFollower ef = makeDecoupledFollower(kFastAttack,
                                                static_cast<float>(kTauR),
                                                /*smooth=*/false);

    float settled = 0.0f;
    for (int n = 0; n < kSettle; ++n) {
        settled = ef.process(1.0f);
    }
    const double startEnv = static_cast<double>(settled);
    CHECK(startEnv > 0.9); // must have risen near unity for the threshold to mean anything
    const double threshold = kReleaseCrossFrac * startEnv;

    const int maxSamples = static_cast<int>(5.0 * kTauR * kSampleRate) + 16;
    int crossing = -1;
    for (int n = 0; n < maxSamples; ++n) {
        const float env = ef.process(0.0f);
        if (static_cast<double>(env) <= threshold) {
            crossing = n;
            break;
        }
    }
    checkTiming(crossing, kTauR);
}

TEST_CASE("decoupled peak has no branching re-attack artifact") {
    // Rationale: the decoupled topology tracks the peak with a release-only
    // one-pole (y1) and only then applies attack smoothing (env). Once the
    // envelope has peaked, a step down to a sustained lower level lets y1 decay
    // monotonically toward that level and env follows it down without re-rising
    // — it cannot exhibit the coefficient-flip re-attack glitch a single
    // branching one-pole can produce mid-decay.
    //
    // We assert this qualitatively and robustly: locate the envelope's peak
    // sample, then require the envelope to be monotonically non-increasing for
    // the entire remainder of the sustained-medium phase (no spurious re-rise).
    constexpr float kAttack  = 0.005f; // 5 ms
    constexpr float kRelease = 0.100f; // 100 ms
    EnvelopeFollower ef = makeDecoupledFollower(kAttack, kRelease, /*smooth=*/false);

    std::vector<float> env;

    // Brief loud transient at 1.0 (~3 attack time constants) so the envelope
    // rises high before the level drops.
    const int transientSamples = static_cast<int>(3.0 * 0.005 * kSampleRate);
    for (int n = 0; n < transientSamples; ++n) {
        env.push_back(ef.process(1.0f));
    }

    // Sustained MEDIUM level below the peak (~5 release time constants) — the
    // decay phase whose monotonicity we assert.
    constexpr float kMedium = 0.3f;
    const int sustainSamples = static_cast<int>(5.0 * 0.100 * kSampleRate);
    for (int n = 0; n < sustainSamples; ++n) {
        env.push_back(ef.process(kMedium));
    }

    const auto peakIt = std::max_element(env.begin(), env.end());
    const std::size_t peakIdx =
        static_cast<std::size_t>(std::distance(env.begin(), peakIt));
    constexpr double kMonotoneEpsilon = 1.0e-6; // absorb float rounding only
    for (std::size_t n = peakIdx + 1; n < env.size(); ++n) {
        CHECK(static_cast<double>(env[n]) <=
              static_cast<double>(env[n - 1]) + kMonotoneEpsilon);
    }
}
