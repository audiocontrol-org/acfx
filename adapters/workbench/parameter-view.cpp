#include "parameter-view.h"

namespace acfx::workbench {

ParameterView::ParameterView(span<const ParameterDescriptor> params, OnChange onChange)
    : onChange_(std::move(onChange)) {
    rows_.reserve(params.size());
    for (const ParameterDescriptor& d : params) {
        rows_.emplace_back();
        Row& row = rows_.back();
        row.descriptor = d;

        row.label.setText(juce::String(std::string(d.name)), juce::dontSendNotification);
        addAndMakeVisible(row.label);

        if (d.kind == ParamKind::discrete) {
            row.combo = std::make_unique<juce::ComboBox>();
            for (int i = 0; i < d.discreteCount; ++i)
                row.combo->addItem(juce::String(i), i + 1); // item ids are 1-based
            row.combo->setSelectedItemIndex(0, juce::dontSendNotification);

            const ParamId id = d.id;
            const std::uint8_t count = d.discreteCount;
            juce::ComboBox* combo = row.combo.get();
            OnChange& cb = onChange_;
            combo->onChange = [combo, id, count, &cb] {
                const int index = combo->getSelectedItemIndex();
                // Centre-of-bucket normalized value for this discrete index.
                const float norm = (static_cast<float>(index) + 0.5f) / static_cast<float>(count);
                if (cb)
                    cb(id, norm);
            };
            addAndMakeVisible(*row.combo);
        } else {
            row.slider = std::make_unique<juce::Slider>(juce::Slider::LinearHorizontal,
                                                        juce::Slider::TextBoxRight);
            // The slider works in normalized 0..1 space; the descriptor owns the
            // skew, so the effect maps back to plain units on setParameter.
            row.slider->setRange(0.0, 1.0, 0.0);
            row.slider->setValue(static_cast<double>(normalize(d, d.defaultValue)),
                                 juce::dontSendNotification);

            const ParameterDescriptor desc = d;
            juce::Slider* slider = row.slider.get();
            OnChange& cb = onChange_;
            slider->onValueChange = [slider, desc, &cb] {
                if (cb)
                    cb(desc.id, static_cast<float>(slider->getValue()));
            };
            addAndMakeVisible(*row.slider);
        }
    }
}

void ParameterView::setNormalized(ParamId id, float normalized) {
    for (Row& row : rows_) {
        if (row.descriptor.id != id)
            continue;
        if (row.slider)
            row.slider->setValue(static_cast<double>(normalized), juce::dontSendNotification);
        else if (row.combo) {
            const int count = row.descriptor.discreteCount < 2 ? 2 : row.descriptor.discreteCount;
            int index = static_cast<int>(normalized * static_cast<float>(count));
            if (index >= count)
                index = count - 1;
            row.combo->setSelectedItemIndex(index, juce::dontSendNotification);
        }
        return;
    }
}

void ParameterView::resized() {
    auto area = getLocalBounds().reduced(8);
    const int rowHeight = 32;
    for (Row& row : rows_) {
        auto r = area.removeFromTop(rowHeight);
        row.label.setBounds(r.removeFromLeft(120));
        if (row.slider)
            row.slider->setBounds(r);
        else if (row.combo)
            row.combo->setBounds(r.removeFromLeft(160));
        area.removeFromTop(6);
    }
}

} // namespace acfx::workbench
