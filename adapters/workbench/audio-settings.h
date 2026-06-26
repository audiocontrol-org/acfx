#pragma once

#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_gui_basics/juce_gui_basics.h>

// A separate Audio Settings window hosting JUCE's standard
// AudioDeviceSelectorComponent — input/output device, sample rate, buffer size, and
// MIDI input selection in one tested component (research.md decision 2). It drives the
// shared AudioDeviceManager directly; its edits fire the same audio stop/start cycle
// the source lifecycle reconfigures through, so device changes are RT-safe by
// construction (FR-008). Closing the window hides it (the manager state persists).

namespace acfx::workbench {

class AudioSettingsWindow; // defined in T006

} // namespace acfx::workbench
