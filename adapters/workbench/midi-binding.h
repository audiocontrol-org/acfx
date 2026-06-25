#pragma once

#include <functional>
#include <unordered_map>

#include <juce_audio_basics/juce_audio_basics.h>

#include "dsp/param-id.h"

// Maps MIDI continuous-controller messages to normalized parameter changes
// (T024). A CC value 0..127 becomes a normalized 0..1 value handed to
// setParameter(id, normalized) — the effect denormalizes via its descriptor, so
// the binding stays parameter-agnostic.

namespace acfx::workbench {

class MidiBinding {
public:
    using OnParam = std::function<void(ParamId, float)>;

    // Bind a CC number (0..127) to a parameter id. Re-binding a CC replaces it.
    void bind(int ccNumber, ParamId id) { bindings_[ccNumber] = id; }

    // Feed a MIDI message; if it is a CC we have a binding for, invokes onParam
    // with the normalized value and returns true.
    bool handle(const juce::MidiMessage& msg, const OnParam& onParam) const {
        if (!msg.isController())
            return false;
        const auto it = bindings_.find(msg.getControllerNumber());
        if (it == bindings_.end())
            return false;
        const float normalized = static_cast<float>(msg.getControllerValue()) / 127.0f;
        if (onParam)
            onParam(it->second, normalized);
        return true;
    }

private:
    std::unordered_map<int, ParamId> bindings_;
};

} // namespace acfx::workbench
