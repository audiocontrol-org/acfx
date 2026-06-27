#pragma once

#include <memory>

#include <juce_audio_utils/juce_audio_utils.h>

#include "workbench-settings.h"

// Save/restore the workbench's audio configuration (research.md decision 3). This is
// the JUCE side of persistence — deliberately a SEPARATE translation unit from the
// JUCE-free workbench-settings serde so the tested seam stays JUCE-free. It writes the
// AudioDeviceManager state XML (devices, rate, buffer, enabled MIDI inputs) plus the
// serialized SourceConfig into a juce::ApplicationProperties user settings file (app
// name "acfx Workbench"), and reads them back on launch. A corrupt/missing file is not
// fatal: load() reports it via LoadedSettings::corrupt so the caller restores defaults
// and surfaces the problem (FR-009), never crashing startup.

namespace acfx::workbench {

struct LoadedSettings {
    // The saved AudioDeviceManager state, or null when absent/unreadable. Pass to
    // AudioAppComponent::setAudioChannels(in, out, deviceState) to restore devices.
    std::unique_ptr<juce::XmlElement> deviceState;
    // The saved source selection, or the safe default (live) when absent/garbage.
    SourceConfig source;
    // True when a settings file existed but could not be read as valid settings — the
    // caller starts with defaults AND surfaces the problem (does not fail silently).
    bool corrupt = false;
};

class WorkbenchPersistence {
public:
    WorkbenchPersistence();

    // Load the saved settings (never throws; a missing file yields defaults with
    // corrupt == false, a present-but-unreadable file yields defaults with corrupt ==
    // true).
    LoadedSettings load();

    // Persist the current device-manager state + source selection. Safe to call on
    // every change and on quit.
    void save(const juce::AudioDeviceManager& deviceManager, const SourceConfig& source);

    // Persist a SPECIFIC device-state XML + source selection, rather than the live
    // device state. Used to preserve a saved device preference while that preferred
    // device is temporarily unavailable, so a fallback session does not clobber it
    // (AUDIT-20260627-01). Pass nullptr to clear the device-state block.
    void savePreserving(const juce::XmlElement* deviceState, const SourceConfig& source);

private:
    void writeSettings(const juce::XmlElement* deviceState, const SourceConfig& source);

    juce::ApplicationProperties applicationProperties_;

    JUCE_DECLARE_NON_COPYABLE(WorkbenchPersistence)
};

} // namespace acfx::workbench
