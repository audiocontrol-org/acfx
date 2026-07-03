#include <doctest/doctest.h>

#include <cmath>

#include "effects/compressor/compressor-core.h"

// T012/T017 — US1 (MVP) + US3 end-to-end compressor correctness (TDD, written
// to spec).
//
// Drives the fully-composed acfx::CompressorCore (compressor-core.h) directly
// through process(x, key) per sample -- feedforward, keyless (key == x),
// level-smoothed ballistics, hard knee, no lookahead/makeup/mix/output
// coloring -- and asserts:
//
//   * T012 static map (spec.md US1 Acceptance Scenarios 1-2, SC-001):
//     compress, threshold -20 dBFS, ratio 4:1, hard knee -- a steady -10 dBFS
//     level settles to ~= -17.5 dBFS out; a steady -30 dBFS level (below
//     threshold) passes at unity.
//   * T012 attack/release timing (US1 Acceptance Scenario 3, SC-002): a step
//     from AT the threshold to well above it reaches ~63% (1 - 1/e) of its
//     steady gain reduction within the configured attack time; the reverse
//     step (above -> at threshold) recovers through 1/e of its starting gain
//     reduction within the configured release time.
//   * T012 silence edge case (spec.md "Edge Cases"): silence in => silence
//     out, no NaN/Inf.
//   * T017 limit e2e (US3 Acceptance Scenario 1, SC-001): limit mode,
//     threshold -6 dBFS -- a steady -1 dBFS level settles to ~= -6 dBFS
//     (brickwall: output held at the threshold).
//
// STIMULUS CHOICE (steady "tone/level" -> a constant/DC level): every
// stimulus below is a constant sample value fed every sample, not an AC sine
// tone. This is deliberate and still satisfies spec.md's "steady tone/level"
// language literally (the acceptance scenarios say "a steady -10 dBFS tone",
// and a DC level at that amplitude is a steady level): a sine tone would
// force a choice between peak detection (which ripples every half-cycle
// under the branching one-pole, since DetectMode::peak returns the
// UNheld |x| each sample -- see envelope-follower.h's detect()) and RMS
// detection (which requires knowing the peak/RMS ratio, 20*log10(sqrt(2)) =
// 3.0103 dB, to derive the expected settled level) -- both add measurement
// noise/complexity that is orthogonal to what this suite is proving (the
// static curve + ballistics timing). A constant level settles to an exactly
// steady output with NO ripple and needs no peak/RMS correction, giving tight
// deterministic assertions on the analytic curve itself.
//
// DETECTOR MODE (explicit, matching CompressorCore's own default): a
// bare CompressorCore (driven directly, as here) never calls setDetector()
// on its own, so its EnvelopeFollower detector_ keeps ITS default,
// DetectMode::peak (see envelope-follower.h's mode_ default). This suite
// configures DetectMode::peak explicitly for determinism rather than relying
// on the implicit default (CompressorEffect's applied default is `rms`
// instead -- see compressor-effect.h's detector_ member default -- but that
// wrapper default is irrelevant here since this suite drives CompressorCore
// directly). Because every stimulus is a constant/DC level, peak and RMS
// detection of it converge to the IDENTICAL settled value (|x| in both
// cases), so this choice does not change any expected value below -- it is
// pinned explicitly purely so the configuration is deterministic and
// self-documenting, per the task's instruction.
//
// LEVEL CONVENTION: levels are expressed in dBFS (20*log10 of a full-scale-1.0
// linear amplitude), matching the static curve's dB-domain contract
// (gain-computer.h, envelope-follower.h's decibel domain). Helpers below
// convert dB <-> linear amplitude in double precision for measurement
// headroom; the core itself computes in float.

using acfx::BallisticsSite;
using acfx::CompressorCore;
using acfx::Detection;
using acfx::DetectMode;
using acfx::GainMode;

