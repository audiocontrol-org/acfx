#pragma once

#include <juce_audio_utils/juce_audio_utils.h>

#include "workbench-settings.h"

// Save/restore the workbench's audio configuration (research.md decision 3). This is
// the JUCE side of persistence — deliberately a SEPARATE translation unit from the
// JUCE-free workbench-settings serde so the tested seam stays JUCE-free. It writes the
// AudioDeviceManager state XML (devices, rate, buffer, enabled MIDI inputs) plus the
// serialized SourceConfig into a juce::ApplicationProperties user settings file (app
// name "acfx Workbench"), and reads them back on launch. A corrupt/missing file is not
// fatal: the caller restores defaults and surfaces the problem (FR-009).

namespace acfx::workbench {

class WorkbenchPersistence; // defined in T012

} // namespace acfx::workbench
