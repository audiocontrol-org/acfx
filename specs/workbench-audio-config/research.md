# Phase 0 Research — Workbench audio device + source + MIDI selection

Resolves the load-bearing decisions for adding in-UI device/source/MIDI selection +
persistence to the workbench without breaking real-time safety. Each entry:
**Decision / Rationale / Alternatives considered**. JUCE-internals claims are verified
against the pinned JUCE source (no assumptions — a lesson from the prior feature's
govern rounds).

## 1. Audio-stopped reconfigure via `restartLastAudioDevice()` (the RT-safety mechanism)

- **Decision**: Every source/device change reconfigures `WorkbenchAudioSource` in
  `prepareToPlay()` only, and source changes are applied by calling
  `deviceManager.restartLastAudioDevice()`. Verified in the pinned JUCE:
  `AudioDeviceManager::restartLastAudioDevice()` exists, and the
  `AudioSourcePlayer` that `AudioAppComponent` registers fires `audioDeviceStopped()`
  → our `releaseResources()` then `audioDeviceAboutToStart()` → our `prepareToPlay()`.
  So the audio callback is **guaranteed stopped** between release and re-prepare; the
  source (incl. `fileBuffer_`) is only ever reassigned in that stopped window.
- **Rationale**: This reuses the exact stopped-callback bracket that a device change
  already produces, for source changes too — one safe reconfigure path. It directly
  satisfies FR-008 and preserves the RT guarantees the prior govern rounds
  established (no mid-callback swap, no lock on the audio thread, no use-after-free —
  the AUDIT-15/20 class). `fillBlock` and the in-memory player stay byte-for-byte.
- **Alternatives considered**: (a) Double-buffer the file with an `atomic<int>`
  active-index for a lock-free *live* swap — works, but more complex and unnecessary
  given the device already gives us a clean stop window; deferred. (b)
  `shutdownAudio()` + `setAudioChannels()` to force re-init — heavier, re-reads device
  config, and can drop the user's selection; rejected.

## 2. `AudioDeviceSelectorComponent` for device + MIDI selection

- **Decision**: Host `juce::AudioDeviceSelectorComponent(deviceManager, 0, 2, 0, 2,
  showMidiInputs=true, showMidiOutputs=false, showChannelsAsStereoPairs=true,
  hideAdvancedOptions=false)` in a separate **Audio Settings** `DocumentWindow`. It
  drives the shared `AudioDeviceManager` directly — selecting input/output device,
  sample rate, buffer size, and MIDI inputs — and its edits fire the same
  stop/start cycle as decision 1, so the source reconfigures automatically.
- **Rationale**: It is JUCE's standard, tested device UI; it covers FR-001/002/005 in
  one component and removes the need to hand-roll dropdowns + rate/buffer/MIDI +
  error handling. Header present in the pinned JUCE.
- **Alternatives considered**: hand-built `ComboBox`es (reimplements what the standard
  component already does — more code, more bugs; rejected).

## 3. Persistence via `AudioDeviceManager` state XML + `ApplicationProperties`

- **Decision**: On change/quit, save `deviceManager.createStateXml()` (devices, rate,
  buffer, enabled MIDI inputs) plus a small `SourceConfig` (mode + file path) into a
  `juce::ApplicationProperties` user settings file (app name "acfx Workbench"). On
  launch, initialise the device manager from the saved XML (falling back to defaults
  when absent/invalid) and restore the source. `ApplicationProperties` header present
  in the pinned JUCE.
- **Rationale**: `createStateXml`/the `initialise(..., savedState)` overload is JUCE's
  built-in mechanism for exactly this; `ApplicationProperties` gives a per-user file in
  the platform-standard location with no path plumbing. Satisfies FR-006.
- **Alternatives considered**: a hand-rolled JSON/INI file (reinvents
  `ApplicationProperties`; rejected). Persisting nothing (fails FR-006 / SC-004;
  rejected).

## 4. Source selection UI + async file chooser

- **Decision**: A small `source-bar` component with a Live/File choice and a "Load
  file…" button using `juce::FileChooser::launchAsync` (non-blocking, message-thread).
  Choosing *File* opens the chooser; on a valid pick it updates `SourceConfig.file`
  and triggers the decision-1 restart; cancelling with no file leaves the prior valid
  source (revert to Live if none) — never a broken no-source state (FR-009).
- **Rationale**: The async chooser keeps the message thread responsive and never
  touches the audio thread; the source bar emits callbacks only (no audio logic),
  keeping it small and testable-by-inspection. Satisfies FR-003/004.
- **Alternatives considered**: a modal/blocking chooser (can stall the UI; rejected).
  Keeping the env var as the only source path (fails FR-004; rejected — env var
  retained as a first-run convenience only).

## 5. Failure surfacing (no silent fallback)

- **Decision**: Device-open failures surface through `AudioDeviceSelectorComponent`'s
  own UI; source/config failures (unreadable file, missing saved device/file, corrupt
  settings) use the existing async `NativeMessageBox` alert and leave a safe,
  non-broken state. Corrupt settings → start with defaults + report.
- **Rationale**: Constitution V / FR-009 — errors are surfaced, never swallowed or
  replaced with mock audio. Reuses the alert path already added to the workbench.
- **Alternatives considered**: silent fallback to defaults with no message (violates V;
  rejected).

## 6. The one host-side testable seam: `SourceConfig` serialize/parse

- **Decision**: `SourceConfig { SourceMode mode; juce::String filePath; }` gets a
  **pure** `serialize → string` / `parse string → SourceConfig` pair kept free of
  device/UI types, unit-tested host-side for round-trip + malformed input.
- **Rationale**: The rest of the feature is interactive JUCE UI (manual-acceptance);
  this is the one piece with deterministic, device-free behaviour worth a doctest.
- **Alternatives considered**: no test (loses the only cheap regression guard;
  rejected). Round-tripping through the full `ApplicationProperties` file in a test
  (drags in a filesystem + JUCE app context; rejected — keep the seam pure).

## Outcome

All decisions resolved; the RT-safety mechanism (decision 1) and the JUCE APIs are
verified against the pinned source. No `[NEEDS CLARIFICATION]` remains. Ready for
Phase 1.
