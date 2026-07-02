#include <doctest/doctest.h>

#include <cmath>
#include <vector>

#include "primitives/dynamics/envelope-follower.h"

// T022 — US3 peak-hold DETECTOR behavior (TDD, written to spec).
//
// Peak-hold acts at the DETECTOR stage (upstream of the ballistics smoother),
// so it is topology-independent. Per sample, with rect = fabs(x), state
// heldPeak_ and integer holdCounter_ (both cleared by reset()), and
// holdSamples_ = round(holdSeconds * fs):
//
//   if (rect >= heldPeak_) { heldPeak_ = rect; holdCounter_ = holdSamples_; }
//   else if (holdCounter_ > 0) { --holdCounter_; }   // heldPeak_ unchanged
//   else { heldPeak_ = rect; }                       // hold expired: track down
//   return heldPeak_;                                // detector output
//
// The returned detector level then goes through the domain + ballistics
// smoother as usual, so the "release after hold" shape is governed by the
// release ballistics, not by the detector itself.
//
// EXPECTED-RED: EnvelopeFollower::detect()'s peakHold branch is currently a
// TODO(T023) stub that just returns fabs(x) (plain peak, no latch/hold), so
// every TEST_CASE below FAILS now. They are written to the authoritative
// detector behavior above (the contract T023 implements) and go green once
// T023 lands the heldPeak_/holdCounter_ recurrence.

using acfx::Ballistics;
using acfx::DetectMode;
using acfx::EnvelopeFollower;

namespace {

inline constexpr double kSampleRate = 48000.0;
inline constexpr float  kSampleRateF = static_cast<float>(kSampleRate);

// Fast attack so the envelope reaches the established peak essentially
// immediately (well under one control period of the hold time), meaning the
// plateau measurement is not contaminated by attack settle.
inline constexpr float kFastAttackSeconds = 0.0001f; // 0.1 ms

// Moderate release: slow enough that a "release after hold" span is easy to
// distinguish sample-by-sample from the flat plateau, fast enough that tests
// stay cheap.
inline constexpr float kReleaseSeconds = 0.020f; // 20 ms

// Configure a fresh follower at kSampleRate: peakHold mode, given ballistics,
// fast attack / moderate release, given hold time, freshly reset.
EnvelopeFollower makeHoldFollower(Ballistics ballistics, float holdSeconds) {
    EnvelopeFollower ef;
    ef.init(kSampleRateF);
    ef.setMode(DetectMode::peakHold);
    ef.setBallistics(ballistics);
    ef.setAttack(kFastAttackSeconds);
    ef.setRelease(kReleaseSeconds);
    ef.setHold(holdSeconds);
    ef.reset();
    return ef;
}

// holdSeconds -> integer hold-sample count, mirroring
// EnvelopeFollower::holdSamplesFor() (round(seconds*fs), floor at 0) so tests
// reason about the SAME sample-accurate window the primitive uses.
int holdSamplesFor(double holdSeconds) {
    if (holdSeconds <= 0.0) {
        return 0;
    }
    return static_cast<int>(std::lround(holdSeconds * kSampleRate));
}

// Analytic release-onset budget: once the hold expires, the DETECTOR output
// steps straight down (silence input => heldPeak_ = rect = 0 the very next
// sample), but the returned ENVELOPE only crosses frac*peak once the release
// one-pole y[m] = peak * aRel^m has decayed that far:
//   peak * aRel^m <= frac*peak  =>  m >= ln(frac) / ln(aRel).
// Since aRel = exp(-1/(releaseSeconds*fs)), ln(aRel) = -1/(releaseSeconds*fs),
// so m >= -ln(frac) * releaseSeconds * fs. This is the number of samples the
// "drop below frac*peak" test has to wait AFTER holdSamples elapses purely
// because of release ballistics, not because of any hold-detector slop — it
// is the dominant term in the boundary tolerance below (for
// kReleaseSeconds/kPlateauFrac as configured here, ~100 samples, versus a
// couple of samples of lround()/attack-settle slop).
int releaseOnsetSamples(double frac, double releaseSeconds) {
    return static_cast<int>(std::ceil(-std::log(frac) * releaseSeconds * kSampleRate));
}

} // namespace

