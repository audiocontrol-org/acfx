#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>

#include "primitives/dynamics/envelope-follower.h"

// Low-sample-rate coefficient characterization (research.md Decision 7), split
// out of envelope-follower-ballistics-test.cpp to keep both files under the
// FR-022 500-line file-size budget. This is a pure move — no test behavior
// changes; see the ballistics file's US1/US4 suites for the timing-tolerance
// and reference-model constants this file mirrors locally.

using acfx::Ballistics;
using acfx::DetectMode;
using acfx::EnvelopeFollower;

namespace {

// House named tolerance for the timing assertions, mirrored from the
// ballistics suite (see its kTimingTolFrac for the full rationale): +/-10% of
// tau comfortably covers the <=1-sample off-by-one and discrete-crossing
// quantization while staying far tighter than a passthrough-stub failure mode.
inline constexpr double kTimingTolFrac = 0.10;

// One-pole step-response crossing fraction of a unit step: 1 - 1/e. Mirrored
// from the ballistics suite's kAttackCrossFrac.
inline constexpr double kAttackCrossFrac = 0.63212055882855767;

} // namespace

// T034 — characterize low-sample-rate coefficient accuracy (research.md Decision 7).
//
// Decision 7 was the ONE deferred open question: does the coefficient map
//     a = exp(-1 / (tau * fs))
// (coeffFor() in the primitive) need a higher-order correction for very short
// time constants at MCU sample rates (fs <= 32 kHz), where tau*fs approaches a
// single sample and the one-pole's effective time could deviate from the
// nominal 1 - 1/e?
//
// The cheap first-order map  a ~= 1 - 1/(tau*fs)  DOES diverge in exactly that
// regime (it can even go negative once tau*fs < 1). The primitive deliberately
// does NOT use it: exp(-1/(tau*fs)) is the mathematically exact one-pole pole
// for ANY fs, so its timing accuracy does not degrade at low fs — only the
// discrete-sample QUANTIZATION of a crossing coarsens (a fixed sample is a
// larger fraction of a short tau), and once tau*fs < 1 the time constant is
// simply sub-sample and cannot be resolved at all (that is physics, not a
// coefficient error).
//
// These cases DEMONSTRATE that:
//   1. Stability/finiteness holds across a wide tau range (incl. very short,
//      down to ~1 sample and sub-sample) at fs = 8 kHz and 32 kHz — the FR-018
//      guards guarantee this and it MUST pass.
//   2. A moderate 5 ms attack still hits the 63.2% crossing within the file's
//      existing +/-10%-of-tau timing tolerance at both low sample rates — the
//      exact exp() keeps timing accurate, so NO correction is warranted.
//   3. An extreme sub-sample tau (tau*fs < 1) degrades GRACEFULLY: the output
//      stays finite, rises monotonically on a step, and still converges toward
//      the input — it just cannot resolve a sub-sample time constant (asserted
//      as sane/stable behavior, NOT an exact crossing time).
//
// CONCLUSION (Decision 7 closed): exact exp() needs no low-fs correction; the
// header is left unmodified. See the matching note near coeffFor().

namespace {

// Configure a fresh peak/branching follower at an ARBITRARY sample rate (the
// ballistics suite's makeFollower() is pinned to 48 kHz; low-fs
// characterization needs a parameterized fs), freshly reset.
EnvelopeFollower makeFollowerAtRate(double fs, float attackSeconds,
                                    float releaseSeconds) {
    EnvelopeFollower ef;
    ef.init(static_cast<float>(fs));
    ef.setMode(DetectMode::peak);
    ef.setBallistics(Ballistics::branching);
    ef.setAttack(attackSeconds);
    ef.setRelease(releaseSeconds);
    ef.reset();
    return ef;
}

// Drive a unit step for `stepSamples`, then x = 0 for `decaySamples`, asserting
// every emitted envelope sample is finite and bounded to [0, 1] (unit input can
// never drive a valid one-pole outside that band). Bounded+finite output over
// the whole step+decay is the observable proxy for "coefficient stayed in
// [0,1) with no NaN/Inf" — coeffFor()'s result is private, but a coefficient
// that escaped [0,1) or went NaN would immediately break this band.
void checkFiniteBoundedStepDecay(double fs, float attackSeconds,
                                 float releaseSeconds, int stepSamples,
                                 int decaySamples) {
    EnvelopeFollower ef = makeFollowerAtRate(fs, attackSeconds, releaseSeconds);
    constexpr double kBandEpsilon = 1.0e-5; // absorb float rounding at the 1.0 cap
    for (int n = 0; n < stepSamples; ++n) {
        const float env = ef.process(1.0f);
        REQUIRE(std::isfinite(env));
        CHECK(static_cast<double>(env) >= -kBandEpsilon);
        CHECK(static_cast<double>(env) <= 1.0 + kBandEpsilon);
    }
    for (int n = 0; n < decaySamples; ++n) {
        const float env = ef.process(0.0f);
        REQUIRE(std::isfinite(env));
        CHECK(static_cast<double>(env) >= -kBandEpsilon);
        CHECK(static_cast<double>(env) <= 1.0 + kBandEpsilon);
    }
}

} // namespace