namespace {

constexpr double kSampleRate = 48000.0;

// dB -> linear amplitude (double precision, measurement-side only; the core's
// own dbToLin() is float and private).
double dbToLinD(double db) { return std::pow(10.0, db * 0.05); }

// Linear amplitude -> dB. Every stimulus in this suite is well above the
// primitive's -120 dBFS floor (kFloorLin = 1e-6 in envelope-follower.h), so no
// floor guard is needed on the measurement side.
double linToDb(double lin) { return 20.0 * std::log10(lin); }

// Build a fully-configured, freshly-reset CompressorCore: feedforward
// detection, level-smoothed ballistics (US1 default site), peak detection
// (see DETECTOR MODE above), no sidechain HPF / lookahead / makeup / mix
// coloring beyond unity passthrough -- so the only thing shaping the output
// is the requested mode/threshold/ratio/knee/attack/release.
CompressorCore makeCore(GainMode mode, float thresholdDb, float ratio, float kneeDb,
                        float attackSeconds, float releaseSeconds) {
    CompressorCore core;
    core.prepare(static_cast<float>(kSampleRate), /*maxLookaheadSamples=*/0);

    core.setDetection(Detection::feedForward);
    core.setDetector(DetectMode::peak);
    core.setBallisticsSite(BallisticsSite::level);

    core.setMode(mode);
    core.setThreshold(thresholdDb);
    core.setRatio(ratio);
    core.setKnee(kneeDb);
    core.setRange(-40.0f); // unused by compress/limit; set explicitly for determinism

    core.setAttack(attackSeconds);
    core.setRelease(releaseSeconds);

    core.setSidechainHpf(0.0f); // bypass
    core.setLookahead(0);       // no added latency
    core.setMakeup(0.0f);
    core.setAutoMakeup(false);
    core.setMix(1.0f);   // fully wet (fully compressed)
    core.setOutput(0.0f); // no output trim

    core.reset();
    return core;
}

// Named tolerance for settled static-map assertions (SC-001). The settle
// windows used below run for tens of attack/release time constants, so the
// residual envelope-settling error is negligible (<< 0.01 dB); this bound
// covers float32 accumulation + log10 rounding only.
constexpr double kStaticTolDb = 0.1;

// Named tolerance for the attack/release crossing-time assertions (SC-002).
// The bare EnvelopeFollower ballistics test (envelope-follower-ballistics-
// test.cpp) uses +/-10% of tau for a plain one-pole step. Here the same
// one-pole is measured indirectly through the compressor's static curve, and
// the pre-step level is only ASYMPTOTICALLY (not exactly) pinned to the
// threshold by a long-but-finite settle dwell, adding a small extra residual
// on top -- so a wider +/-25% of the attack/release time is used, per the
// task's guidance that compressor ballistics timing is approximate.
constexpr double kTimingTolFrac = 0.25;

// One-pole step-response crossing fractions (matching envelope-follower-
// ballistics-test.cpp exactly): a rising one-pole reaches 1-1/e of its target
// step in one time constant; a falling one-pole decays THROUGH 1/e of its
// starting value in one time constant.
constexpr double kAttackCrossFrac = 0.63212055882855767;
constexpr double kReleaseCrossFrac = 0.36787944117144233;

void checkTiming(int crossingSample, double tauSeconds) {
    REQUIRE(crossingSample >= 0); // a crossing must have been found at all
    const double measuredSeconds = static_cast<double>(crossingSample) / kSampleRate;
    const double tolerance = kTimingTolFrac * tauSeconds;
    CHECK(std::fabs(measuredSeconds - tauSeconds) <= tolerance);
}

// Steps `core`'s (already-settled, feedforward, keyless) input to a constant
// `aboveDb` level and returns the first sample index where the measured gain
// reduction reaches kAttackCrossFrac of `grDbFinal` (the analytic steady-state
// gain reduction at that level), or -1 if it never does within maxSamples.
// grDb(n) is recovered from the per-sample output via
// grDb(n) = 20*log10(y(n)) - aboveDb (the input level itself is constant
// post-step, so subtracting it isolates the compressor's own gain change).
// `core` MUST already be settled with its detector envelope AT the threshold
// (u = 0) before this call -- see the TEST_CASE below -- so the step's GR
// trajectory is a clean scaled one-pole rise from the origin, letting the
// 63%-crossing land at the attack time constant itself (the classic
// unit-step derivation), rather than being skewed by an initial "dead zone"
// a coarser before/after level choice would introduce.
int attackCrossingSample(CompressorCore& core, double aboveDb, double grDbFinal,
                         int maxSamples) {
    const float x = static_cast<float>(dbToLinD(aboveDb));
    const double target = kAttackCrossFrac * grDbFinal; // grDbFinal <= 0
    for (int n = 0; n < maxSamples; ++n) {
        const float y = core.process(x, x);
        const double grDb = linToDb(static_cast<double>(y)) - aboveDb;
        if (grDb <= target) { // GR is <=0 and falling (more negative) toward grDbFinal
            return n;
        }
    }
    return -1;
}

// Steps `core` down to a constant `thresholdDb` level (u = 0, so the
// steady-state gain reduction there is exactly 0) and returns the first
// sample index where the measured gain reduction has recovered (risen)
// through kReleaseCrossFrac of `grDbFinal` (the gain reduction `core` was
// settled at just before this call), or -1 if it never does within
// maxSamples. `core` MUST already be settled ABOVE threshold at `grDbFinal`
// before this call (see the TEST_CASE below), so the step's GR trajectory is
// a clean scaled one-pole decay toward zero, matching the classic
// exponential-decay derivation.
int releaseCrossingSample(CompressorCore& core, double thresholdDb, double grDbFinal,
                          int maxSamples) {
    const float x = static_cast<float>(dbToLinD(thresholdDb));
    const double target = kReleaseCrossFrac * grDbFinal; // less negative than grDbFinal
    for (int n = 0; n < maxSamples; ++n) {
        const float y = core.process(x, x);
        const double grDb = linToDb(static_cast<double>(y)) - thresholdDb;
        if (grDb >= target) { // GR is <=0 and rising (less negative) toward 0
            return n;
        }
    }
    return -1;
}

} // namespace

