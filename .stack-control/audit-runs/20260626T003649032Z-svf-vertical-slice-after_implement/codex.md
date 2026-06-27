### MIDI inputs are registered but never enabled

Finding-ID: AUDIT-BARRAGE-codex-01
Status:     open
Severity:   high
Surface:    adapters/workbench/workbench-app.cpp:37-46, adapters/workbench/workbench-app.cpp:110-122

The workbench advertises default MIDI CC bindings, and `handleIncomingMidiMessage()` is implemented, but the constructor only calls `deviceManager.addMidiInputDeviceCallback({}, this)` after `setAudioChannels(2, 2)`. In JUCE, adding a MIDI callback does not by itself enable any MIDI input devices; inputs generally need to be enumerated and enabled with `setMidiInputDeviceEnabled(...)` or selected through an audio/MIDI settings UI. On a fresh install, the callback can therefore remain inert even though the app claims MIDI CC control is wired.

Blast radius is high because a consumer running the workbench as written will reasonably expect CC 74/71 to control cutoff/resonance, but the feature silently does not work on the default path. A reasonable fix is to either enable available MIDI inputs explicitly during startup or provide a settings surface that enables/selects them before relying on the callback.

### Source failures degrade to silent output with no surfaced error

Finding-ID: AUDIT-BARRAGE-codex-02
Status:     open
Severity:   medium
Surface:    adapters/workbench/workbench-app.cpp:53-65, adapters/workbench/workbench-app.cpp:80-85, adapters/workbench/workbench-app.cpp:130

`prepareToPlay()` catches `AudioSourceError` and stores it in `lastSourceError_`, but that field is never rendered or otherwise reported. Then `getNextAudioBlock()` catches later `AudioSourceError`s, clears the buffer, and returns. This contradicts the nearby comment that there is “no silent fallback”: a missing/unprepared source path becomes literal silence with no operator-visible reason.

Blast radius is medium because it does not corrupt core DSP, but it makes the desktop sketch-and-hear workflow fail opaquely on machines without a usable input/source. A reasonable fix is to surface `lastSourceError_` in the workbench UI and keep it updated when `fillBlock()` fails, or make source selection/preparation explicit enough that silence is an intentional state rather than an error sink.
