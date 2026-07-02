#include <doctest/doctest.h>

#include <cmath>

#include "primitives/dynamics/envelope-follower.h"

// T025 — US5 DECIBEL detection-domain tests.
//
// Pins acfx::EnvelopeFollower's DECIBEL detection domain to its authoritative
// contract. In the dB domain the detected level is clamped to a -120 dBFS floor
// and converted to dB *before* the ballistics smoother, so process() returns a
// smoothed dB value.
//
//   applyDomain(level):
//       level <= 1e-6f  (== 10^(-120/20))  -> -120.0f   (never -inf / NaN)
//       else                               -> 20*log10(level)
//
// Envelope BASELINE: in dB domain the smoother state baselines at the -120 dBFS
// FLOOR (not 0 dB). The implementation sets env_/y1_ to -120 in dB domain when
// the state is cleared, so the USAGE CONTRACT is:
//
//       configure the domain FIRST, THEN call reset().
//
// Every helper/test below therefore does, in order:
//       init(48000); setMode(...); setDomain(DetectDomain::decibel);
//       setAttack(...); setRelease(...); reset();
// The reset() AFTER setDomain is what establishes the -120 dB baseline.
//
// These are required GREEN contract tests. They pin the -120 dBFS clamp, the
// 20*log10 conversion, the -120 dB baseline, and — TEST_CASEs 5-6 below — the
// DECOUPLED-topology dB release, which must decay toward the -120 dBFS floor
// (not toward 0 dB, which is unity/loud in the dB domain) for BOTH
// setSmooth(false) and setSmooth(true).
//
// TEST_CASE 4 is a CONTRAST/CONTROL in the LINEAR domain; it exists to
// discriminate dB-domain ballistics from linear-domain ballistics. See its own
// comment for the (subtle) math.

using acfx::Ballistics;
using acfx::DetectDomain;
using acfx::DetectMode;
using acfx::EnvelopeFollower;

