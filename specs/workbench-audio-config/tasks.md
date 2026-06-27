---

description: "Task list — workbench audio device + source + MIDI selection"
---

> ‼ **acfx COMMANDMENTS — non-negotiable** ‼
> **1. COMMIT AND PUSH EARLY AND OFTEN** — version control is a distributed, journaled
> filesystem that safeguards your work, **NOT a sacred rite reserved for the blessed.**
> Small atomic commits, pushed promptly; never hoard unpushed work.
> **2. NO GIT HOOKS, EVER** — this repo uses zero git hooks; none exist, none get added.
> **3. DESCRIPTIVE NAMES, NEVER NUMERIC PREFIXES** — names carry information; fake sequence
> numbers (`001-`) imply false order and false precision (datestamps excepted).
> (acfx Constitution, Principles I–III — `.specify/memory/constitution.md`.)

# Tasks: Workbench audio device + source + MIDI selection (in-UI)

**Input**: Design documents from `specs/workbench-audio-config/`

**Prerequisites**: plan.md, spec.md, research.md, data-model.md, contracts/, quickstart.md

**Tests**: One pure seam is unit-tested (the `SourceConfig` serialize/parse, per
contracts/source-config.md) — written before its implementation. The rest is
interactive JUCE UI verified by manual acceptance (quickstart Scenarios B–F).

## Format: `[ID] [P?] [Story] Description with file path`

- **[P]**: parallelizable (different files, no dependency on an incomplete task)
- **[Story]**: US1 / US2 / US3 / US4 (user-story phases only)

## Path Conventions

All work is confined to `adapters/workbench/` (+ one test file under `tests/core/`).
No change to `core/`, `host/`, the plugin, or the MCU adapters. Descriptive names only.

---

## Phase 1: Setup

- [X] T001 Register the new workbench units and test in the build: add `audio-settings.cpp`, `source-bar.cpp`, `workbench-settings.cpp` (JUCE-free serde), and `workbench-persistence.cpp` (JUCE) to `adapters/workbench/CMakeLists.txt`; add `tests/core/workbench-settings-test.cpp` AND `adapters/workbench/workbench-settings.cpp` to the `acfx_core_tests` target in `tests/CMakeLists.txt` (the serde TU is JUCE-free so it links there). Create empty stubs so both the `desktop` and `test` presets still configure + build

**Checkpoint**: `cmake --build --preset desktop --target acfx_workbench` and `ctest --preset test` still green with the stubs in place.

---

## Phase 2: Foundational — shared lifecycle + config (BLOCKS all user stories)

The audio-stopped reconfigure lifecycle and the persistable source config every story
reuses. No change to the audio callback or the lock-free parameter handoff.

- [X] T002 Implement `SourceMode` + `SourceConfig` (`std::string filePath`) + pure JUCE-free `serialize`/`parse` (`std::string`; never throws; safe default on garbage) in `adapters/workbench/workbench-settings.h` (+ `.cpp`) per `contracts/source-config.md` — JUCE-free so it links into the JUCE-free `acfx_core_tests`; the workbench converts to/from `juce::String` at the boundary
- [X] T003 Write the `SourceConfig` serialize/parse unit test (round-trip for both modes incl. paths with spaces/unicode; safe-default on `""`/garbage/unknown-mode) in `tests/core/workbench-settings-test.cpp` — fails until T002
- [X] T004 Rework the workbench source lifecycle in `adapters/workbench/workbench-app.cpp`: hold message-thread `SourceMode`/`sourceFile_` state; make `prepareToPlay()` the single reconfigure point (release → configure live/file from state → prepare); add `restartAudio()` using `deviceManager.restartLastAudioDevice()` (research.md §1) so source changes apply with the callback stopped
- [X] T005 Relax `adapters/workbench/audio-source.h`/`.cpp`: replace the "throw if already configured" guard with the release-before-reconfigure lifecycle invariant; keep `fillBlock` `noexcept` + the in-memory player byte-for-byte (RT-safety preserved, Constitution VI)

**Checkpoint**: `ctest --preset test` green incl. the new serde test; the workbench builds; device-restart reconfigure path compiles against real JUCE.

---

## Phase 3: User Story 1 — Choose input/output devices (Priority: P1) 🎯 MVP

**Goal**: Select audio input/output device (+ rate/buffer) from the UI; changes take
effect live.

**Independent test**: quickstart Scenario B — open Audio Settings, change output then
input device, confirm audio routes accordingly with no restart/crash.

- [X] T006 [US1] Implement `AudioSettingsWindow` hosting `juce::AudioDeviceSelectorComponent(deviceManager, 0, 2, 0, 2, /*midiIn*/ true, /*midiOut*/ false, /*stereoPairs*/ true, /*hideAdvanced*/ false)` (close box hides) in `adapters/workbench/audio-settings.h`/`.cpp` (research.md §2)
- [X] T007 [US1] Add an "Audio Settings…" button to the main window in `adapters/workbench/workbench-app.cpp` that shows the window; confirm a device change drives the lifecycle (T004) so the source reconfigures with the callback stopped
- [ ] T008 [US1] *(manual acceptance — operator)* Run quickstart Scenario B and confirm the US1 acceptance scenarios (output/input device change routes audio; a failing device is surfaced, previous device kept)

