// Daisy adapter (T032): the libDaisy audio callback drives the SAME
// core/effects/svf source, and the Seed's ADC knobs map to setParameter. The MCU
// build uses the concrete SvfEffect directly — no ProcessorNode, no JUCE
// (Constitution IV; SC-007). Only the audio-callback + control glue is
// platform-specific; the effect is identical to the desktop targets.

#include <cstdint>

#include "daisy_seed.h"

#include "dsp/audio-block.h"
#include "dsp/param-id.h"
#include "dsp/process-context.h"
#include "effects/svf/svf-effect.h"

using namespace daisy;

namespace {

DaisySeed hw;
acfx::SvfEffect svf;

// ADC channel assignment (Seed analog pins): cutoff, resonance, mode.
enum AdcChannel { kAdcCutoff = 0, kAdcResonance = 1, kAdcMode = 2, kAdcCount };

// Last-published knob values; only re-publish past a dead-band so the effect
// recomputes coefficients when a knob actually moves, not on every block.
float lastKnob[kAdcCount] = {-1.0f, -1.0f, -1.0f};
constexpr float kKnobDeadband = 0.002f;

void maybeSet(acfx::SvfEffect::Param param, int adc) {
    const float v = hw.adc.GetFloat(adc);
    if (v < lastKnob[adc] - kKnobDeadband || v > lastKnob[adc] + kKnobDeadband) {
        lastKnob[adc] = v;
        svf.setParameter(acfx::ParamId{static_cast<std::uint8_t>(param)}, v);
    }
}

void AudioCallback(AudioHandle::InputBuffer in, AudioHandle::OutputBuffer out, size_t size) {
    // Map the knobs to normalized 0..1 values. The effect denormalizes each via
    // its descriptor — identical mapping to every other adapter. For the discrete
    // `mode` knob the descriptor quantizes 0..1 to a mode index and CLAMPS it to
    // [0, count) (verified in tests/core/parameter-test.cpp), so full knob travel
    // selects the last mode, never an out-of-range index. Dead-banded so a steady
    // knob does not recompute coefficients every block.
    maybeSet(acfx::SvfEffect::kCutoff, kAdcCutoff);
    maybeSet(acfx::SvfEffect::kResonance, kAdcResonance);
    maybeSet(acfx::SvfEffect::kMode, kAdcMode);

    // libDaisy hands non-interleaved per-channel buffers. Copy in -> out, then
    // process out in place through the shared core.
    float* channels[2] = {out[0], out[1]};
    for (size_t i = 0; i < size; ++i) {
        out[0][i] = in[0][i];
        out[1][i] = in[1][i];
    }
    acfx::AudioBlock block(channels, 2, static_cast<int>(size));
    svf.process(block);
}

} // namespace

int main() {
    hw.Configure();
    hw.Init();
    hw.SetAudioBlockSize(48);

    // Configure the three ADC knobs.
    AdcChannelConfig adcConfig[kAdcCount];
    adcConfig[kAdcCutoff].InitSingle(seed::A0);
    adcConfig[kAdcResonance].InitSingle(seed::A1);
    adcConfig[kAdcMode].InitSingle(seed::A2);
    hw.adc.Init(adcConfig, kAdcCount);
    hw.adc.Start();

    const acfx::ProcessContext ctx{static_cast<double>(hw.AudioSampleRate()),
                                   static_cast<int>(hw.AudioBlockSize()), 2};
    svf.prepare(ctx);

    hw.StartAudio(AudioCallback);
    for (;;) {
        // Parameters are sampled in the audio callback; nothing to do here.
    }
}