namespace {

inline constexpr double kSampleRate  = 48000.0;
inline constexpr float  kSampleRateF = static_cast<float>(kSampleRate);

// The -120 dBFS floor and the linear level below which applyDomain() clamps
// to it (10^(-120/20) == 1e-6).
inline constexpr double kFloorDb    = -120.0;
inline constexpr double kFloorLevel = 1.0e-6;

// One-pole step-response crossing fraction: 1 - 1/e (~0.632). The spec phrases
// the attack crossing as "0.632 * span"; 1 - 1/e is that value to full float
// precision, and it is exactly the fraction a one-pole reaches after one time
// constant, so a crossing at this fraction lands at t ~= tau analytically.
inline constexpr double kCrossFrac = 0.63212055882855767;

// Absolute dB tolerance for the floor equality (T1) and the steady-level
// equality (T2). Both compare a settled dB value against an analytic 20*log10,
// so a few thousandths of a dB comfortably absorbs float rounding while staying
// far below the passthrough stub's divergence (whole dB or more) so the red
// tests fail loudly.
inline constexpr double kFloorTolDb  = 1.0e-3;
inline constexpr double kSteadyTolDb = 0.05;

// Timing tolerance as a fraction of the configured attack time (the spec allows
// +/-10-15%; 15% covers the <=1-sample off-by-one and sub-sample quantization
// of a discrete crossing while still failing the stub loudly).
inline constexpr double kTimingTolFrac = 0.15;

// The attack time used by the level-independence (T3) and contrast (T4) tests.
inline constexpr double kAttackTauSec = 0.010; // 10 ms
inline constexpr float  kLongRelease  = 1.0f;  // release idle throughout the rise

// Pre-settle / settle sample counts. 40000 samples (~0.83 s) is many attack
// time constants at every attack used here, so the dB envelope reaches its
// steady state to well within the tolerances above before we measure.
inline constexpr int kSettleSamples = 40000;

// Moderate attack/release used by the DECOUPLED x dB coverage below (T5/T6):
// distinct, easily-separated time constants, independent of kAttackTauSec
// (used by the branching level-independence tests above).
inline constexpr double kDecoupledAttackSec  = 0.005; // 5 ms
inline constexpr double kDecoupledReleaseSec = 0.050; // 50 ms

// Silence duration fed during the decoupled release-toward-floor test: 1 s is
// 20 release time constants at kDecoupledReleaseSec, far more than enough for
// a one-pole release to reach the -120 dBFS floor.
inline constexpr int kReleaseSamples = static_cast<int>(kSampleRate); // 1 s

// The release must land at/below this bound within kReleaseSamples -- 20 dB
// of margin above the -120 dB floor itself, well clear of the settled -6 dB
// starting point so the check is discriminating.
inline constexpr double kNearFloorDb = -100.0;

// Tiny epsilon for the release monotonic-non-increasing check: absorbs float
// rounding at the smoother's own precision without hiding a genuine increase
// (the pre-fix bug decayed toward 0 dB, an increase of two-plus orders of
// magnitude larger than this epsilon).
inline constexpr double kMonotonicEpsDb = 1.0e-4;

// dB conversion mirroring the applyDomain() contract, guarded against log10(0)
// with the SAME 1e-6 floor the primitive uses. Used both to compute analytic
// expectations and (in T4) to read the dB of a measured LINEAR envelope.
double dbOf(double linear) noexcept {
    return (linear <= kFloorLevel) ? kFloorDb : 20.0 * std::log10(linear);
}

// Configure a fresh peak / branching / decibel follower, honouring the usage
// contract (setDomain BEFORE reset so the smoother baselines at -120 dB).
EnvelopeFollower makeDbFollower(float attackSeconds, float releaseSeconds) {
    EnvelopeFollower ef;
    ef.init(kSampleRateF);
    ef.setMode(DetectMode::peak);
    ef.setDomain(DetectDomain::decibel);
    ef.setAttack(attackSeconds);
    ef.setRelease(releaseSeconds);
    ef.reset(); // AFTER setDomain -> -120 dB baseline
    return ef;
}

// Measure the number of samples for the dB envelope to cross kCrossFrac of the
// dB SPAN of a step Llow -> Lhigh, with the follower PRE-SETTLED at Llow first.
// Returns -1 if no crossing is found within the search window.
int dbAttackCrossingSamples(float Llow, float Lhigh, float attackSeconds) {
    EnvelopeFollower ef = makeDbFollower(attackSeconds, kLongRelease);

    for (int n = 0; n < kSettleSamples; ++n) {
        ef.process(Llow); // settle the dB envelope onto 20*log10(Llow)
    }

    const double lo     = dbOf(static_cast<double>(Llow));
    const double hi     = dbOf(static_cast<double>(Lhigh));
    const double target = lo + kCrossFrac * (hi - lo);

    // Search up to 8 attack time constants — a one-pole is >99.9% settled by 7
    // tau, so the ~1 tau crossing is always inside this window.
    const int maxSamples = static_cast<int>(8.0 * attackSeconds * kSampleRate) + 16;
    for (int n = 0; n < maxSamples; ++n) {
        const double env = static_cast<double>(ef.process(Lhigh));
        if (env >= target) {
            return n;
        }
    }
    return -1;
}

// LINEAR-domain analogue used by the contrast test: pre-settle at Llow in the
// LINEAR domain, step to Lhigh, and find the sample where the dB of the LINEAR
// envelope (computed here, guarded by the 1e-6 floor) crosses kCrossFrac of the
// dB span. Returns -1 if no crossing is found.
int linearDomainDbCrossingSamples(float Llow, float Lhigh, float attackSeconds) {
    EnvelopeFollower ef;
    ef.init(kSampleRateF);
    ef.setMode(DetectMode::peak);
    ef.setDomain(DetectDomain::linear);
    ef.setAttack(attackSeconds);
    ef.setRelease(kLongRelease);
    ef.reset();

    for (int n = 0; n < kSettleSamples; ++n) {
        ef.process(Llow); // settle the LINEAR envelope onto Llow
    }

    const double lo     = dbOf(static_cast<double>(Llow));
    const double hi     = dbOf(static_cast<double>(Lhigh));
    const double target = lo + kCrossFrac * (hi - lo);

    const int maxSamples = static_cast<int>(8.0 * attackSeconds * kSampleRate) + 16;
    for (int n = 0; n < maxSamples; ++n) {
        const double envDb = dbOf(static_cast<double>(ef.process(Lhigh)));
        if (envDb >= target) {
            return n;
        }
    }
    return -1;
}

// Configure a fresh peak / DECOUPLED / decibel follower, honouring the usage
// contract (setDomain BEFORE reset so the smoother baselines at -120 dB).
EnvelopeFollower makeDbDecoupledFollower(bool smooth) {
    EnvelopeFollower ef;
    ef.init(kSampleRateF);
    ef.setMode(DetectMode::peak);
    ef.setBallistics(Ballistics::decoupled);
    ef.setSmooth(smooth);
    ef.setDomain(DetectDomain::decibel);
    ef.setAttack(static_cast<float>(kDecoupledAttackSec));
    ef.setRelease(static_cast<float>(kDecoupledReleaseSec));
    ef.reset(); // AFTER setDomain -> -120 dB baseline
    return ef;
}

} // namespace

