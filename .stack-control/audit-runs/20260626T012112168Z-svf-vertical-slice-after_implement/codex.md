### Plugin parameter apply constructs `std::function` on the audio thread

Finding-ID: AUDIT-BARRAGE-codex-01
Status:     open
Severity:   high
Surface:    adapters/plugin/plugin-parameters.h:21-29; adapters/plugin/plugin-processor.cpp:28-35

`PluginProcessor::processBlock()` calls `parameters_.apply([this](ParamId id, float normalized) { ... })` on every audio callback, but `PluginParameters::apply` takes `const ApplyFn&`, where `ApplyFn` is `std::function<void(ParamId, float)>`. That means each audio block materializes a temporary `std::function` from the lambda before entering `apply`. `std::function` construction is not guaranteed allocation-free, even when a given implementation often stores small captures inline, so the code’s line 31-32 claim that this path is “Allocation-free” is not a portable RT-safety contract.

Blast radius is high because adopters will run this in a DAW audio callback and can hit callback-time heap behavior or allocator locks in a real host. A reasonable fix is to make `apply` templated on the callable, or expose an iterator/direct loop API so `processBlock` invokes `node_.setParameter` without crossing a type-erased `std::function` boundary.

### Workbench file source can be mutated while the audio thread reads it

Finding-ID: AUDIT-BARRAGE-codex-02
Status:     open
Severity:   high
Surface:    adapters/workbench/audio-source.cpp:22-30,49-74; adapters/workbench/audio-source.h:9-18,32-47

`fillBlock()` reads `fileBuffer_` directly on the audio thread with no lock or lifetime snapshot, while `useFilePlayer()` replaces `fileBuffer_` via move assignment and then publishes `hasFile_`/`live_`. The header describes the source as “selectable at runtime” and says `fillBlock()` is lock-free, but the implementation has no double-buffer, immutable shared buffer handoff, stop-the-device boundary, or message-thread/audio-thread ownership invariant. If `useFilePlayer()` is called while audio is running, `fillBlock()` can observe the old buffer dimensions and then read from storage being reassigned or freed.

Blast radius is high because this is a real-time audio race: the most natural consumer of a runtime-selectable workbench source will wire file selection while the audio device is active, and the failure mode is undefined behavior in the audio callback. A reasonable fix is to make runtime source swaps publish an immutable buffer object atomically with lifetime ownership, or to enforce and document a hard invariant that `useFilePlayer()` only runs while audio is stopped and make the UI/device orchestration honor that invariant.

### The promised built-in file-player selection surface is absent

Finding-ID: AUDIT-BARRAGE-codex-03
Status:     open
Severity:   medium
Surface:    missing workbench UI/caller surface for adapters/workbench/audio-source.h:32-39

`WorkbenchAudioSource` exposes `useFilePlayer()` and the comments describe “a built-in looping file player OR live input device, selectable at runtime,” but this chunk provides only the source primitive. In the surrounding workbench code, `prepareToPlay()` defaults to live input when inputs exist and otherwise reports an error; there is no caller of `useFilePlayer()` and no operator-facing file selection path. That makes the “deterministic default for reproducible A/B” described in the audio-source contract unreachable through the workbench.

Blast radius is medium: live-input users can still hear the effect, but a fresh install without inputs or a user trying to run deterministic file playback will hit the error path instead of the stated feature. A reasonable fix is to add the missing workbench control/caller that selects a file and hands it to `useFilePlayer()` under a safe audio-thread ownership boundary, or narrow the contract so the file player is not presented as an implemented runtime feature.
