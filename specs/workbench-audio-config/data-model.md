# Phase 1 Data Model — Workbench audio device + source + MIDI selection

The "data" here is workbench-adapter configuration/state (no application data, no
persistence beyond a settings file). Entities map to the spec's Key Entities; field
lists are conceptual (exact C++ spelling lives in `contracts/`).

## SourceMode

- **What**: which source the workbench filters.
- **Values**: `live` (the selected input device) | `file` (a looped audio file).
- **Rules**: defaults to `live`; `file` is only valid once a readable file is chosen.

## SourceConfig

- **What**: the source selection as a persistable value (the one host-testable seam).
- **Fields**: `mode: SourceMode`, `filePath: string` (empty unless `mode == file`).
- **Rules**: pure `serialize → string` / `parse string → SourceConfig` round-trips;
  parsing a malformed/empty string yields the safe default (`live`, empty path) rather
  than throwing — this is config parsing, not audio-path data (Constitution V applies
  to runtime audio fallbacks, not to defaulting an absent setting).
- **Relationships**: produced/consumed by the persistence unit; consumed by the
  workbench to decide how `prepareToPlay` configures `WorkbenchAudioSource`.

## AudioSelection (held by JUCE, persisted as XML)

- **What**: the chosen input/output device, sample rate, buffer size, and enabled MIDI
  inputs — owned by `juce::AudioDeviceManager`.
- **Form**: `AudioDeviceManager::createStateXml()` on save; restored via the manager's
  initialise-from-saved-state on launch.
- **Rules**: the workbench does not model these fields itself; it persists/restores the
  manager's own XML. An unavailable saved device falls back to an available one and is
  surfaced (FR-009 / edge case).

## PersistedSettings

- **What**: the saved form of everything restored on launch.
- **Fields**: the `AudioDeviceManager` state XML (devices/rate/buffer/MIDI) +
  `SourceConfig` (mode + file path).
- **Form**: a `juce::ApplicationProperties` user settings file (app name "acfx
  Workbench"), one profile per user.
- **Rules**: a corrupt/unreadable file does not crash startup — the workbench starts
  with defaults and reports the problem (edge case / FR-009).

## Workbench message-thread state (runtime)

- **What**: the live selection the audio lifecycle reads.
- **Fields**: current `SourceMode`, current `sourceFile`, `sourceReady` flag,
  `preparedChannels` (already present).
- **State transitions**: a UI change (device via the selector, or source via the
  source bar) updates this state, then triggers an **audio-stopped reconfigure**
  (`restartLastAudioDevice()` → `releaseResources` → `prepareToPlay`). `prepareToPlay`
  is the single point that (re)configures `WorkbenchAudioSource` from this state.
  `WorkbenchAudioSource` itself is unchanged except that its "throw if already
  configured" guard becomes the lifecycle invariant "release before reconfigure"
  (prepareToPlay releases first), since reconfiguration is now legitimate — and still
  only ever happens with the audio callback stopped.

## Relationships (summary)

```
SourceConfig ──persisted in──> PersistedSettings <──includes── AudioDeviceManager XML
     │ parsed on launch                                   │ restored on launch
     ▼                                                    ▼
 Workbench message-thread state ──drives──> prepareToPlay() ──configures──>
 WorkbenchAudioSource (live input | in-memory file player)   [audio stopped]
```
