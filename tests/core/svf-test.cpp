#include <doctest/doctest.h>

#include <cmath>

#include "dsp/audio-block.h"
#include "dsp/param-id.h"
#include "dsp/parameter.h"
#include "dsp/process-context.h"
#include "effects/svf/svf-effect.h"
#include "support/svf-reference.h"

// T016 — SVF effect: per-mode frequency response vs the known-good references
// (T013), plus NaN/denormal stability at high resonance. Fails until SvfEffect
// (T017) is implemented.

using namespace acfx;
using acfx::test::kPassbandFreqHz;
using acfx::test::kPassbandGainMin;
using acfx::test::kRefCutoffHz;
using acfx::test::kRefSampleRate;
using acfx::test::kStopbandFreqHz;
using acfx::test::kStopbandGainMax;
using acfx::test::measureMagnitude;

namespace {

// Configure a prepared mono SvfEffect at the reference cutoff, zero resonance,
// in the requested mode. Returns a per-sample processing callable for the
// magnitude measurement.
struct MonoDriver {
    SvfEffect fx;
    float scratch = 0.0f;

    explicit MonoDriver(SvfMode mode, float resonanceNorm = 0.0f) {
        fx.prepare(ProcessContext{kRefSampleRate, 1, 1});
        fx.setParameter(ParamId{SvfEffect::kCutoff},
                        normalize(SvfEffect::kParams[SvfEffect::kCutoff],
                                  static_cast<float>(kRefCutoffHz)));
        fx.setParameter(ParamId{SvfEffect::kResonance}, resonanceNorm);
        const float modeIndex = static_cast<float>(static_cast<int>(mode));
        fx.setParameter(ParamId{SvfEffect::kMode},
                        normalize(SvfEffect::kParams[SvfEffect::kMode], modeIndex));
    }

    float operator()(float in) {
        scratch = in;
        float* chans[1] = {&scratch};
        AudioBlock block(chans, 1, 1);
        fx.process(block);
        return scratch;
    }
};

} // namespace

TEST_CASE("lowpass passes lows and attenuates highs") {
    MonoDriver lp{SvfMode::lowpass};
    const double passband = measureMagnitude(lp, kPassbandFreqHz, kRefSampleRate);
    MonoDriver lp2{SvfMode::lowpass};
    const double stopband = measureMagnitude(lp2, kStopbandFreqHz, kRefSampleRate);

    CHECK(passband >= kPassbandGainMin);
    CHECK(stopband <= kStopbandGainMax);
    CHECK(passband > stopband);
}

TEST_CASE("highpass passes highs and attenuates lows") {
    MonoDriver hpLow{SvfMode::highpass};
    const double lowGain = measureMagnitude(hpLow, kPassbandFreqHz, kRefSampleRate);
    MonoDriver hpHigh{SvfMode::highpass};
    const double highGain = measureMagnitude(hpHigh, kStopbandFreqHz, kRefSampleRate);

    CHECK(lowGain <= kStopbandGainMax);
    CHECK(highGain >= kPassbandGainMin);
    CHECK(highGain > lowGain);
}

TEST_CASE("bandpass emphasizes the centre relative to both edges") {
    MonoDriver bpCentre{SvfMode::bandpass};
    const double centre = measureMagnitude(bpCentre, kRefCutoffHz, kRefSampleRate);
    MonoDriver bpLow{SvfMode::bandpass};
    const double low = measureMagnitude(bpLow, kPassbandFreqHz, kRefSampleRate);
    MonoDriver bpHigh{SvfMode::bandpass};
    const double high = measureMagnitude(bpHigh, kStopbandFreqHz, kRefSampleRate);

    CHECK(centre > low);
    CHECK(centre > high);
}

TEST_CASE("high resonance stays NaN/denormal-free and bounded") {
    // Near the stability limit; feed an impulse then silence and let it ring.
    MonoDriver ring{SvfMode::bandpass, /*resonanceNorm=*/0.99f};
    float maxAbs = 0.0f;
    for (int n = 0; n < 200000; ++n) {
        const float in = (n == 0) ? 1.0f : 0.0f;
        const float out = ring(in);
        REQUIRE(std::isfinite(out));
        maxAbs = std::max(maxAbs, std::fabs(out));
    }
    // Self-oscillation must not blow up.
    CHECK(maxAbs < 100.0f);
}
