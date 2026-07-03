#include <doctest/doctest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

#include "dsp/audio-block.h"
#include "dsp/param-id.h"
#include "dsp/parameter.h"
#include "dsp/process-context.h"
#include "effects/compressor/compressor-core.h"   // GainMode / DetectMode
#include "effects/compressor/compressor-effect.h" // CompressorEffect (T032-T034)

// T033 (US10) -- CompressorEffect lookahead: reported latency + first-sample
// transient limiting (FR-015, FR-021, SC-008).
//
// Written against specs/compressors/spec.md User Story 10 / SC-008 and the
// headers this suite reasons over:
//   - core/effects/compressor/compressor-effect.h: latencySamples() reports
//     round(lookaheadSeconds_ * sampleRate_), recomputed in applyLookahead()
//     whenever the kLookahead parameter is applied (applyPending(), consumed
//     at the top of process()). The wrapper stores lookahead in SECONDS
//     (kParams[kLookahead] is ParamUnit::seconds, range 0..0.02) even though
//     the design-record ranges are documented in ms -- see
//     compressor-parameters.h's "NOTE on time-valued units".
//   - core/effects/compressor/compressor-core.h: CompressorCore::applyGain()
//     runs the main-path DelayLine every sample (lookahead_.write(x) then
//     readFractional(lookaheadSamples_)) while detectGainLin() computes the
//     gain from the CURRENT (undelayed) key. So the gain applied to a
//     delayed sample was computed from a LATER point on the input timeline --
//     lookaheadSamples of ballistics ramp-up have already elapsed by the time
//     a transient's delayed copy reaches the VCA multiply. lookahead=0
//     degenerates to readFractional(0) == the just-written sample (bypass).
//   - core/primitives/delays/delay-line.h: the pre-delay primitive; capacity
//     is sized once in CompressorCore (kMaxLookaheadSamples/kLookaheadCapacity)
//     and CompressorEffect::prepare() clamps to
//     round(kParams[kLookahead].max * sampleRate) (960 samples @ 48 kHz).
//
// TEST-SETUP NOTE (important, not obvious from the header alone):
// setParameter() only PUBLISHES a pending atomic value (FR-020); it is
// consumed by applyPending() at the TOP of the next process() call, not by
// prepare()/reset() (whose applyAll() pushes the currently-CACHED member
// values, not the pending queue). So every case below calls process() at
// least once after setParameter() before reading back latencySamples() or
// judging DSP output -- this is NOT a workaround, it's the documented
// cross-thread contract (FR-020) exercised as intended.

using acfx::AudioBlock;
using acfx::CompressorEffect;
using acfx::DetectMode;
using acfx::GainMode;
using acfx::ParamId;
using acfx::ProcessContext;

namespace {

constexpr double kSampleRateD = 48000.0;

// Normalize a plain-units value for a CompressorEffect parameter -- the
// inverse of the wrapper's internal denormalize(), so tests can drive
// setParameter() in the plain units the header documents (seconds, dB, mode
// index, ...) instead of hand-computing normalized floats.
float normFor(CompressorEffect::Param p, float plainValue) {
    return acfx::normalize(CompressorEffect::kParams[p], plainValue);
}

// One silent sample, single channel -- just enough to drive
// CompressorEffect::process() once so applyPending() flushes whatever
// setParameter() calls were made since prepare() / the last process().
void flushPending(CompressorEffect& fx) {
    float sample = 0.0f;
    float* chans[1] = {&sample};
    AudioBlock block(chans, 1, 1);
    fx.process(block);
}

} // namespace

// ---------------------------------------------------------------------------
// Reported latency: latencySamples() == round(lookaheadSeconds * sampleRate)
// (FR-021, SC-008, US10 Acceptance Scenario 1). EXACT integer-sample
// assertions -- round(L * fs) must land on a whole sample count, no
// tolerance band.
// ---------------------------------------------------------------------------
TEST_CASE("CompressorEffect::latencySamples() reports round(lookaheadSeconds * fs) exactly (FR-021, SC-008)") {
    struct Case {
        float lookaheadMs;
        int   expectedSamples;
    };
    // 48 kHz keeps every case an exact multiple (no rounding ambiguity):
    //   0 ms -> 0; 5 ms -> 240; 10 ms -> 480; 20 ms -> 960 (== the top of
    // kParams[kLookahead]'s 0..0.02 s range, so this also exercises the
    // upper bound CompressorEffect::prepare() clamps CompressorCore's
    // maxLookaheadSamples_ to).
    constexpr std::array<Case, 4> kCases = {{
        {0.0f, 0},
        {5.0f, 240},
        {10.0f, 480},
        {20.0f, 960},
    }};

    for (const auto& c : kCases) {
        INFO("lookaheadMs=" << c.lookaheadMs);
        CompressorEffect fx;
        fx.prepare(ProcessContext{kSampleRateD, 1, 1});

        const float lookaheadSeconds = c.lookaheadMs * 0.001f;
        fx.setParameter(ParamId{CompressorEffect::kLookahead},
                        normFor(CompressorEffect::kLookahead, lookaheadSeconds));
        flushPending(fx); // applyPending() -> applyLookahead() runs at the top of process()

        CHECK(fx.latencySamples() == c.expectedSamples);
    }
}

