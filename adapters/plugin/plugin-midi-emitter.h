#pragma once

#include <array>
#include <cmath>
#include <functional>
#include <memory>

#include <juce_audio_utils/juce_audio_utils.h>

#include "dsp/param-id.h"
#include "midi-cc-map.h" // acfx::nucleo::kCcBindings (shared with the firmware)

// Emits a MIDI CC whenever a plugin parameter changes, so the plugin UI can
// drive the hardware device. It opens the board's CoreMIDI port DIRECTLY
// (auto-connect by name) plus a virtual port (fallback you can patch in your MIDI
// setup) -- bypassing DAW MIDI routing, which most hosts (Logic especially) do
// not expose for audio-effect plugins. The param-index -> CC map is the inverse
// of the firmware's CC table (midi-cc-map.h), so the board interprets each CC as
// the same parameter. Message-thread only (a Timer); never touches the audio
// thread, so it stays RT-safe.

namespace acfx::plugin {

class MidiCcEmitter final : private juce::Timer {
public:
    // `poll` invokes its callback once per parameter with (paramIndex, normalized
    // 0..1). `name` is used for the virtual port. Pass PluginParameters::apply.
    using PollFn = std::function<void(const std::function<void(int, float)>&)>;

    MidiCcEmitter(PollFn poll, const juce::String& name)
        : poll_(std::move(poll)) {
        paramToCc_.fill(-1);
        for (const acfx::nucleo::CcBinding& b : acfx::nucleo::kCcBindings)
            if (b.paramIndex < paramToCc_.size())
                paramToCc_[b.paramIndex] = static_cast<int>(b.cc);
        lastCc_.fill(-1);
        virtualOut_ = juce::MidiOutput::createNewDevice(name);
        tryOpenDevice();
        startTimerHz(50);
    }

    ~MidiCcEmitter() override { stopTimer(); }

private:
    void tryOpenDevice() {
        for (const juce::MidiDeviceInfo& d : juce::MidiOutput::getAvailableDevices()) {
            if (d.name.containsIgnoreCase("acfx") || d.name.containsIgnoreCase("nucleo")) {
                deviceOut_ = juce::MidiOutput::openDevice(d.identifier);
                if (deviceOut_ != nullptr) { lastCc_.fill(-1); return; }  // re-broadcast full state to the board
            }
        }
    }

    void send(int cc, int value) {
        const juce::MidiMessage m = juce::MidiMessage::controllerEvent(1, cc, value);
        if (deviceOut_  != nullptr) deviceOut_->sendMessageNow(m);
        if (virtualOut_ != nullptr) virtualOut_->sendMessageNow(m);
    }

    void timerCallback() override {
        if (++ticks_ < 50) return;   // ~1 s settle before the first broadcast
        if (deviceOut_ == nullptr && (retry_++ % 100) == 0) tryOpenDevice(); // re-scan ~2 s
        if (deviceOut_ == nullptr && virtualOut_ == nullptr) return;
        poll_([this](int idx, float norm) {
            if (idx < 0 || idx >= static_cast<int>(paramToCc_.size())) return;
            const int cc = paramToCc_[static_cast<std::size_t>(idx)];
            if (cc < 0) return;
            const float clamped = norm < 0.0f ? 0.0f : (norm > 1.0f ? 1.0f : norm);
            const int v = static_cast<int>(std::lround(clamped * 127.0f));
            if (v != lastCc_[static_cast<std::size_t>(idx)]) {
                lastCc_[static_cast<std::size_t>(idx)] = v;
                send(cc, v);
            }
        });
    }

    PollFn poll_;
    std::unique_ptr<juce::MidiOutput> deviceOut_, virtualOut_;
    std::array<int, 128> paramToCc_{};
    std::array<int, 128> lastCc_{};
    int retry_ = 0;
    int ticks_ = 0;
};

} // namespace acfx::plugin