TEST_CASE("peak-hold latches and holds a peak for the hold time") {
    // Named tolerance rationale: while the hold counter is running, the
    // DETECTOR output is pinned at the latched peak (heldPeak_ constant), and
    // the fast attack (0.1 ms == 4.8 samples at 48 kHz) means the smoothed
    // envelope only ever rises toward that constant during the entire hold
    // window -- it can never dip below a threshold set at or below the
    // post-attack-settle peak until the hold actually expires. So the lower
    // bound on the observed drop sample is holdSamples itself (no slack
    // needed there beyond a couple of samples for lround() edge effects).
    // The UPPER bound needs real slack: once the hold expires, the detector
    // steps to 0 in one sample, but the release one-pole still needs
    // releaseOnsetSamples() samples to visibly decay through the 0.9*P line
    // (see that helper's derivation) -- that release-onset budget dominates
    // the tolerance.
    constexpr float kHoldSeconds = 0.050f; // 50 ms
    const int holdSamples = holdSamplesFor(kHoldSeconds);
    constexpr double kPlateauFrac = 0.9;
    // A couple of samples of slack for lround()/attack-settle edge effects,
    // on top of the dominant release-onset budget.
    constexpr int kEdgeSlackSamples = 8;
    const int lowerToleranceSamples = kEdgeSlackSamples;
    const int upperToleranceSamples =
        releaseOnsetSamples(kPlateauFrac, kReleaseSeconds) + kEdgeSlackSamples;

    EnvelopeFollower ef = makeHoldFollower(Ballistics::branching, kHoldSeconds);

    // Establish a peak P ~= 1.0: a single loud sample is enough given the
    // fast attack, but feed a short burst so the attack indisputably settles.
    // P itself is the nominal input amplitude (not the just-post-burst
    // envelope snapshot): the detector holds heldPeak_ pinned at that raw
    // input value for the ENTIRE hold window (2400 samples here, >> the ~5
    // sample attack time constant), so the branching envelope has ample time
    // to converge the rest of the way to P well before the hold expires --
    // using the raw amplitude as the threshold basis (rather than the
    // 16-sample burst snapshot, which hasn't fully converged yet) keeps the
    // analytic release-onset budget below exact.
    constexpr float kAmplitude = 1.0f;
    constexpr int kBurstSamples = 16;
    float peak = 0.0f;
    for (int n = 0; n < kBurstSamples; ++n) {
        peak = ef.process(kAmplitude);
    }
    REQUIRE(static_cast<double>(peak) > 0.9); // attack must have reached ~P before silence starts

    // Now feed silence and track when the envelope first drops below 0.9*P.
    const double threshold = kPlateauFrac * static_cast<double>(kAmplitude);

    int dropSample = -1;
    const int maxSamples = holdSamples + upperToleranceSamples +
                            20 * static_cast<int>(kReleaseSeconds * kSampleRate);
    for (int n = 0; n < maxSamples; ++n) {
        const float env = ef.process(0.0f);
        if (dropSample < 0 && static_cast<double>(env) < threshold) {
            dropSample = n;
        }
        // Before the hold window can plausibly expire, the envelope must
        // stay at or above P (it is monotonically rising toward the constant
        // held detector level during this entire span).
        if (n < holdSamples - lowerToleranceSamples) {
            CHECK(static_cast<double>(env) > threshold);
        }
    }

    REQUIRE(dropSample >= 0); // the envelope must eventually release
    CHECK(dropSample >= holdSamples - lowerToleranceSamples);
    CHECK(dropSample <= holdSamples + upperToleranceSamples);
}

