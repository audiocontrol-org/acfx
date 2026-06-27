> ‼ **acfx COMMANDMENTS — non-negotiable** ‼
> **1. COMMIT AND PUSH EARLY AND OFTEN** — version control is a distributed, journaled
> filesystem that safeguards your work, **NOT a sacred rite reserved for the blessed.**
> Small atomic commits, pushed promptly; never hoard unpushed work.
> **2. NO GIT HOOKS, EVER** — this repo uses zero git hooks; none exist, none get added.
> **3. DESCRIPTIVE NAMES, NEVER NUMERIC PREFIXES** — names carry information; fake sequence
> numbers (`001-`) imply false order and false precision (datestamps excepted).
> (acfx Constitution, Principles I–III — `.specify/memory/constitution.md`.)

# Implementation Plan: Workbench audio device + source + MIDI selection (in-UI)

**Branch**: `platform-foundation` | **Date**: 2026-06-26 | **Spec**: [spec.md](./spec.md)

**Input**: Feature specification from `specs/workbench-audio-config/spec.md`

## Summary

Make the JUCE desktop workbench usable by a person: add in-UI selection of the audio
input/output device (plus rate/buffer), the audio source (live input or a file picked
in the UI), and MIDI inputs, and persist those choices across launches. The technical
approach (from the approved design) is to host JUCE's standard
`AudioDeviceSelectorComponent` in a separate **Audio Settings** window, add a small
custom **source bar** (Live/File + "Load file…") to the main window, and persist via
the `AudioDeviceManager` state XML plus a small settings file. The load-bearing
constraint — keeping the existing real-time guarantees — is met by routing **every**
device/source change through an **audio-stopped reconfigure** (`prepareToPlay`
re-applies the source; source changes trigger a clean audio restart), so no change
ever swaps a source under a live audio callback. The platform-independent core, the
`ProcessorNode` boundary, and `WorkbenchAudioSource`'s RT-safe `fillBlock` are
untouched.

## Technical Context

**Language/Version**: C++20 (desktop preset; same as the existing workbench).

**Primary Dependencies**: JUCE 8 (already CPM-pinned) — specifically
`juce::AudioDeviceManager`, `juce::AudioDeviceSelectorComponent`,
`juce::ApplicationProperties`, `juce::FileChooser`, `juce::AudioAppComponent`. No new
external dependency.