TEST_CASE("dB envelope of silence is the -120 dBFS floor (SC-008)") {
    // Silence and any sub-floor level must map to exactly -120 dB and stay
    // there (the smoother baselines at -120, so there is nothing to move):
    // finite, never -inf / NaN.
    EnvelopeFollower ef = makeDbFollower(0.010f, kLongRelease);

    for (int n = 0; n < 2048; ++n) {
        const float env = ef.process(0.0f);
        CHECK(std::isfinite(env));
        CHECK(std::fabs(static_cast<double>(env) - kFloorDb) <= kFloorTolDb);
    }

    // A tiny SUB-FLOOR level (< 1e-6) also clamps to the floor, never -inf.
    for (int n = 0; n < 2048; ++n) {
        const float env = ef.process(1.0e-9f);
        CHECK(std::isfinite(env));
        CHECK(std::fabs(static_cast<double>(env) - kFloorDb) <= kFloorTolDb);
    }
}

TEST_CASE("dB of a steady level equals 20*log10(level)") {
    // Feed a constant level until the dB envelope settles; it must equal the
    // analytic 20*log10(level). Fast attack (1 ms) so kSettleSamples is many
    // time constants and the residual is far below kSteadyTolDb.
    constexpr float kFastAttack = 0.001f; // 1 ms

    auto settledDb = [](float level) {
        EnvelopeFollower ef = makeDbFollower(kFastAttack, kLongRelease);
        float env = 0.0f;
        for (int n = 0; n < kSettleSamples; ++n) {
            env = ef.process(level);
        }
        return static_cast<double>(env);
    };

    SUBCASE("level = 0.5 -> ~ -6.02 dB") {
        const double want = 20.0 * std::log10(0.5); // ~ -6.0206 dB
        CHECK(std::fabs(settledDb(0.5f) - want) <= kSteadyTolDb);
    }
    SUBCASE("level = 1.0 -> ~ 0 dB") {
        const double want = 20.0 * std::log10(1.0); // 0 dB
        CHECK(std::fabs(settledDb(1.0f) - want) <= kSteadyTolDb);
    }
    SUBCASE("level = 0.1 -> ~ -20 dB") {
        const double want = 20.0 * std::log10(0.1); // -20 dB
        CHECK(std::fabs(settledDb(0.1f) - want) <= kSteadyTolDb);
    }
}

TEST_CASE("level-independent attack time in dB (SC-006)") {
    // The headline property: with dB-domain detection the attack time (time to
    // cross 63.2% of the dB SPAN of a step) is the SAME at low and high absolute
    // levels — because the step is a clean rectangle in dB, so the smoother
    // one-poles across a fixed dB span in exactly the configured attack tau
    // regardless of where on the dB scale it sits.
    //
    // Case A (low level):  0.01 -> 0.10  (a +20 dB span)
    // Case B (high level): 0.10 -> 1.00  (also a +20 dB span)
    // Same attack (10 ms), long release (1 s), peak mode.
    //
    // We assert BOTH that each measured attack time matches the configured tau
    // AND that the two are equal. Pinning to tau is what makes this a genuine
    // TDD red: under the current linear passthrough stub the returned "dB"
    // values are actually linear (0.01..1.0), which are >= any negative dB
    // target immediately, so the stub reports a crossing at n = 0 (0 s) for both
    // cases — the equality alone would pass falsely, but the tau match fails.
    const int nA = dbAttackCrossingSamples(0.01f, 0.10f, static_cast<float>(kAttackTauSec));
    const int nB = dbAttackCrossingSamples(0.10f, 1.00f, static_cast<float>(kAttackTauSec));

    REQUIRE(nA >= 0);
    REQUIRE(nB >= 0);

    const double tA  = static_cast<double>(nA) / kSampleRate;
    const double tB  = static_cast<double>(nB) / kSampleRate;
    const double tol = kTimingTolFrac * kAttackTauSec;

    // Each case reaches 63.2% of its dB span in ~ the configured attack time.
    CHECK(std::fabs(tA - kAttackTauSec) <= tol);
    CHECK(std::fabs(tB - kAttackTauSec) <= tol);

    // ...and the two attack times are EQUAL -> level independence.
    CHECK(std::fabs(tA - tB) <= tol);
}