TEST_CASE("coefficient accuracy holds at low sample rates") {
    // MCU-class sample rates where the deferred concern lives.
    const double lowRates[] = {8000.0, 32000.0};

    SUBCASE("stability and finiteness across short time constants") {
        // Time constants spanning the ordinary short range down INTO the
        // sub-sample regime. At fs = 8 kHz one sample = 0.125 ms, so 0.1 ms is
        // already tau*fs = 0.8 (< 1 sample) — the exact regime Decision 7 asked
        // about. Every one of these must stay finite and bounded (FR-018).
        const float taus[] = {
            0.0001f, // 0.1 ms  -> tau*fs = 0.8 @ 8 kHz, 3.2 @ 32 kHz
            0.0005f, // 0.5 ms
            0.001f,  // 1 ms
            0.005f,  // 5 ms
            0.020f,  // 20 ms
        };
        for (const double fs : lowRates) {
            // ~1-sample time constant for this fs (tau*fs == 1 exactly).
            const float oneSampleTau = static_cast<float>(1.0 / fs);
            for (const float atk : taus) {
                for (const float rel : taus) {
                    // 5 tau of step + 5 tau of decay (bounded by fs), min 64.
                    const int stepN =
                        std::max(64, static_cast<int>(5.0 * atk * fs));
                    const int decayN =
                        std::max(64, static_cast<int>(5.0 * rel * fs));
                    checkFiniteBoundedStepDecay(fs, atk, rel, stepN, decayN);
                }
            }
            // Explicit ~1-sample-period time constant (attack and release).
            checkFiniteBoundedStepDecay(fs, oneSampleTau, oneSampleTau, 256, 256);
        }
    }

    SUBCASE("moderate 5 ms attack keeps 63.2% timing within tolerance") {
        // The exact exp() keeps the crossing on target even at low fs. At
        // fs = 8 kHz, tau = 5 ms is tau*fs = 40 samples; the +/-10%-of-tau house
        // tolerance (= 4 samples here) comfortably covers the <=1-sample
        // initial-condition off-by-one and the discrete-crossing quantization.
        constexpr float kAttack       = 0.005f; // 5 ms
        constexpr float kLongRelease  = 1.0f;   // idle release path
        constexpr double kTauA        = 0.005;
        for (const double fs : lowRates) {
            EnvelopeFollower ef = makeFollowerAtRate(fs, kAttack, kLongRelease);
            const int maxSamples = static_cast<int>(5.0 * kTauA * fs) + 16;
            int crossing = -1;
            for (int n = 0; n < maxSamples; ++n) {
                const float env = ef.process(1.0f);
                if (static_cast<double>(env) >= kAttackCrossFrac) {
                    crossing = n;
                    break;
                }
            }
            REQUIRE(crossing >= 0);
            const double measuredSeconds = static_cast<double>(crossing) / fs;
            const double tolerance = kTimingTolFrac * kTauA;
            CHECK(std::fabs(measuredSeconds - kTauA) <= tolerance);
        }
    }

    SUBCASE("sub-sample time constant degrades gracefully") {
        // tau*fs < 1: the time constant is shorter than a sample period, so it
        // is physically unresolvable at this fs — we assert STABLE, SANE
        // behavior (finite, monotonic rise, convergence toward the input), NOT
        // an exact crossing time. This is the honest answer to Decision 7's
        // extreme: exact exp() still yields a valid coefficient in [0,1) that
        // one-poles sensibly; there is nothing to "correct".
        constexpr double fs = 8000.0;             // 1 sample = 0.125 ms
        constexpr float  kSubSampleTau = 0.00005f; // 0.05 ms -> tau*fs = 0.4
        EnvelopeFollower ef =
            makeFollowerAtRate(fs, kSubSampleTau, kSubSampleTau);

        float previous = 0.0f;
        float last     = 0.0f;
        constexpr double kMonotoneEpsilon = 1.0e-6;
        for (int n = 0; n < 256; ++n) {
            const float env = ef.process(1.0f);
            REQUIRE(std::isfinite(env));
            // Monotonic non-decreasing on a rising step (no overshoot/ring).
            CHECK(static_cast<double>(env) >=
                  static_cast<double>(previous) - kMonotoneEpsilon);
            previous = env;
            last = env;
        }
        // Still converges essentially to the unit input despite being sub-sample.
        CHECK(static_cast<double>(last) > 0.99);
    }
}
