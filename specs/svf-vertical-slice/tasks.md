---

description: "Task list — SVF vertical slice"
---

> ‼ **acfx COMMANDMENTS — non-negotiable** ‼
> **1. COMMIT AND PUSH EARLY AND OFTEN** — version control is a distributed, journaled
> filesystem that safeguards your work, **NOT a sacred rite reserved for the blessed.**
> Small atomic commits, pushed promptly; never hoard unpushed work.
> **2. NO GIT HOOKS, EVER** — this repo uses zero git hooks; none exist, none get added.
> **3. DESCRIPTIVE NAMES, NEVER NUMERIC PREFIXES** — names carry information; fake sequence
> numbers (`001-`) imply false order and false precision (datestamps excepted).
> (acfx Constitution, Principles I–III — `.specify/memory/constitution.md`.)

# Tasks: SVF Vertical Slice — proving the acfx cross-platform spine

**Input**: Design documents from `specs/svf-vertical-slice/`

**Prerequisites**: plan.md, spec.md, research.md, data-model.md, contracts/, quickstart.md

**Tests**: TDD is used for the platform-independent core (constitution VIII): the
no-allocation invariant and the SVF frequency-response check are first-class tasks
written before/with the implementation they cover.

## Format: `[ID] [P?] [Story] Description with file path`

- **[P]**: parallelizable (different files, no dependency on an incomplete task)
- **[Story]**: US1 / US2 / US3 (user-story phases only; setup/foundational/polish carry none)

## Path Conventions

Cross-platform C++ monorepo (per plan.md): `core/`, `host/`, `adapters/*`, `tests/`,
`cmake/`, `external/` at repo root. Descriptive names only — no numeric prefixes.

---

## Phase 1: Setup (build skeleton)

- [X] T001 Create the monorepo directory skeleton with placeholders (`core/dsp/`, `core/primitives/`, `core/effects/svf/`, `host/processor-node/`, `adapters/workbench/`, `adapters/plugin/`, `adapters/daisy/`, `adapters/teensy/`, `tests/core/`, `tests/support/`, `cmake/`, `external/`) per `plan.md` Project Structure
- [X] T002 Add top-level `CMakeLists.txt` defining the `acfx_core` interface/library target and orchestrating subdirectories, with C++17/C++20 handling (concept checks guarded by `__cpp_concepts`)
- [X] T003 [P] Add `cmake/CPM.cmake` and `cmake/dependencies.cmake` declaring CPM-pinned deps (JUCE 8, clap-juce-extensions, DaisySP, libDaisy, Teensy core+Audio Library, doctest); capture each exact tag in the declaration when first fetched (research.md §4)
- [X] T004 [P] Add `CMakePresets.json` with presets `desktop`, `daisy`, `teensy`, `test`
- [X] T005 [P] Add `cmake/toolchains/daisy.cmake` (arm-none-eabi-gcc) and `cmake/toolchains/teensy.cmake` toolchain files
- [X] T006 [P] Add `.clang-format` and `.editorconfig` aligned with strict typing (no implicit narrowing) at repo root

**Checkpoint**: `cmake --preset test` configures cleanly (empty test target ok).

---

## Phase 2: Foundational — the core spine + tests (BLOCKS all user stories)

The platform-independent spine every story reuses. No JUCE/libDaisy/Teensy headers
in any `core/` file (Constitution IV).

