#pragma once

#include <functional>
#include <vector>

#include <juce_gui_basics/juce_gui_basics.h>

#include "dsp/param-id.h"
#include "dsp/parameter.h"
#include "dsp/span.h"

// Auto-renders one control per ParameterDescriptor (T023). There is no per-effect
// UI code: the view iterates the effect's parameters() table and builds a slider
// (continuous) or combo (discrete) for each, labelled from the descriptor. This
// is the same descriptor table every other adapter consumes (FR-003, SC-006).

namespace acfx::workbench {

class ParameterView : public juce::Component {
public:
    // Called when a control moves, with the normalized 0..1 value for that id.
    using OnChange = std::function<void(ParamId, float)>;

    ParameterView(span<const ParameterDescriptor> params, OnChange onChange);

    // Reflect an externally-driven change (e.g. a MIDI CC) back into the control
    // without re-firing onChange.
    void setNormalized(ParamId id, float normalized);

    void resized() override;

private:
    struct Row {
        ParameterDescriptor descriptor;
        juce::Label label;
        std::unique_ptr<juce::Slider> slider; // continuous
        std::unique_ptr<juce::ComboBox> combo; // discrete
    };

    OnChange onChange_;
    // Heap-allocated rows: Row holds a juce::Label/Component, which is non-movable,
    // so the vector stores pointers (not Rows) to satisfy reallocation.
    std::vector<std::unique_ptr<Row>> rows_;
};

} // namespace acfx::workbench
