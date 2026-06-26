### Workbench source switching races the audio callback

Finding-ID: AUDIT-BARRAGE-codex-01  
Status:     open  
Severity:   high  
Surface:    adapters/workbench/audio-source.cpp:15-25, adapters/workbench/audio-source.cpp:33-35, adapters/workbench/audio-source.cpp:68-69

`WorkbenchAudioSource` mutates `readerSource_` and `transport_` from the caller thread in `useFilePlayer()` / `useLiveInput()` while `fillBlock()` can concurrently call `transport_.getNextAudioBlock()` on the audio thread. The comment at lines 20-22 explicitly describes runtime source switching from the message thread, but there is no synchronization, audio-device stop, `AudioProcessorGraph`-style handoff, or JUCE lock around `transport_.setSource()` and `readerSource_.reset()`.

This can produce a real use-after-free or inconsistent transport state: `useFilePlayer()` replaces `readerSource_` at line 15 before installing it into the transport at line 17, and `useLiveInput()` resets it at line 34 while the audio callback may be inside line 69. Blast radius is high because an adopter exercising the advertised runtime source switch can hit crashes or audio-thread undefined behavior. A reasonable fix would serialize source changes with the audio callback using JUCE’s audio callback lock/device-manager mechanism, or stage source changes and apply them from a known non-concurrent audio lifecycle point.

### Plugin automation path constructs `std::function` on the audio thread

Finding-ID: AUDIT-BARRAGE-codex-02  
Status:     open  
Severity:   medium  
Surface:    adapters/plugin/plugin-parameters.h:21-27, adapters/plugin/plugin-processor.cpp:31-35

`PluginParameters::ApplyFn` is `std::function<void(ParamId, float)>`, and `processBlock()` passes a capturing lambda into `parameters_.apply()` every audio block. The comment at `plugin-processor.cpp:31-32` claims this path is allocation-free, but constructing a `std::function` from a lambda on the audio thread is not a hard RT-safe contract; whether it uses small-buffer storage is implementation-dependent and can change with capture shape or standard library.

The blast radius is medium: this may not fail on the current desktop build, but downstream plugin consumers rely on the audio callback being allocation-free, and the abstraction invites future edits that silently allocate in `processBlock()`. A reasonable fix is to make `apply` a templated callable or pass a concrete function object by forwarding reference, so the audio thread does not type-erase the callback.