- [X] T007 [P] Implement `ParamId` + `ParamUnit`/`ParamSkew`/`ParamKind` enums in `core/dsp/param-id.h` (per contracts/parameter-model.md)
- [X] T008 [P] Implement `ParameterDescriptor` + allocation-free `normalize`/`denormalize` (linear, logarithmic, discrete) in `core/dsp/parameter.h` (per contracts/parameter-model.md)
- [X] T009 [P] Implement `ProcessContext` (sampleRate, maxBlockSize, numChannels) in `core/dsp/process-context.h`
- [X] T010 [P] Implement `AudioBlock` (fixed-size, non-owning, non-allocating view) in `core/dsp/audio-block.h`
- [X] T011 Define the `Effect` concept (C++20, `__cpp_concepts`-guarded; C++17 duck-typed fallback) in `core/dsp/effect.h` (per contracts/effect-concept.md) — depends on T007–T010
- [X] T012 [P] Implement the allocation sentinel (global `operator new`/`delete` counter, thread-local) in `tests/support/allocation-sentinel.h` + `tests/support/allocation-sentinel.cpp`
- [X] T013 [P] Capture known-good SVF frequency-response reference vectors in `tests/support/svf-reference.h`
- [X] T014 Write parameter scaling/skew tests in `tests/core/parameter-test.cpp` (assert linear/log/discrete mapping at min/mid/max + bounds) — fails until T008
- [X] T015 Implement the DaisySP `Svf` wrapper (allocation-free, mode-selectable) in `core/primitives/svf-primitive.h` (research.md §1) — depends on T003
- [X] T016 Write SVF effect tests (impulse/frequency response per mode vs T013 references, NaN/denormal stability at high resonance) in `tests/core/svf-test.cpp` — fails until T017
- [X] T017 Implement `SvfEffect` (constexpr param table cutoff[log]/resonance[linear]/mode[discrete×3]; `prepare`/`process`/`reset`/`setParameter`) in `core/effects/svf/svf-effect.h` (+ `.cpp` if needed) satisfying `Effect` — makes T014/T016 pass
- [X] T018 Write + pass the no-heap-allocation-in-`process()` invariant test in `tests/core/no-allocation-test.cpp` using the T012 sentinel across several block sizes (FR-014) — depends on T017
- [X] T019 Implement `ProcessorNode` interface + `EffectNode<T>` template in `host/processor-node/processor-node.h` (per contracts/processor-node.md; desktop-only, ≤1 vcall/block) — depends on T011
- [X] T020 Wire the `test` preset target so `ctest --preset test` builds + runs all `tests/core/*` (FR-013)

**Checkpoint**: `cmake --build --preset test && ctest --preset test` is green — the
core spine + SVF are proven host-side. User stories can now proceed in any order.

---

## Phase 3: User Story 1 — Desktop sketch-and-hear (Priority P1) 🎯 MVP

**Goal**: Hear and tweak the SVF live in the desktop workbench; the fast edit→hear loop.

**Independent test**: quickstart Scenario B — build + launch the workbench, route audio,
sweep cutoff/resonance/mode, drive a bound MIDI CC, toggle A/B; edit + rebuild + relaunch.

- [X] T021 [US1] Add the JUCE standalone workbench target in `adapters/workbench/CMakeLists.txt` (under the `desktop` preset)
- [X] T022 [US1] Implement the workbench app holding `std::unique_ptr<ProcessorNode>` = `EffectNode<SvfEffect>` and the audio device callback in `adapters/workbench/workbench-app.cpp` — depends on T019
- [X] T023 [P] [US1] Auto-render a control per descriptor from `SvfEffect::parameters()` in `adapters/workbench/parameter-view.cpp`
- [X] T024 [P] [US1] Bind MIDI CCs → `setParameter(id, normalized)` in `adapters/workbench/midi-binding.cpp`
- [X] T025 [P] [US1] Implement the audio source (built-in loop/file player + live input device selection; descriptive error if neither available — Constitution V) in `adapters/workbench/audio-source.cpp`
- [X] T026 [US1] Implement the dry/processed A/B toggle in the workbench signal path in `adapters/workbench/workbench-app.cpp`
- [ ] T027 [US1] Run quickstart Scenario B end-to-end and confirm all US1 acceptance scenarios

**Checkpoint**: US1 is independently shippable — a working sketch-and-hear workbench.

---

## Phase 4: User Story 2 — DAW plugin (Priority P2)

**Goal**: The same effect, unchanged, as a VST3/AU/CLAP plugin with host automation.

**Independent test**: quickstart Scenario C — load each format in a host, automate cutoff,
confirm parity with the workbench.

