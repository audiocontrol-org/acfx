// Daisy adapter (T032): the libDaisy audio callback drives the SAME
// core/effects/svf source, and the Seed's ADC knobs map to setParameter. The MCU
// build uses the concrete SvfEffect directly — no ProcessorNode, no JUCE
// (Constitution IV; SC-007). Only the audio-callback + control glue is
// platform-specific; the effect is identical to the desktop targets.

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

void AudioCallback(AudioHandle::InputBuffer in, AudioHandle::OutputBuffer out, size_t size) {
    // Map the knobs to normalized parameter values (the effect denormalizes via
    // its descriptor — identical mapping to every other adapter).
    svf.setParameter(acfx::ParamId{acfx::SvfEffect::kCutoff}, hw.adc.GetFloat(kAdcCutoff));
    svf.setParameter(acfx::ParamId{acfx::SvfEffect::kResonance}, hw.adc.GetFloat(kAdcResonance));
    svf.setParameter(acfx::ParamId{acfx::SvfEffect::kMode}, hw.adc.GetFloat(kAdcMode));

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
