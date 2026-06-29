#pragma once

#include <array>
#include <memory>

#include <juce_audio_processors/juce_audio_processors.h>

#include ACFX_EFFECT_HEADER
#include "plugin-parameters.h"
#include "processor-node/processor-node.h"

// The DAW plugin AudioProcessor (T029). Wraps the SAME host boundary the
// workbench uses — EffectNode<SvfEffect> — adding only JUCE plugin glue and the
// host-automation parameters generated from the effect's descriptor table. One
// processor, exported as VST3 / AU / CLAP by the build (T028).

namespace acfx::plugin {

// The concrete effect this build targets. Injected by the build via compile
// definitions (ACFX_EFFECT_TYPE / ACFX_EFFECT_HEADER) so the SAME adapter source
// builds a plugin for any type satisfying acfx::Effect — no per-effect source.
using AppEffect = ACFX_EFFECT_TYPE;

class PluginProcessor final : public juce::AudioProcessor {
public:
    PluginProcessor();
    ~PluginProcessor() override = default;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) override;
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return "Default"; }
    void changeProgramName(int, const juce::String&) override {}

    // Preset/state persistence is out of scope for this milestone (plan.md:
    // Storage N/A) — intentionally no-op, not a silent fallback.
    void getStateInformation(juce::MemoryBlock&) override {}
    void setStateInformation(const void*, int) override {}

private:
    static constexpr int kMaxChannels = 8;

    EffectNode<AppEffect> node_;
    PluginParameters parameters_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PluginProcessor)
};

} // namespace acfx::plugin
