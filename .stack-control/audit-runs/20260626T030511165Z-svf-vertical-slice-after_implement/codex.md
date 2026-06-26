### Plugin parameter changes are not persisted

Finding-ID: AUDIT-BARRAGE-codex-01
Status:     open
Severity:   high
Surface:    adapters/plugin/plugin-processor.h:43-44

`getStateInformation()` and `setStateInformation()` are hard no-ops, even though the processor exposes editable host parameters through `PluginParameters` and a `GenericAudioProcessorEditor`. A DAW user can change cutoff/resonance/mode, save the project or preset, and reload into defaults because the plugin never serializes or restores its parameter values. That is a direct correctness hit for the DAW-plugin surface; the blast radius is high because an adopter will lose session state without an obvious runtime error.

A reasonable fix is to serialize every generated parameter by stable parameter ID/name in `getStateInformation()` and restore those values in `setStateInformation()`, using the same descriptor-generated table so the plugin remains data-driven.

### File-player memory bound ignores channel count

Finding-ID: AUDIT-BARRAGE-codex-02
Status:     open
Severity:   medium
Surface:    adapters/workbench/audio-source.cpp:21-37

`useFilePlayer()` caps only `lengthInSamples` at `1 << 28`, then allocates `juce::AudioBuffer<float> decoded(numChannels, numSamples)`. `numChannels` comes from the reader and is not bounded before allocation, so a valid but high-channel file can multiply the supposedly “sane” sample cap into a very large allocation. The blast radius is medium: it is not in the audio thread, but a user-selected file can still crash or hang the workbench before the descriptive error path has a chance to help.

The guard should cap total samples as `channels * frames`, cap supported channel count, or stream/decode only the channels the workbench can actually render.

### Decode failures are published as usable audio

Finding-ID: AUDIT-BARRAGE-codex-03
Status:     open
Severity:   medium
Surface:    adapters/workbench/audio-source.cpp:36-42

The file path validates that a reader exists, then calls `reader->read(&decoded, ...)` but ignores the boolean result. If decoding fails or only partially succeeds, the code still moves `decoded` into `fileBuffer_` and publishes `hasFile_ = true`, so the audio thread will loop whatever contents the buffer has instead of surfacing the failure. The blast radius is medium because corrupt or partially readable input produces misleading audio rather than a clear setup error.

The setup path should check the `read()` return value, clear/reject partial buffers, and throw `AudioSourceError` before publishing `hasFile_`.
