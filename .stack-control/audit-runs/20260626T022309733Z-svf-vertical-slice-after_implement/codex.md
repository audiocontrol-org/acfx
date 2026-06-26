### Source recovery never resets failure state after a transient prepare error

Finding-ID: AUDIT-BARRAGE-codex-01
Status:     open
Severity:   medium
Surface:    adapters/workbench/workbench-app.cpp:71-94

`prepareToPlay()` sets `sourceReady_ = false` only in the exception path, but it never clears/releases the prior `WorkbenchAudioSource` before attempting a new source selection. If an audio-device restart or sample-rate change happens after a failed `ACFX_WORKBENCH_FILE` load, the old source object can remain in whatever partially configured state `useFilePlayer()` left behind, while future successful prepares depend on that object recovering implicitly.

The blast radius is medium: this does not break the happy path, but desktop audio devices are routinely restarted by JUCE, and the workbench is explicitly meant for sketch-and-hear/manual A/B. A reasonable fix is to reset the source at the start of `prepareToPlay()` or ensure every `use*` path is strongly exception-safe, then only flip `sourceReady_` true after a fully successful selection and prepare.

### MIDI CC can be bound outside the stated 0..127 contract

Finding-ID: AUDIT-BARRAGE-codex-02
Status:     open
Severity:   low
Surface:    adapters/workbench/midi-binding.h:20-36

`MidiBinding::bind()` documents “Bind a CC number (0..127)” but accepts any `int` and stores it directly in `bindings_`. `handle()` only ever looks up `msg.getControllerNumber()`, which is a valid MIDI CC, so invalid bindings silently become unreachable configuration rather than being rejected.

The blast radius is low because current in-tree bindings use valid constants, but this is a small operator-discipline trap on a reusable boundary. A reasonable fix is to reject or clamp invalid CC numbers, preferably returning `bool` from `bind()` or asserting in debug builds so bad MIDI maps fail visibly.