TEST_CASE("linear domain does NOT exhibit attack-time-accurate dB timing (contrast)") {
    // CONTROL / CONTRAST (linear domain, already implemented -> expected GREEN).
    //
    // Repeat the SAME two steps (0.01 -> 0.10 and 0.10 -> 1.00) in the LINEAR
    // domain, but STILL measure the crossing to 63.2% of the *dB* span (taking
    // the dB of the linear envelope each sample). The discriminating fact:
    //
    //   In the dB domain (T3) the dB envelope crosses 63.2% of the dB span in
    //   exactly the configured attack tau (~10 ms).
    //   In the linear domain the linear one-pole is warped when viewed in dB, so
    //   it reaches 63.2% of the dB span EARLIER (~4.5 ms == ~0.45 tau) — it does
    //   NOT deliver attack-time-accurate dB ballistics. That gap (dB-domain time
    //   vs linear-domain time) is the property dB-domain detection provides and
    //   linear detection lacks.
    //
    // NOTE on "level independence": because a linear one-pole is SCALE-INVARIANT
    // and both steps share the same +20 dB (10x) ratio, the two linear-domain dB
    // crossings are actually IDENTICAL to each other (both ~0.45 tau) — linear
    // detection is coincidentally level-independent for EQUAL-RATIO steps. So
    // the honest discriminator is not "linear A vs linear B differ" (they do
    // not) but "linear-domain dB timing differs from the configured attack tau
    // that dB-domain detection hits". We assert exactly that below, and also
    // pin the linear A==B scale-invariance for the record.
    const int nA = linearDomainDbCrossingSamples(0.01f, 0.10f, static_cast<float>(kAttackTauSec));
    const int nB = linearDomainDbCrossingSamples(0.10f, 1.00f, static_cast<float>(kAttackTauSec));

    REQUIRE(nA >= 0);
    REQUIRE(nB >= 0);

    const double tA  = static_cast<double>(nA) / kSampleRate;
    const double tB  = static_cast<double>(nB) / kSampleRate;
    const double tol = kTimingTolFrac * kAttackTauSec;

    // The discriminator: linear-domain dB timing is NOT within tolerance of the
    // configured attack tau (unlike the dB domain in T3, which IS).
    CHECK(std::fabs(tA - kAttackTauSec) > tol);
    CHECK(std::fabs(tB - kAttackTauSec) > tol);

    // For the record: the two linear crossings coincide (scale invariance at a
    // shared +20 dB ratio), so linear detection cannot be distinguished from
    // dB detection by A-vs-B equality alone — only by the tau mismatch above.
    CHECK(std::fabs(tA - tB) <= tol);
}

TEST_CASE("dB decoupled steady-state settles at 20*log10(level) (base + smooth)") {
    // Regression pin for the base-decoupled release-toward-floor fix: before
    // it, the base (setSmooth(false)) release stage decayed toward 0, which is
    // unity (loud) in the dB domain, so a steady 0.5 amplitude never settled
    // at the correct ~ -6.02 dB — it drifted toward ~0 dB instead. Cover BOTH
    // setSmooth(false) (the path that was broken) and setSmooth(true) (already
    // correct) so the two topologies stay distinguishable in DECOUPLED + dB.
    const double want = 20.0 * std::log10(0.5); // ~ -6.0206 dBFS

    SUBCASE("smooth = false (base)") {
        EnvelopeFollower ef = makeDbDecoupledFollower(/*smooth=*/false);
        float env = 0.0f;
        for (int n = 0; n < kSettleSamples; ++n) {
            env = ef.process(0.5f);
        }
        CHECK(std::isfinite(env));
        CHECK(std::fabs(static_cast<double>(env) - want) <= kSteadyTolDb);
    }
    SUBCASE("smooth = true") {
        EnvelopeFollower ef = makeDbDecoupledFollower(/*smooth=*/true);
        float env = 0.0f;
        for (int n = 0; n < kSettleSamples; ++n) {
            env = ef.process(0.5f);
        }
        CHECK(std::isfinite(env));
        CHECK(std::fabs(static_cast<double>(env) - want) <= kSteadyTolDb);
    }
}

TEST_CASE("dB decoupled release decays toward the -120 dBFS floor, not toward 0 dB (base + smooth)") {
    // The bug this pins: the base decoupled release stage used to decay
    // toward 0 linear, which is UNITY (loud) in the dB domain, so on silence
    // the dB envelope drifted back UP toward 0 dB instead of down toward the
    // -120 dBFS floor. After the fix both setSmooth(false) and
    // setSmooth(true) must decay DOWN, monotonically (within a tiny float
    // epsilon), toward the floor.
    auto exercise = [](bool smooth) {
        EnvelopeFollower ef = makeDbDecoupledFollower(smooth);

        // Settle high first (steady 0.5 amplitude) so there is a real level
        // to release from.
        float env = 0.0f;
        for (int n = 0; n < kSettleSamples; ++n) {
            env = ef.process(0.5f);
        }
        REQUIRE(std::isfinite(env));

        // Then feed silence and confirm the envelope only ever moves DOWN
        // (within kMonotonicEpsDb), ending well below the settled level and
        // close to the -120 dBFS floor.
        double prev = static_cast<double>(env);
        for (int n = 0; n < kReleaseSamples; ++n) {
            const double cur = static_cast<double>(ef.process(0.0f));
            CHECK(std::isfinite(cur));
            CHECK(cur <= prev + kMonotonicEpsDb);
            prev = cur;
        }
        CHECK(prev <= kNearFloorDb);
    };

    SUBCASE("smooth = false (base)") { exercise(false); }
    SUBCASE("smooth = true") { exercise(true); }
}
