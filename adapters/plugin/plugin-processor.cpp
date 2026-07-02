#include "plugin-processor.h"

#include "dsp/audio-block.h"
#include "dsp/process-context.h"
#include "dsp/span.h"

namespace acfx::plugin {

PluginProcessor::PluginProcessor()
    : juce::AudioProcessor(BusesProperties()
                               .withInput("Input", juce::AudioChannelSet::stereo(), true)
                               .withOutput("Output", juce::AudioChannelSet::stereo(), true)) {
    parameters_.build(*this, node_.parameters());
}

void PluginProcessor::prepareToPlay(double sampleRate, int samplesPerBlock) {
    const ProcessContext ctx{sampleRate, samplesPerBlock, getTotalNumOutputChannels()};
    node_.prepare(ctx);

    // (Re)configure the shared live harmonic readout for the host's actual sample
    // rate (T031, US5, FR-014/FR-016). This call itself never touches the audio
    // path or the analysis engine -- it just publishes the rate; PluginEditor's
    // message-thread timer picks it up and rebuilds the shared LiveReadout (see
    // plugin-editor.h for why the editor pulls rather than being pushed to).
    currentSampleRate_.store(sampleRate, std::memory_order_relaxed);
}

bool PluginProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const {
    const auto out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo())
        return false;
    // Same in/out layout (in-place processing).
    return layouts.getMainInputChannelSet() == out;
}

void PluginProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) {
    juce::ScopedNoDenormals noDenormals;

    // Push current host-automation values to the effect (same normalized mapping
    // as the workbench). Allocation-free.
    parameters_.apply([this](ParamId id, float normalized) {
        node_.setParameter(id, normalized);
    });

    const int numChannels = juce::jmin(buffer.getNumChannels(), kMaxChannels);
    std::array<float*, kMaxChannels> chans{};
    for (int ch = 0; ch < numChannels; ++ch)
        chans[ch] = buffer.getWritePointer(ch);

    AudioBlock block(chans.data(), numChannels, buffer.getNumSamples());
    node_.processBlock(block);

    // Harmonic readout (T031, US5, FR-014/FR-016): a bounded, lock-free, RT-safe
    // handoff of the POST-effect output just computed above -- one representative
    // channel (channel 0), no analysis on this thread (FR-016). See
    // plugin-editor.h/PluginEditor::timerCallback for the drain side.
    if (numChannels > 0) {
        harmonicProbe_.push(
            acfx::span<const float>(buffer.getReadPointer(0),
                                     static_cast<std::size_t>(buffer.getNumSamples())));
    }
}

juce::AudioProcessorEditor* PluginProcessor::createEditor() {
    // The live harmonic readout editor (T031, US5, FR-014/FR-016): drains the RT
    // capture probe above through the SAME shared acfx::analysis::LiveReadout
    // engine the workbench uses (host/analysis/live-readout.h). Replaces the prior
    // auto-generated GenericAudioProcessorEditor -- host-automation parameters
    // still exist and remain host-automatable even though this editor doesn't
    // expose bespoke controls for them (JUCE's generic host-parameter UI, e.g. a
    // DAW's own parameter list, is unaffected by which editor Component is shown).
    return new PluginEditor(*this);
}

} // namespace acfx::plugin

// The plugin factory JUCE's wrappers call to instantiate the processor.
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() {
    return new acfx::plugin::PluginProcessor();
}
