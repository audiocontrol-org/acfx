// Daisy adapter (T032/T010 embedded): the libDaisy audio callback drives the SAME
// core effect source, and the Seed's ADC knobs map to setParameter. The MCU build
// uses the concrete effect type directly — no ProcessorNode, no JUCE
// (Constitution IV; SC-007). Only the audio-callback + control glue is
// platform-specific; the effect is identical to the desktop targets.
//
// The concrete effect type/header are injected by the acfx_add_effect_daisy
// factory via compile definitions (ACFX_EFFECT_TYPE / ACFX_EFFECT_HEADER), so the
// SAME source builds a firmware for ANY effect that satisfies acfx::Effect. The
// three ADC knobs bind GENERICALLY to the first three parameter indices
// (ParamId{0,1,2}); for SvfEffect those are cutoff/resonance/mode, for
// ModulatedDelayEffect they are delay_time/feedback/mix.

#include <cstddef>
#include <cstdint>

#include "daisy_seed.h"

#include "dsp/audio-block.h"
#include "dsp/param-id.h"
#include "dsp/process-context.h"
#include ACFX_EFFECT_HEADER

using namespace daisy;

namespace {

using AppEffect = ACFX_EFFECT_TYPE;

DaisySeed hw;
AppEffect effect;

// Three ADC knobs (Seed analog pins A0/A1/A2) bind to the first three parameter
// indices of whatever effect is compiled in.
constexpr std::size_t kAdcCount = 3;
constexpr acfx::ParamId kAdcParam[kAdcCount] = {
    acfx::ParamId{0}, acfx::ParamId{1}, acfx::ParamId{2}};

// Number of knobs actually wired = min(3, parameter count). Effects with fewer
// than three params leave the unused knobs unbound (no out-of-range setParameter).
constexpr std::size_t boundKnobs() {
    const std::size_t n = AppEffect::parameters().size();
    return n < kAdcCount ? n : kAdcCount;
}

// Last-published knob values; only re-publish past a dead-band so the effect
// recomputes coefficients when a knob actually moves, not on every block.
float lastKnob[kAdcCount] = {-1.0f, -1.0f, -1.0f};
constexpr float kKnobDeadband = 0.002f;

void maybeSet(std::size_t knob) {
    const float v = hw.adc.GetFloat(static_cast<int>(knob));
    if (v < lastKnob[knob] - kKnobDeadband || v > lastKnob[knob] + kKnobDeadband) {
        lastKnob[knob] = v;
        effect.setParameter(kAdcParam[knob], v);
    }
}

void AudioCallback(AudioHandle::InputBuffer in, AudioHandle::OutputBuffer out, size_t size) {
    // Map the knobs to normalized 0..1 values. The effect denormalizes each via
    // its descriptor — identical mapping to every other adapter. Dead-banded so a
    // steady knob does not recompute coefficients every block.
    for (std::size_t k = 0; k < boundKnobs(); ++k)
        maybeSet(k);

    // libDaisy hands non-interleaved per-channel buffers. Copy in -> out, then
    // process out in place through the shared core.
    float* channels[2] = {out[0], out[1]};
    for (size_t i = 0; i < size; ++i) {
        out[0][i] = in[0][i];
        out[1][i] = in[1][i];
    }
    acfx::AudioBlock block(channels, 2, static_cast<int>(size));
    effect.process(block);
}

} // namespace

int main() {
    hw.Configure();
    hw.Init();
    hw.SetAudioBlockSize(48);

    // Configure the three ADC knobs (A0/A1/A2 -> first three param indices).
    AdcChannelConfig adcConfig[kAdcCount];
    adcConfig[0].InitSingle(seed::A0);
    adcConfig[1].InitSingle(seed::A1);
    adcConfig[2].InitSingle(seed::A2);
    hw.adc.Init(adcConfig, kAdcCount);
    hw.adc.Start();

    const acfx::ProcessContext ctx{static_cast<double>(hw.AudioSampleRate()),
                                   static_cast<int>(hw.AudioBlockSize()), 2};
    effect.prepare(ctx);

    hw.StartAudio(AudioCallback);
    for (;;) {
        // Parameters are sampled in the audio callback; nothing to do here.
    }
}
