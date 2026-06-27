#pragma once

#include <vector>

#include <juce_audio_processors/juce_audio_processors.h>

#include "dsp/param-id.h"
#include "dsp/parameter.h"
#include "dsp/span.h"

// Host-automation parameters generated from the effect's descriptor table (T030).
// There is no hand-written parameter list: each ParameterDescriptor becomes a
// JUCE parameter (continuous -> AudioParameterFloat in normalized 0..1 space with
// a plain-unit display; discrete -> AudioParameterChoice). The normalized value
// handed to setParameter is the same one the workbench produces, so the mapping
// is identical across adapters (SC-006).

namespace acfx::plugin {

class PluginParameters {
public:
    // Create one JUCE parameter per descriptor and add it to the processor.
    void build(juce::AudioProcessor& processor, span<const ParameterDescriptor> descriptors);

    // Push each parameter's current normalized value to the effect via `fn`
    // (fn: void(ParamId, float)). A template, not std::function — so the audio
    // thread never constructs a std::function whose allocation depends on SBO.
    template <typename Fn>
    void apply(Fn&& fn) const {
        for (const Entry& e : entries_) {
            if (e.floatParam != nullptr) {
                fn(e.descriptor.id, e.floatParam->get());
            } else if (e.choiceParam != nullptr) {
                const int index = e.choiceParam->getIndex();
                const int count = e.descriptor.discreteCount < 2 ? 2 : e.descriptor.discreteCount;
                const float norm = (static_cast<float>(index) + 0.5f) / static_cast<float>(count);
                fn(e.descriptor.id, norm);
            }
        }
    }

private:
    struct Entry {
        ParameterDescriptor descriptor;
        juce::AudioParameterFloat* floatParam = nullptr;
        juce::AudioParameterChoice* choiceParam = nullptr;
    };
    std::vector<Entry> entries_;
};

} // namespace acfx::plugin
