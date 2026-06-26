#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

// The main-window source bar: a Live/File choice plus a "Load file..." button
// (research.md decision 4). It owns no audio logic — it emits callbacks only
// (onSelectLive / onChooseFile), so the workbench decides how to apply a source
// change (always via the audio-stopped restart). The file chooser is async
// (juce::FileChooser::launchAsync) so it never blocks the message thread.

namespace acfx::workbench {

class SourceBar; // defined in T009

} // namespace acfx::workbench
