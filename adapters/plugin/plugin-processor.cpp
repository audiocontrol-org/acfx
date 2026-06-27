#include "plugin-processor.h"

#include "dsp/audio-block.h"
#include "dsp/process-context.h"

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
}

juce::AudioProcessorEditor* PluginProcessor::createEditor() {
    // Auto-generated UI from the host-automation parameters — no bespoke editor.
    return new juce::GenericAudioProcessorEditor(*this);
}

} // namespace acfx::plugin

// The plugin factory JUCE's wrappers call to instantiate the processor.
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() {
    return new acfx::plugin::PluginProcessor();
}