TEST_CASE("T012 US1 compress static level map matches the analytic curve (SC-001)") {
    // thr -20 dBFS, ratio 4:1, hard knee (knee = 0), feedforward, level site.
    constexpr float kThresholdDb = -20.0f;
    constexpr float kRatio = 4.0f;
    constexpr float kAttackS = 0.010f;
    constexpr float kReleaseS = 0.100f;

    // >> attack time constant so the climb from the -120 dBFS floor fully
    // converges (both subcases below are RISES: the target level is above
    // the cold-start floor at every point during the climb, so the attack
    // coefficient governs throughout -- see envelope-follower.h's branching
    // applySmoothing()).
    const int settleSamples = static_cast<int>(30.0 * kAttackS * kSampleRate);

    SUBCASE("above threshold: -10 dBFS settles to ~-17.5 dBFS out") {
        CompressorCore core =
            makeCore(GainMode::compress, kThresholdDb, kRatio, 0.0f, kAttackS, kReleaseS);
        constexpr double kLevelDb = -10.0;
        const float x = static_cast<float>(dbToLinD(kLevelDb));

        float y = 0.0f;
        for (int n = 0; n < settleSamples; ++n) {
            y = core.process(x, x);
        }
        const double outDb = linToDb(static_cast<double>(y));

        CHECK(std::isfinite(outDb));
        // Analytic: out = thr + (level - thr)/ratio = -20 + (-10 - -20)/4 = -17.5.
        CHECK(std::fabs(outDb - (-17.5)) <= kStaticTolDb);
    }

    SUBCASE("below threshold: -30 dBFS passes at unity (no reduction)") {
        CompressorCore core =
            makeCore(GainMode::compress, kThresholdDb, kRatio, 0.0f, kAttackS, kReleaseS);
        constexpr double kLevelDb = -30.0;
        const float x = static_cast<float>(dbToLinD(kLevelDb));

        float y = 0.0f;
        for (int n = 0; n < settleSamples; ++n) {
            y = core.process(x, x);
        }
        const double outDb = linToDb(static_cast<double>(y));

        CHECK(std::isfinite(outDb));
        CHECK(std::fabs(outDb - kLevelDb) <= kStaticTolDb);
    }
}

