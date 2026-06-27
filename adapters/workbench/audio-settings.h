#pragma once

#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_gui_basics/juce_gui_basics.h>

// A separate Audio Settings window hosting JUCE's standard
// AudioDeviceSelectorComponent — input/output device, sample rate, buffer size, and
// MIDI input selection in one tested component (research.md decision 2). It drives the
// shared AudioDeviceManager directly; its edits fire the same audio stop/start cycle
// the source lifecycle reconfigures through, so device changes are RT-safe by
// construction (FR-008). Closing the window hides it (the manager state persists), so
// it lives off the main window without cluttering the sketch-and-hear controls (FR-010).

namespace acfx::workbench {

class AudioSettingsWindow final : public juce::DocumentWindow {
public:
    explicit AudioSettingsWindow(juce::AudioDeviceManager& deviceManager);

    // Hide rather than destroy on close — the workbench owns this window's lifetime
    // and reopens it instantly with the device-manager state intact.
    void closeButtonPressed() override;

private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AudioSettingsWindow)
};

} // namespace acfx::workbench