TEST_CASE("a higher peak during the hold window restarts the hold") {
    // Same tolerance rationale as the previous TEST_CASE (see
    // releaseOnsetSamples()'s doc comment), applied to the SECOND (higher)
    // peak's hold window instead of the first.
    constexpr float kHoldSeconds = 0.050f; // 50 ms
    const int holdSamples = holdSamplesFor(kHoldSeconds);
    constexpr double kPlateauFrac = 0.9;
    constexpr int kEdgeSlackSamples = 8;
    const int lowerToleranceSamples = kEdgeSlackSamples;
    const int upperToleranceSamples =
        releaseOnsetSamples(kPlateauFrac, kReleaseSeconds) + kEdgeSlackSamples;

    EnvelopeFollower ef = makeHoldFollower(Ballistics::branching, kHoldSeconds);

    constexpr float kP1 = 0.5f;
    constexpr float kP2 = 1.0f;

    // Establish P1.
    constexpr int kBurstSamples = 16;
    float afterP1 = 0.0f;
    for (int n = 0; n < kBurstSamples; ++n) {
        afterP1 = ef.process(kP1);
    }
    REQUIRE(static_cast<double>(afterP1) > 0.9 * static_cast<double>(kP1));

    // Partway through the P1 hold window (well before it would expire), feed
    // a higher peak P2.
    const int kPartway = holdSamples / 4;
    for (int n = 0; n < kPartway; ++n) {
        ef.process(0.0f);
    }

    float afterP2 = 0.0f;
    for (int n = 0; n < kBurstSamples; ++n) {
        afterP2 = ef.process(kP2);
    }
    // The held value must rise to ~= P2, not stay pinned near P1.
    REQUIRE(static_cast<double>(afterP2) > 0.9 * static_cast<double>(kP2));

    // The hold window restarts from THIS point: the envelope must stay near
    // P2 for ~holdSeconds measured from here, then release. As in the
    // previous TEST_CASE, use the nominal amplitude P2 (not the just-post-
    // burst afterP2 snapshot) as the threshold basis: the restarted hold
    // window is long enough (2400 samples, >> the attack time constant) for
    // the envelope to converge the rest of the way to P2 before it expires.
    const double threshold = kPlateauFrac * static_cast<double>(kP2);
    int dropSample = -1;
    const int maxSamples = holdSamples + upperToleranceSamples +
                            20 * static_cast<int>(kReleaseSeconds * kSampleRate);
    for (int n = 0; n < maxSamples; ++n) {
        const float env = ef.process(0.0f);
        if (dropSample < 0 && static_cast<double>(env) < threshold) {
            dropSample = n;
        }
        if (n < holdSamples - lowerToleranceSamples) {
            CHECK(static_cast<double>(env) > threshold);
        }
    }

    REQUIRE(dropSample >= 0);
    CHECK(dropSample >= holdSamples - lowerToleranceSamples);
    CHECK(dropSample <= holdSamples + upperToleranceSamples);
}

TEST_CASE("peak-hold is topology-independent (branching == decoupled during hold)") {
    // Hold happens upstream of the ballistics smoother, so for the SAME
    // peak-then-silence input, the held plateau value and its dwell duration
    // must match between branching and decoupled — differences may only
    // appear once release begins shaping the two topologies differently.
    constexpr float kHoldSeconds = 0.050f; // 50 ms
    const int holdSamples = holdSamplesFor(kHoldSeconds);

    // Guard band strictly inside the hold window so we are comparing the
    // plateau region only (never near the release-onset boundary, where the
    // two topologies are explicitly allowed to start diverging).
    constexpr int kPlateauGuardSamples = 32; // >> attack settle
    const int kPlateauEnd = holdSamples - kPlateauGuardSamples;
    REQUIRE(kPlateauEnd > kPlateauGuardSamples); // sanity: window big enough to have a plateau

    // Tight tolerance: within the plateau both topologies just replay the
    // SAME detector output (heldPeak_) through their own smoother, and the
    // fast attack has long since settled both smoothers onto that same
    // constant value, so they should agree to within float rounding.
    constexpr double kPlateauTolAbs = 1.0e-4;

    EnvelopeFollower branchingEf = makeHoldFollower(Ballistics::branching, kHoldSeconds);
    EnvelopeFollower decoupledEf = makeHoldFollower(Ballistics::decoupled, kHoldSeconds);

    constexpr int kBurstSamples = 16;
    float branchingPeak = 0.0f;
    float decoupledPeak = 0.0f;
    for (int n = 0; n < kBurstSamples; ++n) {
        branchingPeak = branchingEf.process(1.0f);
        decoupledPeak = decoupledEf.process(1.0f);
    }
    REQUIRE(static_cast<double>(branchingPeak) > 0.9);
    REQUIRE(static_cast<double>(decoupledPeak) > 0.9);

    for (int n = 0; n < kPlateauEnd; ++n) {
        const float branchingEnv = branchingEf.process(0.0f);
        const float decoupledEnv = decoupledEf.process(0.0f);
        if (n >= kPlateauGuardSamples) {
            CHECK(std::fabs(static_cast<double>(branchingEnv) -
                             static_cast<double>(decoupledEnv)) <= kPlateauTolAbs);
        }
    }
}

