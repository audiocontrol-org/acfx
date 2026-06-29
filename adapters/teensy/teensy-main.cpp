// Teensy adapter (T033/T017 embedded): a Teensy Audio Library AudioStream node
// feeds the SAME core effect source, and analog inputs map to setParameter. The
// MCU build uses the concrete effect type directly — no ProcessorNode, no JUCE
// (Constitution IV; SC-007). On a C++17 toolchain the Effect contract degrades to
// a duck-typed template (the concept is compiled out via __cpp_concepts); the
// effect source is identical to every other target.
//
// The concrete effect type/header are injected by the acfx_add_effect_teensy
// factory via compile definitions (ACFX_EFFECT_TYPE / ACFX_EFFECT_HEADER), so the
// SAME source builds a firmware for ANY effect that satisfies acfx::Effect. The
// three analog knobs bind GENERICALLY to the first three parameter indices
// (ParamId{0,1,2}); for SvfEffect those are cutoff/resonance/mode, for
// ModulatedDelayEffect they are delay_time/feedback/mix.

#include <cstddef>

#include <Arduino.h>
#include <Audio.h>

#include "dsp/audio-block.h"
#include "dsp/param-id.h"
#include "dsp/process-context.h"
#include ACFX_EFFECT_HEADER

namespace {
using AppEffect = ACFX_EFFECT_TYPE;
AppEffect effect;
constexpr float kInt16ToFloat = 1.0f / 32768.0f;
constexpr float kFloatToInt16 = 32767.0f;

// Three analog knobs bind to the first three parameter indices of whatever effect
// is compiled in. boundKnobs() = min(3, parameter count) guards effects with
// fewer than three params.
constexpr std::size_t kKnobCount = 3;
constexpr acfx::ParamId kKnobParam[kKnobCount] = {
    acfx::ParamId{0}, acfx::ParamId{1}, acfx::ParamId{2}};
constexpr std::size_t boundKnobs() {
    const std::size_t n = AppEffect::parameters().size();
    return n < kKnobCount ? n : kKnobCount;
}
} // namespace

// A single-channel effect node in the Teensy audio graph.
class AcfxEffectNode : public AudioStream {
public:
    AcfxEffectNode() : AudioStream(1, inputQueueArray_) {}

    void update() override {
        audio_block_t* block = receiveWritable(0);
        if (block == nullptr)
            return;

        float samples[AUDIO_BLOCK_SAMPLES];
        for (int i = 0; i < AUDIO_BLOCK_SAMPLES; ++i)
            samples[i] = static_cast<float>(block->data[i]) * kInt16ToFloat;

        float* channels[1] = {samples};
        acfx::AudioBlock audioBlock(channels, 1, AUDIO_BLOCK_SAMPLES);
        effect.process(audioBlock);

        for (int i = 0; i < AUDIO_BLOCK_SAMPLES; ++i) {
            float v = samples[i] * kFloatToInt16;
            if (v > 32767.0f)
                v = 32767.0f;
            if (v < -32768.0f)
                v = -32768.0f;
            block->data[i] = static_cast<int16_t>(v);
        }

        transmit(block, 0);
        release(block);
    }

private:
    audio_block_t* inputQueueArray_[1];
};

// Audio graph: line in -> effect -> line out, with the SGTL5000 codec (Audio Shield).
namespace {
AudioInputI2S audioIn;
AcfxEffectNode effectNode;
AudioOutputI2S audioOut;
AudioControlSGTL5000 codec;

AudioConnection patchIn(audioIn, 0, effectNode, 0);
AudioConnection patchOut(effectNode, 0, audioOut, 0);

constexpr int kKnobPin[kKnobCount] = {A0, A1, A2};
} // namespace

void setup() {
    AudioMemory(12);
    codec.enable();
    codec.volume(0.6f);

    const acfx::ProcessContext ctx{static_cast<double>(AUDIO_SAMPLE_RATE_EXACT),
                                   AUDIO_BLOCK_SAMPLES, 1};
    effect.prepare(ctx);
}

void loop() {
    // Sample the knobs and map to normalized 0..1 values (the effect denormalizes
    // via its descriptor — identical mapping to every adapter). Read off the audio
    // ISR, then publish the values as a group under AudioNoInterrupts so update()
    // (the audio ISR) cannot observe a partial update between them.
    float values[kKnobCount];
    for (std::size_t k = 0; k < boundKnobs(); ++k)
        values[k] = static_cast<float>(analogRead(kKnobPin[k])) / 1023.0f;

    AudioNoInterrupts();
    for (std::size_t k = 0; k < boundKnobs(); ++k)
        effect.setParameter(kKnobParam[k], values[k]);
    AudioInterrupts();
    delay(5);
}
