// Teensy adapter (T033): a Teensy Audio Library AudioStream node feeds the SAME
// core/effects/svf source, and analog inputs map to setParameter. The MCU build
// uses the concrete SvfEffect directly — no ProcessorNode, no JUCE (Constitution
// IV; SC-007). On a C++17 toolchain the Effect contract degrades to a duck-typed
// template (the concept is compiled out via __cpp_concepts); the effect source is
// identical to every other target.

#include <Arduino.h>
#include <Audio.h>

#include "dsp/audio-block.h"
#include "dsp/param-id.h"
#include "dsp/process-context.h"
#include "effects/svf/svf-effect.h"

namespace {
acfx::SvfEffect svf;
constexpr float kInt16ToFloat = 1.0f / 32768.0f;
constexpr float kFloatToInt16 = 32767.0f;
} // namespace

// A single-channel SVF node in the Teensy audio graph.
class AcfxSvfNode : public AudioStream {
public:
    AcfxSvfNode() : AudioStream(1, inputQueueArray_) {}

    void update() override {
        audio_block_t* block = receiveWritable(0);
        if (block == nullptr)
            return;

        float samples[AUDIO_BLOCK_SAMPLES];
        for (int i = 0; i < AUDIO_BLOCK_SAMPLES; ++i)
            samples[i] = static_cast<float>(block->data[i]) * kInt16ToFloat;

        float* channels[1] = {samples};
        acfx::AudioBlock audioBlock(channels, 1, AUDIO_BLOCK_SAMPLES);
        svf.process(audioBlock);

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

// Audio graph: line in -> SVF -> line out, with the SGTL5000 codec (Audio Shield).
namespace {
AudioInputI2S audioIn;
AcfxSvfNode svfNode;
AudioOutputI2S audioOut;
AudioControlSGTL5000 codec;

AudioConnection patchIn(audioIn, 0, svfNode, 0);
AudioConnection patchOut(svfNode, 0, audioOut, 0);

constexpr int kCutoffPin = A0;
constexpr int kResonancePin = A1;
constexpr int kModePin = A2;
} // namespace

void setup() {
    AudioMemory(12);
    codec.enable();
    codec.volume(0.6f);

    const acfx::ProcessContext ctx{static_cast<double>(AUDIO_SAMPLE_RATE_EXACT),
                                   AUDIO_BLOCK_SAMPLES, 1};
    svf.prepare(ctx);
}

void loop() {
    // Sample the knobs and map to normalized parameter values (the effect
    // denormalizes via its descriptor — identical mapping to every adapter).
    svf.setParameter(acfx::ParamId{acfx::SvfEffect::kCutoff},
                     static_cast<float>(analogRead(kCutoffPin)) / 1023.0f);
    svf.setParameter(acfx::ParamId{acfx::SvfEffect::kResonance},
                     static_cast<float>(analogRead(kResonancePin)) / 1023.0f);
    svf.setParameter(acfx::ParamId{acfx::SvfEffect::kMode},
                     static_cast<float>(analogRead(kModePin)) / 1023.0f);
    delay(5);
}
