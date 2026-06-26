#include "plugin-parameters.h"

#include <string>

namespace acfx::plugin {

namespace {

juce::String unitSuffix(ParamUnit unit) {
    switch (unit) {
    case ParamUnit::hz:
        return " Hz";
    case ParamUnit::decibels:
        return " dB";
    case ParamUnit::percent:
        return " %";
    case ParamUnit::ratio:
    case ParamUnit::none:
    default:
        return {};
    }
}

juce::String modeName(int index) {
    switch (index) {
    case 1:
        return "highpass";
    case 2:
        return "bandpass";
    case 0:
    default:
        return "lowpass";
    }
}

} // namespace

void PluginParameters::build(juce::AudioProcessor& processor,
                             span<const ParameterDescriptor> descriptors) {
    entries_.clear();
    entries_.reserve(descriptors.size());

    for (const ParameterDescriptor& d : descriptors) {
        Entry entry;
        entry.descriptor = d;
        const juce::String name(std::string(d.name));
        const juce::ParameterID paramId(name, 1);

        if (d.kind == ParamKind::discrete) {
            juce::StringArray choices;
            for (int i = 0; i < d.discreteCount; ++i)
                choices.add(modeName(i));
            const int defaultIndex = static_cast<int>(d.defaultValue);
            auto param = std::make_unique<juce::AudioParameterChoice>(paramId, name, choices,
                                                                      defaultIndex);
            entry.choiceParam = param.get();
            processor.addParameter(param.release());
        } else {
            // Normalized 0..1 automation; the descriptor owns the skew, so the
            // displayed plain value uses denormalize() — matching the workbench.
            const ParameterDescriptor desc = d;
            const float defaultNorm = normalize(d, d.defaultValue);
            auto attributes =
                juce::AudioParameterFloatAttributes()
                    .withLabel(unitSuffix(d.unit))
                    .withStringFromValueFunction([desc](float norm, int) {
                        const float plain = denormalize(desc, norm);
                        return juce::String(plain, 2);
                    });
            auto param = std::make_unique<juce::AudioParameterFloat>(
                paramId, name, juce::NormalisableRange<float>(0.0f, 1.0f), defaultNorm,
                attributes);
            entry.floatParam = param.get();
            processor.addParameter(param.release());
        }

        entries_.push_back(entry);
    }
}

} // namespace acfx::plugin