- [X] T028 [US2] Add the JUCE plugin target exporting VST3 + AU + CLAP (CLAP via clap-juce-extensions) in `adapters/plugin/CMakeLists.txt`
- [X] T029 [US2] Implement the plugin `AudioProcessor` wrapping the same `EffectNode<SvfEffect>` in `adapters/plugin/plugin-processor.cpp` — depends on T019
- [X] T030 [US2] Generate host-automation parameters from `SvfEffect::parameters()` (name/range/default/skew) in `adapters/plugin/plugin-parameters.cpp`
- [ ] T031 [US2] Run quickstart Scenario C and confirm all US2 acceptance scenarios (formats instantiate; params correct; parity with workbench)

**Checkpoint**: US2 is independently shippable — the plugin, sharing the core with US1.

---

## Phase 5: User Story 3 — Hardware cross-compile + link (Priority P3)

**Goal**: The same SVF source compiles and links for Daisy and Teensy with parameter
mapping; no JUCE in the MCU builds.

**Independent test**: quickstart Scenario D — `daisy` and `teensy` presets build & link
the same `core/effects/svf`; each dependency graph shows core + adapter only.

- [X] T032 [P] [US3] Implement the Daisy adapter (libDaisy audio callback → `effect.process`; ADC/encoder → `setParameter`) in `adapters/daisy/daisy-main.cpp`
- [X] T033 [P] [US3] Implement the Teensy adapter (Teensy `AudioStream` node → `effect.process`; analog/MIDI → `setParameter`) in `adapters/teensy/teensy-main.cpp`
- [X] T034 [US3] Verify the installed Teensy toolchain's C++ standard (research.md §3 open item); set Teensy to the highest supported (≥C++17) in `cmake/toolchains/teensy.cmake` and confirm the concept-degradation path compiles the same `SvfEffect`
- [ ] T035 [US3] Run quickstart Scenario D: build `daisy` + `teensy` presets, confirm linked artifacts and that neither dependency graph includes JUCE or desktop-only stubs (SC-007)

**Checkpoint**: US3 done — the cross-platform claim is real (build + link on both MCUs).

---

## Phase 6: Polish & cross-cutting concerns

- [ ] T036 [P] Add a CI workflow building + testing the `test` and `desktop` presets on every change (quickstart Scenario A + desktop build), as explicit steps — NOT a git hook (Constitution II, FR-015)
- [ ] T037 [P] Add explicit script/CI checks for the file-size budget (~300–500 lines, Constitution VII) and "no JUCE in MCU dependency graph" (SC-007) — visible steps, not hooks
- [ ] T038 [P] Confirm the one-source-many-targets invariant (quickstart Scenario E): the identical `core/effects/svf` built all four targets with no per-target `#ifdef` forks of the effect (SC-001, SC-005)
- [ ] T039 Update `README.md` with build/run instructions referencing `quickstart.md`

---

## Dependencies & completion order

- **Setup (Phase 1)** → **Foundational (Phase 2)** must complete before any user story.
- Within Foundational: T007–T010 → T011; T008 → T014; T003 → T015 → T016 → T017 → T018; T011 → T019.
- **US1, US2, US3 are independent** once Foundational is done (each only needs the core spine + SVF; US1/US2 additionally need T019 ProcessorNode; US3 does not).
- Recommended order is priority order: **US1 (MVP) → US2 → US3**, but US2 and US3 may be built in parallel by different people after Phase 2.
- **Polish (Phase 6)** after the stories it validates.

## Parallel opportunities

- Phase 1: T003, T004, T005, T006 in parallel.
- Phase 2: T007–T010 in parallel; T012, T013 in parallel with them.
- Phase 3 (US1): T023, T024, T025 in parallel after T022.
- Phase 5 (US3): T032 and T033 in parallel.
- Across stories: after Phase 2, US2 (Phase 4) and US3 (Phase 5) can proceed concurrently.

## MVP scope

**User Story 1 alone** (Phases 1+2+3) is a viable MVP: a working desktop
sketch-and-hear workbench for the SVF, with the full proven core spine behind it.
US2 and US3 extend the *same* core to the plugin and to hardware without touching
the effect source.

## Task count

- Setup: 6 (T001–T006)
- Foundational: 14 (T007–T020)
- US1: 7 (T021–T027) · US2: 4 (T028–T031) · US3: 4 (T032–T035)
- Polish: 4 (T036–T039)
- **Total: 39**