// ---------------------------------------------------------------------------
// Edge Case: "Lookahead = 0: zero added latency, no delay buffer engaged;
// reported latency is 0." Checked straight off prepare() -- lookahead's
// descriptor default is 0 (compressor-parameters.h row 11), and
// prepare()'s applyAll() -> applyLookahead() establishes latencySamples_
// from that default with NO setParameter()/process() call at all.
// ---------------------------------------------------------------------------
TEST_CASE("CompressorEffect lookahead=0 adds no latency immediately after prepare() (Edge Case)") {
    CompressorEffect fx;
    fx.prepare(ProcessContext{kSampleRateD, 1, 1});
    CHECK(fx.latencySamples() == 0);
}

// ---------------------------------------------------------------------------
// First-sample transient limiting: lookahead pre-charges gain reduction so a
// sharp above-threshold transient is limited (near) from its first output
// sample; a zero-lookahead limiter cannot react in time and lets the
// transient's raw amplitude through on that first sample (SC-008, US10
// Acceptance Scenario 2).
//
// Stimulus: silence, then an instantaneous step to a sustained transient
// amplitude (a "fast attack, sustained" transient -- the classic worst case
// for a zero-lookahead limiter, and simpler to reason about exactly than an
// impulse, whose peak a zero-lookahead limiter would barely touch either way
// given a non-zero attack time).
//
// Config: mode=limit, hard knee (0 dB) so the curve is the exact brickwall
// u>0 => outputDb=thresholdDb, no knee-rounding ambiguity; detector=peak (the
// simplest, most direct level map for a DC-like step); ballisticsSite=level
// (the default -- CompressorCore::detector_ carries attack/release).
//
// DERIVATION (worked from the shipped formulas, so the assertions below are
// not a guess):
//   EnvelopeFollower's branching one-pole (envelope-follower.h) is
//   y[i] = a*y[i-1] + (1-a)*target, a = exp(-1/(attackSeconds*fs)), starting
//   from the dB-domain floor kFloorDb = -120 (spec.md Edge Cases: "the
//   dB-domain detection floor (-120 dBFS, inherited from EnvelopeFollower)").
//   With attackSeconds = 1 ms @ 48 kHz, tau = 48 samples.
//
//   Zero-lookahead (L=0): CompressorCore::process() computes gainLin from
//   the level AT THE SAME SAMPLE it applies to. At the transient's very
//   first sample (i=0), only ONE ballistics step has run:
//     y0 = a*(-120) + (1-a)*0 ~= -117.5 dB, still far below (threshold=-6dB),
//   so GainComputer's hard-knee low branch (mLow=0) returns EXACTLY 0 dB
//   reduction -- gainLin == 1.0 exactly. The limiter passes the transient's
//   full raw amplitude on its first sample: an actual overshoot past the
//   threshold-linear amplitude, matching "no threshold overshoot a
//   zero-lookahead limiter would pass" (SC-008) as the very failure mode
//   lookahead exists to fix.
//
//   Lookahead=5ms=240 samples=5*tau: CompressorCore::applyGain() delays the
//   main path by 240 samples, so the transient's DELAYED copy reaches the
//   VCA multiply at the same wall-clock sample as detectGainLin() is
//   reading the key 240 samples INTO the transient (i=240, 5 time
//   constants): y240 = -120*a^241 ~= -0.79 dB, close to the 0 dB target, so
//   grDb = thresholdDb - y240 ~= -5.21 dB -> gainLin ~= 0.549 (~-5.2 dB).
//   Applying that to the delayed transient gives an output peak near the
//   threshold-linear amplitude (~0.501), not the raw transient amplitude
//   (1.0) -- attenuated from (near) its first output sample, as SC-008
//   requires. (Gain keeps decreasing on subsequent samples as the level
//   keeps ramping toward the true steady state, so this first delayed
//   sample IS the peak of the lookahead-engaged output.)
// ---------------------------------------------------------------------------
TEST_CASE("CompressorEffect: lookahead limits a first-sample transient; zero-lookahead lets it overshoot (SC-008, US10 Acceptance Scenario 2)") {
    constexpr float kThresholdDb      = -6.0f;
    constexpr float kAttackSeconds    = 0.001f; // 1 ms
    constexpr float kReleaseSeconds   = 0.100f; // 100 ms -- irrelevant to a rising transient
    constexpr float kLookaheadSeconds = 0.005f; // 5 ms == 5x the attack time constant
    constexpr int   kLookaheadSamples = 240;    // round(0.005 * 48000) -- matches the T033 latency case above
    constexpr float kThresholdLin     = 0.50118723f; // 10^(-6/20)
    constexpr float kTransientAmplitude = 1.0f;       // 0 dBFS sustained step

    constexpr int kPreRoll  = 100;  // silence before the transient
    constexpr int kPostRoll = 1000; // sustained transient afterward (>> kLookaheadSamples, so the delayed transient is fully visible)
    constexpr int kTotal    = kPreRoll + kPostRoll;

    auto configure = [&](CompressorEffect& fx, float lookaheadSeconds) {
        fx.prepare(ProcessContext{kSampleRateD, kTotal, 1});
        fx.setParameter(ParamId{CompressorEffect::kMode},
                        normFor(CompressorEffect::kMode,
                                static_cast<float>(static_cast<int>(GainMode::limit))));
        fx.setParameter(ParamId{CompressorEffect::kThreshold},
                        normFor(CompressorEffect::kThreshold, kThresholdDb));
        fx.setParameter(ParamId{CompressorEffect::kKnee}, normFor(CompressorEffect::kKnee, 0.0f)); // hard knee
        fx.setParameter(ParamId{CompressorEffect::kAttack},
                        normFor(CompressorEffect::kAttack, kAttackSeconds));
        fx.setParameter(ParamId{CompressorEffect::kRelease},
                        normFor(CompressorEffect::kRelease, kReleaseSeconds));
        fx.setParameter(ParamId{CompressorEffect::kDetector},
                        normFor(CompressorEffect::kDetector,
                                static_cast<float>(static_cast<int>(DetectMode::peak))));
        fx.setParameter(ParamId{CompressorEffect::kLookahead},
                        normFor(CompressorEffect::kLookahead, lookaheadSeconds));
        // mode/threshold/knee/attack/release/detector/lookahead default
        // ballisticsSite=level, detection=feedForward, mix=1, makeup=0,
        // output=0 dB -- all left at their descriptor defaults (already
        // pushed by prepare()'s applyAll()), so no explicit set needed.
    };

    auto buildTransient = [&] {
        std::vector<float> x(static_cast<std::size_t>(kTotal), 0.0f);
        for (int n = kPreRoll; n < kTotal; ++n)
            x[static_cast<std::size_t>(n)] = kTransientAmplitude;
        return x;
    };

    float maxWithLookahead = 0.0f;
    {
        CompressorEffect fx;
        configure(fx, kLookaheadSeconds);
        std::vector<float> buf = buildTransient();
        float* chans[1] = {buf.data()};
        AudioBlock block(chans, 1, kTotal);
        fx.process(block); // the first process() call flushes every setParameter() above, THEN runs the whole signal

        REQUIRE(fx.latencySamples() == kLookaheadSamples); // setup sanity check

        for (float y : buf) {
            REQUIRE(std::isfinite(y));
            maxWithLookahead = std::max(maxWithLookahead, std::fabs(y));
        }
    }

    float maxWithoutLookahead = 0.0f;
    {
        CompressorEffect fx;
        configure(fx, 0.0f);
        std::vector<float> buf = buildTransient();
        float* chans[1] = {buf.data()};
        AudioBlock block(chans, 1, kTotal);
        fx.process(block);

        REQUIRE(fx.latencySamples() == 0);

        for (float y : buf) {
            REQUIRE(std::isfinite(y));
            maxWithoutLookahead = std::max(maxWithoutLookahead, std::fabs(y));
        }
    }

    // Named tolerances (comparative, not exact -- per the derivation above,
    // these are generous bounds around the worked numeric estimates, not
    // hand-picked to only-just pass):
    //   kOvershootFloor: the zero-lookahead limiter passes (near) the full
    //     unattenuated transient on its first sample (derivation: gainLin ==
    //     1.0 exactly at i=0) -- floor at 90% of the transient amplitude
    //     leaves ample margin below the derived ~100%.
    //   kLookaheadCeiling: the lookahead-engaged limiter has ~99.3% settled
    //     gain reduction by the time the delayed transient arrives
    //     (derivation: ~0.549 peak) -- ceiling at the HALFWAY point between
    //     the threshold-linear amplitude and the raw transient amplitude, well
    //     above the derived value, so this is a real margin, not a hairline.
    constexpr float kOvershootFloor   = 0.9f * kTransientAmplitude;
    constexpr float kLookaheadCeiling = kThresholdLin + 0.5f * (kTransientAmplitude - kThresholdLin);

    CHECK(maxWithoutLookahead > kOvershootFloor);
    CHECK(maxWithLookahead < kLookaheadCeiling);

    // The core SC-008 comparative assertion: for the SAME brickwall-limit
    // config on a sharp transient, lookahead must not let the peak exceed
    // what a zero-lookahead limiter would pass.
    CHECK(maxWithLookahead <= maxWithoutLookahead);
}
