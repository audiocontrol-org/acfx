#include "audio-settings.h"

// T006 (US1) — host the standard JUCE device selector in its own window.

namespace acfx::workbench {

AudioSettingsWindow::AudioSettingsWindow(juce::AudioDeviceManager& deviceManager)
    : juce::DocumentWindow("Audio Settings", juce::Colours::darkgrey,
                           juce::DocumentWindow::closeButton) {
    setUsingNativeTitleBar(true);

    // (deviceManager, minIn=0, maxIn=2, minOut=0, maxOut=2, showMidiInputs=true,
    // showMidiOutputs=false, showChannelsAsStereoPairs=true, hideAdvanced=false) — one
    // component covers device + rate/buffer + MIDI-input selection (FR-001/002/005),
    // and its own UI surfaces device-open failures (FR-009), keeping the previous
    // working device when a new one fails to open.
    auto* selector = new juce::AudioDeviceSelectorComponent(
        deviceManager, 0, 2, 0, 2, true, false, true, false);
    selector->setSize(500, 480);
    setContentOwned(selector, true); // window takes ownership and sizes to the content

    setResizable(true, false);
    centreWithSize(getWidth(), getHeight());
}

void AudioSettingsWindow::closeButtonPressed() { setVisible(false); }

} // namespace acfx::workbench