TEST_CASE("T012 US1 compress attack/release reach the analytic crossing fraction "
          "in the configured time (SC-002)") {
    constexpr float kThresholdDb = -20.0f;
    constexpr float kRatio = 4.0f;
    constexpr float kAttackS = 0.010f;  // 10 ms, per spec.md Acceptance Scenario 3
    constexpr float kReleaseS = 0.100f;
    constexpr double kAboveDb = kThresholdDb + 20.0; // clearly above threshold
    // Analytic hard-knee slope above threshold: mHigh = 1/ratio - 1 = -0.75;
    // grDbFinal = mHigh * (kAboveDb - thr) = -0.75 * 20 = -15 dB.
    constexpr double kMHigh = 1.0 / 4.0 - 1.0;
    constexpr double kGrDbFinal = kMHigh * (kAboveDb - kThresholdDb);

    // >> attack time constant: both pre-settle phases below are RISES from
    // the cold-start floor (to the threshold, and to kAboveDb respectively),
    // so the attack coefficient governs throughout regardless of subcase.
    const int preSettleSamples = static_cast<int>(30.0 * kAttackS * kSampleRate);

    SUBCASE("attack reaches 63% of steady gain reduction within the attack time") {
        CompressorCore core =
            makeCore(GainMode::compress, kThresholdDb, kRatio, 0.0f, kAttackS, kReleaseS);

        // Pre-settle EXACTLY at the threshold (u = 0) so the step's GR
        // trajectory below is a clean scaled one-pole rise from the origin.
        const float xThr = static_cast<float>(dbToLinD(kThresholdDb));
        for (int n = 0; n < preSettleSamples; ++n) {
            core.process(xThr, xThr);
        }

        const int maxSamples = static_cast<int>(5.0 * kAttackS * kSampleRate) + 64;
        const int n = attackCrossingSample(core, kAboveDb, kGrDbFinal, maxSamples);
        checkTiming(n, static_cast<double>(kAttackS));
    }

    SUBCASE("release recovers through 1/e of steady gain reduction within the release time") {
        CompressorCore core =
            makeCore(GainMode::compress, kThresholdDb, kRatio, 0.0f, kAttackS, kReleaseS);

        // Pre-settle ABOVE threshold long enough for GR to reach kGrDbFinal
        // (an attack-governed climb); confirm the settle before trusting the
        // release measurement that follows.
        const float xAbove = static_cast<float>(dbToLinD(kAboveDb));
        float ySettled = 0.0f;
        for (int n = 0; n < preSettleSamples; ++n) {
            ySettled = core.process(xAbove, xAbove);
        }
        const double grDbSettled = linToDb(static_cast<double>(ySettled)) - kAboveDb;
        REQUIRE(std::fabs(grDbSettled - kGrDbFinal) <= kStaticTolDb);

        const int maxSamples = static_cast<int>(5.0 * kReleaseS * kSampleRate) + 64;
        const int n = releaseCrossingSample(core, kThresholdDb, kGrDbFinal, maxSamples);
        checkTiming(n, static_cast<double>(kReleaseS));
    }
}

TEST_CASE("T012 US1 silence in yields silence out, no NaN/Inf (spec.md Edge Cases)") {
    CompressorCore core = makeCore(GainMode::compress, -20.0f, 4.0f, 0.0f, 0.010f, 0.100f);

    for (int n = 0; n < 4800; ++n) {
        const float y = core.process(0.0f, 0.0f);
        CHECK(std::isfinite(y));
        // Exact 0 is expected (unity gain on silence, silent input); a tiny
        // epsilon absorbs float rounding only.
        CHECK(std::fabs(static_cast<double>(y)) <= 1.0e-6);
    }
}

TEST_CASE("T017 US3 limit mode settles to the threshold (brickwall, SC-001)") {
    // limit mode, threshold -6 dBFS, hard knee; ratio is ignored by limit
    // mode (gain-computer.h: mHigh is forced to -1 regardless) so its value
    // here is arbitrary.
    CompressorCore core = makeCore(GainMode::limit, -6.0f, /*ratio (ignored)=*/4.0f,
                                   0.0f, 0.010f, 0.100f);
    constexpr double kLevelDb = -1.0;
    const float x = static_cast<float>(dbToLinD(kLevelDb));

    const int settleSamples = static_cast<int>(30.0 * 0.010 * kSampleRate);
    float y = 0.0f;
    for (int n = 0; n < settleSamples; ++n) {
        y = core.process(x, x);
    }
    const double outDb = linToDb(static_cast<double>(y));

    CHECK(std::isfinite(outDb));
    CHECK(std::fabs(outDb - (-6.0)) <= kStaticTolDb);
}