**Checkpoint**: US1 usable — you can route the workbench to chosen in/out devices.

---

## Phase 4: User Story 2 — Choose the source from the UI (Priority: P2)

**Goal**: Live/File source choice + a file picker, no env var.

**Independent test**: quickstart Scenario C — switch to File, pick a file, hear it
looped; switch back to Live; cancel-with-no-file stays valid.

- [X] T009 [P] [US2] Implement the source bar (Live/File choice + "Load file…" via `juce::FileChooser::launchAsync`) emitting source-change callbacks (no audio logic) in `adapters/workbench/source-bar.h`/`.cpp` (research.md §4)
- [X] T010 [US2] Wire the source bar into `adapters/workbench/workbench-app.cpp`: on change, update `SourceMode`/`sourceFile_` then `restartAudio()`; *File* with no chosen file reverts to *Live* (no broken no-source state; FR-009)
- [ ] T011 [US2] *(manual acceptance — operator)* Run quickstart Scenario C and confirm the US2 acceptance scenarios (file loops; new file without restart; cancel stays valid; no glitch at switch)

**Checkpoint**: US2 usable — the built-in player is reachable from the UI, no env var.

---

## Phase 5: User Story 3 — Remember selections across launches (Priority: P2)

**Goal**: Persist + restore devices/rate/buffer/source/MIDI.

**Independent test**: quickstart Scenario D — select non-defaults, quit, relaunch,
confirm restored.

- [X] T012 [US3] Implement persistence in `adapters/workbench/workbench-persistence.h`/`.cpp` (JUCE; separate TU from the JUCE-free serde): save `deviceManager.createStateXml()` + `serialize(SourceConfig)` into a `juce::ApplicationProperties` settings file (app "acfx Workbench"); load + restore on launch (init device manager from saved XML; restore source) (research.md §3)
- [X] T013 [US3] Wire load-on-launch + save-on-change/quit into `adapters/workbench/workbench-app.cpp`; corrupt/missing settings → safe defaults + surfaced message; a saved device/file that is gone at launch → fall back + surface (FR-009, edge cases)
- [ ] T014 [US3] *(manual acceptance — operator)* Run quickstart Scenario D and confirm the US3 acceptance scenarios (selections restored; missing saved device falls back + surfaced)

**Checkpoint**: US3 usable — the workbench remembers its configuration.

---

## Phase 6: User Story 4 — Choose MIDI input devices (Priority: P3)

**Goal**: Explicit MIDI input selection instead of auto-enable-all.

**Independent test**: quickstart Scenario E — enable one of two controllers; only it
drives CCs.

- [X] T015 [US4] Replace the auto-enable-all MIDI code in `adapters/workbench/workbench-app.cpp` with the `AudioDeviceSelectorComponent` MIDI-inputs section (from T006) + a first-run default (enable all once); confirm only enabled inputs drive CC 74/71
- [ ] T016 [US4] *(manual acceptance — operator)* Run quickstart Scenario E and confirm the US4 acceptance scenario (only the enabled controller affects the filter)

**Checkpoint**: US4 done — MIDI inputs are user-selectable.

---

## Phase 7: Polish & cross-cutting concerns

- [X] T017 [P] Finalize `adapters/workbench/CMakeLists.txt` (all new sources) and extend the CI workflow / `scripts/check-portability.sh` to build the new workbench sources and run the new `workbench-settings-test`
- [X] T018 [P] Confirm each new unit is within the file-size budget (~300–500 lines, Constitution VII); split if any (esp. `workbench-app.cpp`) is over
- [~] T019 Run quickstart Scenario A (the serde unit test) and Scenario F (RT-safety under ~20× rapid device/source switches — no glitch/stall/crash); update the workbench section of `README.md` — **Scenario A passing (17/17 host tests) and README updated; Scenario F is interactive manual acceptance (operator)** (in-UI device/source/MIDI selection + persistence; `ACFX_WORKBENCH_FILE` is now a first-run convenience only)

---

## Dependencies & completion order

- **Setup (Phase 1)** → **Foundational (Phase 2)** before any user story.
- Within Foundational: T002 → T003; T004 + T005 are the shared lifecycle (US1 device
  changes and US2 source changes both depend on them).
- **US1 (P1)** needs T004 (device changes drive the reconfigure). **US2 (P2)** needs
  T002 + T004 + T005. **US3 (P2)** needs T002 (serde) + US1's settings window for the
  device XML. **US4 (P3)** needs T006 (the settings window carries the MIDI section).
- Recommended order: **US1 → US2 → US3 → US4** (priority order).
- **Polish (Phase 7)** after the stories it validates.

## Parallel opportunities

- T009 (source-bar) is independent of T006 (audio-settings) — different files.
- T017, T018 in parallel in Polish.

## MVP scope

**User Story 1** (Phases 1+2+3) is the MVP: in-UI input/output device selection, with
the shared audio-stopped reconfigure lifecycle behind it. US2–US4 extend the same
lifecycle to source selection, persistence, and MIDI.

## Task count

- Setup: 1 (T001) · Foundational: 4 (T002–T005)
- US1: 3 (T006–T008) · US2: 3 (T009–T011) · US3: 3 (T012–T014) · US4: 2 (T015–T016)
- Polish: 3 (T017–T019)
- **Total: 19**
