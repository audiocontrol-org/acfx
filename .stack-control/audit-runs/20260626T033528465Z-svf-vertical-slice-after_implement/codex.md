### Workbench accepts failed audio decodes as playable buffers

Finding-ID: AUDIT-BARRAGE-codex-01
Status:     open
Severity:   medium
Surface:    adapters/workbench/audio-source.cpp:34-42

`useFilePlayer()` validates that a reader exists, then calls `reader->read(&decoded, 0, numSamples, 0, true, true)` without checking the boolean result. If a malformed/truncated file opens but decode fails or only partially succeeds, the code still publishes `fileBuffer_`, sets `hasFile_ = true`, and the audio thread will loop whatever contents JUCE left in the buffer.

The feature contract in `audio-source.h` says unavailable audio should raise a descriptive error, never silently produce mock/zero audio. The blast radius is medium: users hit it with bad source files and get misleading playback instead of a setup-time failure. A reasonable fix is to check the return value, reject failed reads with `AudioSourceError`, and ideally verify the decoded buffer length/channel assumptions before publishing it.

### Host parameter identity is coupled to display labels instead of stable descriptor IDs

Finding-ID: AUDIT-BARRAGE-codex-02
Status:     open
Severity:   medium
Surface:    adapters/plugin/plugin-parameters.cpp:43-48

`PluginParameters::build()` constructs the JUCE `ParameterID` from `d.name`, the same string used as the display name. The descriptor already carries a stable `ParamId`, but that identity is ignored for the host-facing parameter ID. As written, renaming a label, changing capitalization, localizing names, or adding another effect with duplicate labels would change or collide with DAW automation/state identity.

The blast radius is medium because current SVF labels are unique, but the plugin automation surface becomes fragile exactly where host compatibility depends on stable IDs. A reasonable fix is to derive `juce::ParameterID` from a stable machine ID, such as `ParamId` plus an effect namespace, while keeping `d.name` only as the display label.
