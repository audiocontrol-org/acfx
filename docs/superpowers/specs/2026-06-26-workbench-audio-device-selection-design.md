# Design — Workbench audio device + source selection (in-UI)

**Date:** 2026-06-26
**Feature:** make the acfx workbench usable by humans — in-UI audio input/output
device selection, source selection, and MIDI input selection, with persistence.
**Status:** approved (brainstorming) — pending implementation plan.

## Problem

The workbench currently calls `setAudioChannels(2, 2)` and uses the **default**
audio device only, with no device-picker UI. Source selection (live input vs the
built-in file player) is hidden behind the `ACFX_WORKBENCH_FILE` env var, and MIDI
inputs are auto-enabled with no choice. That is enough to prove the platform spine
but not usable by a human: you cannot choose which input/output device to use, you
cannot load a file from the UI, and you cannot pick MIDI inputs.

## Goal

A workbench a person can actually run and route audio through:

1. Select **audio input and output devices** (plus sample rate / buffer size) in the UI.
2. Select the **audio source**: live input device, or a looped file chosen in the UI.
3. Select **MIDI input** devices in the UI.
4. **Persist** all of the above across launches.

Out of scope: per-effect editor UI, presets, multi-effect graph, a custom audio
backend. The platform-independent core, the `ProcessorNode` boundary, and the
RT-safety guarantees are untouched — this is entirely workbench-adapter glue.

## Approach (chosen: A)

Use JUCE's built-in **`AudioDeviceSelectorComponent`** for device + MIDI selection
(it drives the `AudioDeviceManager` directly and covers input/output device, sample
rate, buffer size, and MIDI inputs in one tested component), plus a small **custom
source bar** for the Live/File choice. Persist via the device manager's state XML +
a properties file.

Rejected: hand-built input/output dropdowns (reimplements rate/buffer/MIDI/error
handling JUCE already provides — more code, more bugs); a custom audio backend
(overkill).

## The load-bearing change: a runtime-safe source lifecycle

Today `WorkbenchAudioSource` *forbids* runtime source switching — `useFilePlayer`/
`useLiveInput` throw if called while `configured_`, a deliberate guard added in
response to a prior audit finding (a use-after-free / lock when swapping a source
underneath the running audio callback). Making devices and source changeable at
runtime must NOT reintroduce that race.

Mechanism: **every change reconfigures the source while the audio callback is
stopped.**

- Message-thread state: `SourceMode { live, file }` and `juce::File sourceFile_`.
- `prepareToPlay()` is the single reconfigure point. It `release()`s the source,
  then configures it from the current state (live → `useLiveInput(selected input
  channel count)`; file → `useFilePlayer(sourceFile_)`), then `prepare()`s.
- **Device changes** (via the selector) already make JUCE call
  `releaseResources()` → `prepareToPlay()` with the device callback stopped, so the
  source is reconfigured safely and automatically.
- **Source changes** (Live/File toggle, Load file) update the message-thread state
  and then trigger a clean **audio restart** (`restartAudio()`), so the same
  stopped-callback window applies.

This preserves every RT-safety guarantee established earlier: no mid-callback swap,
no lock on the audio thread, no use-after-free. Reconfiguration only ever happens
while the audio thread is stopped. `WorkbenchAudioSource`'s `fillBlock` stays
`noexcept` + lock-free + in-memory (unchanged); the "throw if configured" guard is
replaced by the lifecycle invariant (prepareToPlay releases before reconfiguring).

## Components

### AudioSettingsWindow (`audio-settings.h/.cpp`)

A `juce::DocumentWindow` hosting
`juce::AudioDeviceSelectorComponent(deviceManager, 0, 2, 0, 2, /*showMidiIn*/ true,
/*showMidiOut*/ false, /*stereoPairs*/ true, /*hideAdvanced*/ false)`. Opened by an
"Audio Settings…" button in the main window; its close box hides (not destroys) the
window. The selector drives `deviceManager` directly, so edits take effect
immediately and fire the source reconfigure above. Its MIDI-inputs section replaces
the current auto-enable-all code — all inputs are enabled on first run for
convenience, then user-controlled.

### Source bar (`source-bar.h/.cpp`)

A compact component: a Live/File choice + a "Load file…" button (async
`juce::FileChooser`). Emits a callback on change (mode and/or file). No audio logic
— the host (workbench-app) reacts by updating state + restarting audio. Picking
*File* opens the chooser; if no file is ever chosen it reverts to *Live* (no silent
fallback — Constitution V). Lives in the main window next to the existing Process
(A/B) toggle and the new Audio Settings… button.

### Persistence (`workbench-settings.h/.cpp`)

A `juce::ApplicationProperties` settings file (per-user app-data, app name "acfx
Workbench") stores `deviceManager.createStateXml()` (devices/rate/buffer/MIDI) plus
a small `SourceConfig { SourceMode mode; juce::String filePath; }`. On launch the
device manager is initialized from the saved XML (falling back to defaults if
absent) and the source is restored. On change and on quit, settings are saved. The
`ACFX_WORKBENCH_FILE` env var remains a first-run convenience override.

`SourceConfig` serialization (mode + path ↔ string) is a **pure function**, kept
free of JUCE device/UI types, so it is unit-testable host-side.

### workbench-app.cpp

Owns the device manager (via `AudioAppComponent`), wires the three components,
holds the `SourceMode`/`sourceFile_` state, and implements the Section-1 lifecycle:
`prepareToPlay` reconfigure + `restartAudio`. Keeps the audio callback
(`getNextAudioBlock`) and the lock-free parameter handoff unchanged.

## Error handling

- Device-open failures surface through `AudioDeviceSelectorComponent` (its own UI).
- Source configuration failures use the existing async message-box alert.
- *File* mode with no chosen file reverts to *Live* (explicit, not silent).
- No fallbacks or mock audio anywhere (Constitution V).

## Module layout (all <300 lines, Constitution VII)

```
adapters/workbench/
├── workbench-app.cpp        # wiring + source lifecycle (audio callback unchanged)
├── audio-settings.h/.cpp    # AudioSettingsWindow over AudioDeviceSelectorComponent
├── source-bar.h/.cpp        # Live/File + Load-file component (callbacks only)
├── workbench-settings.h/.cpp# persistence + pure SourceConfig serialize/parse
├── parameter-view.h/.cpp    # (unchanged)
├── midi-binding.h           # (unchanged)
└── audio-source.h/.cpp      # in-memory player; "throw if configured" guard relaxed
```

## Testing

- Core: unchanged, already 11/11 host-side.
- New testable seam: `SourceConfig` serialize/parse round-trip — a pure function,
  unit-tested host-side (no device).
- The rest (device dropdowns, restart cycle, MIDI selection, file chooser) is
  interactive JUCE UI: build + compile-verify against real JUCE here; run-and-listen
  is a **Manual-acceptance** item (extends Scenario B).
- CI / portability gate extended to build the new sources.

## Risks / notes

- The restart-on-source-change must be a clean stop→reconfigure→start so the audio
  callback is never live during `fileBuffer_` reassignment (this is the whole point
  of routing changes through `prepareToPlay`).
- `AudioDeviceSelectorComponent` is tall; the separate-window layout keeps the main
  sketch-and-hear surface compact.
- Governance: this is a new feature on a governed (stack-control) project. The
  implementation route — governed front door (new spec) vs. a direct plan — is an
  explicit decision at the implementation-transition step.
