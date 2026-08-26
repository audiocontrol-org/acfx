#include "plugin-parameters.h"

#include <memory>
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

} // namespace

void PluginParameters::build(juce::AudioProcessor& processor,
                             span<const ParameterDescriptor> descriptors) {
    entries_.clear();
    entries_.reserve(descriptors.size());

    // A descriptor name of the form "Group/Label" is collected into a JUCE
    // parameter group (AU hosts like Logic render these as sections); a name
    // with no '/' is added flat, so effects that don't use the convention are
    // unaffected. Groups are emitted in first-seen order.
    std::vector<std::unique_ptr<juce::AudioProcessorParameterGroup>> groups;
    auto groupFor = [&groups](const juce::String& id)
        -> juce::AudioProcessorParameterGroup* {
        for (auto& g : groups)
            if (g->getID() == id)
                return g.get();
        groups.push_back(std::make_unique<juce::AudioProcessorParameterGroup>(id, id, "|"));
        return groups.back().get();
    };

    for (const ParameterDescriptor& d : descriptors) {
        Entry entry;
        entry.descriptor = d;

        const juce::String full(std::string(d.name));
        const int          slash     = full.indexOfChar('/');
        const juce::String groupName = slash >= 0 ? full.substring(0, slash) : juce::String();
        const juce::String label     = slash >= 0 ? full.substring(slash + 1) : full;
        // Leaf id: unique + stable, with the separator flattened out.
        const juce::ParameterID paramId(full.replaceCharacter('/', '_'), 1);

        std::unique_ptr<juce::AudioProcessorParameter> param;
        if (d.kind == ParamKind::discrete) {
            juce::StringArray choices;
            for (int i = 0; i < d.discreteCount; ++i)
                choices.add(juce::String(std::string(d.choices[i])));
            const int defaultIndex = static_cast<int>(d.defaultValue);
            auto p = std::make_unique<juce::AudioParameterChoice>(paramId, label, choices,
                                                                  defaultIndex);
            entry.choiceParam = p.get();
            param = std::move(p);
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
            auto p = std::make_unique<juce::AudioParameterFloat>(
                paramId, label, juce::NormalisableRange<float>(0.0f, 1.0f), defaultNorm,
                attributes);
            entry.floatParam = p.get();
            param = std::move(p);
        }

        if (groupName.isNotEmpty())
            groupFor(groupName)->addChild(std::move(param));
        else
            processor.addParameter(param.release());

        entries_.push_back(entry);
    }

    for (auto& g : groups)
        processor.addParameterGroup(std::move(g));
}

} // namespace acfx::plugin