**Storage**: a per-user settings file (JUCE `ApplicationProperties`, app name "acfx
Workbench") holding the device-manager state XML + a small source/MIDI config. This is
the milestone's only persistence; it is local config, not application data.

**Testing**: doctest, host-side. The one pure, **JUCE-free** (`std::string`),
device-free seam — `SourceConfig` serialize/parse — is unit-tested in the JUCE-free
core test target (the workbench converts `std::string ↔ juce::String` at the
boundary). Device/restart/UI behaviour is interactive (manual-acceptance), with
compile-verification against real JUCE in CI.

**Target Platform**: desktop standalone (the `desktop` CMake preset). macOS first
(arm64); the JUCE selector + properties are cross-platform, so Linux/Windows desktop
builds get the same UI where those toolchains are available.

**Project Type**: desktop-app adapter — workbench glue over the existing core. No core
or MCU changes.

**Performance Goals**: real-time audio unaffected — the audio callback
(`getNextAudioBlock`) and the lock-free parameter handoff are unchanged; all
reconfiguration happens off the audio thread with the engine stopped. No new work in
`process()`/the callback.

**Constraints**: no heap allocation/lock/throw on the audio thread (Constitution VI —
already satisfied and preserved); no fallbacks/mock audio (Constitution V — failures
surfaced); strict typing, no unchecked casts; source files ≤~300–500 lines
(Constitution VII), so the workbench adapter is split into focused units.

**Scale/Scope**: one adapter (the workbench); ~4 new small source units + edits to
`workbench-app.cpp` and `audio-source.{h,cpp}`. No change to `core/`, `host/`, the
plugin, or the MCU adapters.

## Constitution Check

*GATE: Must pass before Phase 0 research. Re-check after Phase 1 design.*

| Principle | Gate | Status |
|---|---|---|
| I. Commit & push early/often | Authored + implemented in small pushed increments | ✅ planned |
| II. No git hooks | Quality stays in CMake/CI/doctest + the portability gate; no hooks | ✅ by design |
| III. Descriptive names | Spec dir `workbench-audio-config`; new files named for what they are | ✅ |
| IV. Platform-independent core, thin adapters | Zero `core/` or `host/` change; all work is workbench-adapter glue | ✅ central constraint |
| V. No fallbacks/mock outside tests | Device/file/settings failures surface a message; never silent zeros or mock audio | ✅ FR-009 |
| VI. Real-time safety | Audio callback unchanged; every reconfigure happens with the engine stopped — no mid-callback swap, lock, or throw | ✅ FR-008 (the load-bearing invariant) |
| VII. Strict typing & small modules | Workbench split into focused ≤300-line units; no `any`/unchecked casts | ✅ |
| VIII. Test the core host-side | Core untouched (still 11/11); new pure seam (`SourceConfig` serde) unit-tested; UI is manual-acceptance | ✅ |

**Result**: PASS — no violations. Complexity Tracking left empty.

## Project Structure

### Documentation (this feature)

```text
specs/workbench-audio-config/
├── plan.md              # This file (/speckit-plan output)
├── research.md          # Phase 0 — decisions for the device/restart/persistence path
├── data-model.md        # Phase 1 — the config/state types
├── quickstart.md        # Phase 1 — runnable validation per user story
├── contracts/
│   └── source-config.md # the SourceConfig serialize/parse contract (the tested seam)
├── checklists/
│   └── requirements.md  # spec-quality checklist (passing)
└── tasks.md             # Phase 2 (/speckit-tasks — not created here)
```

### Source Code (repository root) — only `adapters/workbench/` changes

```text
adapters/workbench/
├── workbench-app.cpp         # CHANGED: wire components; own the source lifecycle
│                             #   (prepareToPlay reconfigure + restartAudio); add the
│                             #   Audio Settings button; load/save settings on
│                             #   start/quit; replace auto-enable-all MIDI with the
│                             #   selector + first-run default
├── audio-settings.h/.cpp     # NEW: AudioSettingsWindow over AudioDeviceSelectorComponent
├── source-bar.h/.cpp         # NEW: Live/File + "Load file…" component (callbacks only)
├── workbench-settings.h/.cpp # NEW: JUCE-FREE — SourceMode/SourceConfig + serialize/
│                             #   parse (the tested seam; compiled into acfx_core_tests)
├── workbench-persistence.h/.cpp # NEW: JUCE — save/load the device XML + SourceConfig
│                             #   via ApplicationProperties (workbench only)
├── audio-source.h/.cpp       # CHANGED: relax the "throw if configured" guard into the
│                             #   release-before-reconfigure lifecycle (fillBlock + the
│                             #   in-memory player stay byte-for-byte RT-safe)
├── parameter-view.h/.cpp     # unchanged
├── midi-binding.h            # unchanged
└── CMakeLists.txt            # CHANGED: add the new sources

tests/core/                   # NEW: workbench-settings-test.cpp (SourceConfig serde round-trip)
```

**Structure Decision**: desktop-app adapter change confined to `adapters/workbench/`.
The inward-only dependency rule holds — nothing here is included by `core/`, `host/`,
the plugin, or the MCU adapters. The audio callback and parameter handoff are
unchanged; the only behavioural change is **how/when** the source is (re)configured
(always engine-stopped) and the addition of device/MIDI selection + persistence.

## Complexity Tracking

> No Constitution Check violations — this section intentionally left empty.

## Phases (for reference; artifacts generated by this command)

- **Phase 0 — Research** (`research.md`): resolve the load-bearing decisions — the
  audio-stopped restart mechanism (how to cleanly stop/reconfigure/start with
  `AudioAppComponent`), `AudioDeviceSelectorComponent` configuration, the persistence
  surface (`createStateXml`/`initialise` + `ApplicationProperties`), and the file
  chooser (async).
- **Phase 1 — Design & Contracts** (`data-model.md`, `contracts/`, `quickstart.md`):
  the config/state types, the `SourceConfig` serialize/parse contract (the tested
  seam), and a runnable validation guide per user story.
- **Phase 2 — Tasks** (`tasks.md`): produced by `/speckit-tasks`, not here.
