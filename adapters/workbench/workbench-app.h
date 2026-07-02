#pragma once

#include <atomic>
#include <memory>

#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include "audio-settings.h"
#include "audio-source.h"
#include "harmonic-readout.h"
#include "level-meter.h"
#include "midi-binding.h"
#include "parameter-view.h"
#include "processor-node/processor-node.h"
#include "source-bar.h"
#include "workbench-persistence.h"
#include "workbench-settings.h"

// Declaration of the desktop sketch-and-hear workbench component (T022, T026,
// T030). See workbench-app.cpp for the implementation and design notes. Split
// into a header so workbench-main.cpp (the JUCE app bootstrap) can construct
// a WorkbenchComponent without duplicating its definition.

namespace acfx::workbench {

class WorkbenchComponent final : public juce::AudioAppComponent,
                                 private juce::MidiInputCallback,
                                 private juce::ChangeListener {
public:
    WorkbenchComponent();
    ~WorkbenchComponent() override;

    void prepareToPlay(int blockSize, double sampleRate) override;
    void getNextAudioBlock(const juce::AudioSourceChannelInfo& info) override;
    void releaseResources() override;
    void resized() override;

private:
    int numInputChannels() const;
    int numOutputChannels() const;

    // Restart with the audio callback STOPPED; prepareToPlay reconfigures the source
    // from the updated message-thread state (FR-008). Message-thread only.
    void restartAudio();

    // Open (creating on first use) the Audio Settings window.
    void showAudioSettings();

    // The current source selection as the persistable SourceConfig.
    SourceConfig currentSourceConfig() const;

    // True when a saved preferred device (output or input) is not in the current
    // device list (AUDIT-20260627-01).
    bool preferredDeviceUnavailable() const;

    // Capture the live device state as the new preferred snapshot.
    void refreshPreferredSnapshot();

    void saveSettings();

    // Device-manager changes (device/rate/buffer/MIDI edits via the selector) persist
    // the new configuration (FR-006).
    void changeListenerCallback(juce::ChangeBroadcaster*) override;

    // Surface (never swallow) startup problems (FR-009; AUDIT-20260627-02).
    void surfaceStartupIssues(bool corrupt);

    void handleIncomingMidiMessage(juce::MidiInput*, const juce::MidiMessage& msg) override;

    std::unique_ptr<ProcessorNode> node_;
    span<const ParameterDescriptor> params_;
    ParameterView paramView_;
    MidiBinding midi_;
    WorkbenchAudioSource source_;
    juce::ToggleButton abToggle_;
    juce::TextButton audioSettingsButton_;
    SourceBar sourceBar_;
    LevelMeter meter_;

    // Live harmonic readout (T030, US5, FR-014/FR-016). harmonicProbe_ is the RT
    // capture probe -- the ONLY thing the audio thread touches (push(), in
    // getNextAudioBlock); it must outlive harmonicReadout_, which holds a
    // reference to it, hence its declaration first (member init order = declaration
    // order). harmonicReadout_ owns the shared acfx::analysis::LiveReadout and all
    // of the message-thread drain/display logic.
    HarmonicProbe harmonicProbe_;
    HarmonicReadout harmonicReadout_;

    std::unique_ptr<AudioSettingsWindow> audioSettings_;
    WorkbenchPersistence persistence_;

    // The saved device preference, kept so a temporarily-unavailable device is not
    // clobbered by the fallback on save (AUDIT-20260627-01/02). Refreshed to the live
    // state whenever a non-fallback save happens.
    juce::String preferredOutputDevice_;
    juce::String preferredInputDevice_;
    std::unique_ptr<juce::XmlElement> preferredDeviceState_;

    // A saved/seeded file source whose file was missing at launch — surfaced once at
    // startup; the source falls back to live (AUDIT-20260627-09).
    juce::String missingSavedFile_;

    // Current source selection, owned on the message thread and read by prepareToPlay
    // (the single reconfigure point). The source bar (T010) mutates these and then
    // calls restartAudio() to apply the change with the callback stopped.
    SourceMode mode_ = SourceMode::live;
    juce::File sourceFile_;

    int preparedChannels_ = 2;
    bool sourceReady_ = false;
    std::atomic<bool> processed_{true};
};

} // namespace acfx::workbench
