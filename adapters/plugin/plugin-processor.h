#pragma once

#include <array>
#include <atomic>
#include <memory>

#include <juce_audio_processors/juce_audio_processors.h>

#include ACFX_EFFECT_HEADER
#include "plugin-editor.h"
#include "plugin-parameters.h"
#include "processor-node/processor-node.h"

// The DAW plugin AudioProcessor (T029). Wraps the SAME host boundary the
// workbench uses — EffectNode<SvfEffect> — adding only JUCE plugin glue and the
// host-automation parameters generated from the effect's descriptor table. One
// processor, exported as VST3 / AU / CLAP by the build (T028).
//
// Also owns the RT capture probe (T031, US5, FR-014/FR-016): processBlock() pushes
// the post-effect output into it (bounded, lock-free, RT-safe -- no analysis on
// the audio thread), and createEditor() returns a PluginEditor that drains it
// through the SAME shared acfx::analysis::LiveReadout engine the workbench uses
// (see plugin-editor.h).

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

    // MESSAGE THREAD (or the host's audio-setup thread — never the RT audio
    // callback): the RT capture probe processBlock() pushes into, and the sample
    // rate prepareToPlay() last configured. PluginEditor reads both — see
    // plugin-editor.h for why it pulls rather than being pushed to.
    HarmonicProbe& harmonicProbe() noexcept { return harmonicProbe_; }
    double currentSampleRate() const noexcept {
        return currentSampleRate_.load(std::memory_order_relaxed);
    }

private:
    static constexpr int kMaxChannels = 8;

    EffectNode<AppEffect> node_;
    PluginParameters parameters_;

    // Live harmonic readout (T031, US5, FR-014/FR-016): the RT capture probe --
    // the ONLY thing the audio thread touches (push(), in processBlock) -- plus
    // the sample rate the message-thread editor needs to (re)build the shared
    // LiveReadout. Never touched by the offline/embedded analysis engine itself.
    HarmonicProbe harmonicProbe_;
    std::atomic<double> currentSampleRate_{48000.0};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PluginProcessor)
};

} // namespace acfx::plugin