TEST_CASE("hold = 0 degenerates to plain peak") {
    // With holdSamples_ == 0, the detector's "else if (holdCounter_ > 0)"
    // branch is never taken (holdCounter_ is latched to 0 alongside every new
    // peak), so heldPeak_ tracks the input on every sample where rect <
    // heldPeak_ too -- i.e. plain peak detection, no plateau. After the burst
    // establishes P, silence must release immediately per the ballistics
    // (matching a plain peak/branching follower's release curve), not sit
    // near P for any extended window.
    constexpr int kBurstSamples = 16;

    EnvelopeFollower holdEf = makeHoldFollower(Ballistics::branching, /*holdSeconds=*/0.0f);
    EnvelopeFollower peakEf;
    peakEf.init(kSampleRateF);
    peakEf.setMode(DetectMode::peak);
    peakEf.setBallistics(Ballistics::branching);
    peakEf.setAttack(kFastAttackSeconds);
    peakEf.setRelease(kReleaseSeconds);
    peakEf.reset();

    float holdPeak = 0.0f;
    float plainPeak = 0.0f;
    for (int n = 0; n < kBurstSamples; ++n) {
        holdPeak = holdEf.process(1.0f);
        plainPeak = peakEf.process(1.0f);
    }
    REQUIRE(static_cast<double>(holdPeak) > 0.9);
    REQUIRE(static_cast<double>(plainPeak) > 0.9);

    // No extended plateau: with hold = 0, holdCounter_ is latched to 0
    // alongside every peak, so the detector free-runs as plain peak and the
    // release begins decaying immediately (n = 0 of silence), not after any
    // dwell. Reuse releaseOnsetSamples() (see its doc comment on the previous
    // TEST_CASEs) for the analytic number of samples the release one-pole
    // needs to visibly cross 0.9*peak -- the drop must land at ~that budget
    // from the START of silence, not at ~holdSamples-from-TEST_CASE-1's
    // kHoldSeconds later, which is what a still-latching hold would produce.
    constexpr double kPlateauFrac = 0.9;
    const double threshold = kPlateauFrac * static_cast<double>(holdPeak);
    const int expectedDropSample = releaseOnsetSamples(kPlateauFrac, kReleaseSeconds);
    constexpr int kEdgeSlackSamples = 8;
    const int maxCheckSamples = expectedDropSample + 20 * kEdgeSlackSamples;

    int dropSample = -1;
    for (int n = 0; n < maxCheckSamples; ++n) {
        const float env = holdEf.process(0.0f);
        if (static_cast<double>(env) < threshold) {
            dropSample = n;
            break;
        }
    }
    REQUIRE(dropSample >= 0);
    // Tight window around the analytic release-onset budget -- NOT anywhere
    // near TEST_CASE 1's holdSamples (2400 for the shared 50 ms hold), which
    // is what a spuriously-still-latching hold would produce instead.
    CHECK(dropSample >= expectedDropSample - kEdgeSlackSamples);
    CHECK(dropSample <= expectedDropSample + kEdgeSlackSamples);

    // Stronger check: hold=0 peak-hold must match plain peak detection
    // sample-for-sample through the release (both are driven by the same
    // release ballistics on the same rect input).
    EnvelopeFollower holdEf2 = makeHoldFollower(Ballistics::branching, /*holdSeconds=*/0.0f);
    EnvelopeFollower peakEf2;
    peakEf2.init(kSampleRateF);
    peakEf2.setMode(DetectMode::peak);
    peakEf2.setBallistics(Ballistics::branching);
    peakEf2.setAttack(kFastAttackSeconds);
    peakEf2.setRelease(kReleaseSeconds);
    peakEf2.reset();

    constexpr double kMatchTolAbs = 1.0e-4;
    const int compareSamples =
        kBurstSamples + static_cast<int>(10.0 * kReleaseSeconds * kSampleRate);
    std::vector<float> holdInput(static_cast<std::size_t>(kBurstSamples), 1.0f);
    holdInput.resize(static_cast<std::size_t>(compareSamples), 0.0f);
    for (int n = 0; n < compareSamples; ++n) {
        const float x = holdInput[static_cast<std::size_t>(n)];
        const float a = holdEf2.process(x);
        const float b = peakEf2.process(x);
        CHECK(std::fabs(static_cast<double>(a) - static_cast<double>(b)) <= kMatchTolAbs);
    }
}
