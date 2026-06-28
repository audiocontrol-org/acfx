---

description: "Task list for Modulated Delay implementation"
---

> ‼ **acfx COMMANDMENTS — non-negotiable** ‼
> **1. COMMIT AND PUSH EARLY AND OFTEN** — version control is a distributed, journaled
> filesystem that safeguards your work, **NOT a sacred rite reserved for the blessed.**
> Small atomic commits, pushed promptly; never hoard unpushed work.
> **2. NO GIT HOOKS, EVER** — this repo uses zero git hooks; none exist, none get added.
> **3. DESCRIPTIVE NAMES, NEVER NUMERIC PREFIXES** — names carry information; fake sequence
> numbers (`001-`) imply false order and false precision (datestamps excepted).
> (acfx Constitution, Principles I–III — `.specify/memory/constitution.md`.)

# Tasks: Modulated Delay — feedback-filtered delay with movement, warble, and tape wow & flutter

**Input**: Design documents from `specs/modulated-delay/`

**Prerequisites**: plan.md, spec.md, research.md, data-model.md, contracts/

**Tests**: INCLUDED — Constitution VIII mandates host-side testing and FR-026 / SC-006 / SC-007
require the core be verified host-side. Test tasks are first-class here (doctest).

**Organization**: Tasks are grouped by user story (priority order) so each story is an
independently testable, demonstrable increment. The new DSP primitives are shared
prerequisites and live in the Foundational phase.

## Format: `[ID] [P?] [Story] Description`

- **[P]**: Can run in parallel (different files, no dependency on an incomplete task)
- **[Story]**: US1 / US2 / US3 (Setup/Foundational/Polish carry no story label)
- Exact file paths are included in each task.

## Path Conventions

Cross-platform C++ audio-DSP monorepo (per plan.md). Core under `core/`, host-side
tests under `tests/core/`. No new adapters or dependencies; the effect compiles into
the existing targets exactly as `SvfEffect`.

---

## Phase 1: Setup (Shared Infrastructure)

**Purpose**: Create the feature's source location and wire its tests into the build.

- [ ] T001 Create the effect source directory `core/effects/modulated-delay/` and register the three new host-side test sources (`tests/core/delay-line-test.cpp`, `tests/core/lfo-test.cpp`, `tests/core/modulated-delay-test.cpp`) in the existing `test` CMake target (mirror how `tests/core/svf-test.cpp` is registered in `CMakeLists.txt`).

---

## Phase 2: Foundational (Blocking Prerequisites)

**Purpose**: Platform-independent building blocks every story relies on. No story work
begins until these are in place.

**⚠️ CRITICAL**: Complete this phase before Phase 3+.

- [ ] T002 [P] Add a time unit (`seconds`) to the `ParamUnit` enum in `core/dsp/param-id.h` (purely additive — no behavior change for existing effects) and note its use for delay time in `specs/modulated-delay/contracts/` references (research Decision 7).
- [ ] T003 [P] Implement the interpolated `DelayLine` primitive in `core/primitives/delay-line.h` per `contracts/delay-line.md` — allocation-free circular buffer, linear fractional read, in-range clamp, `prepare`/`reset`/`write`/`readFractional` (no platform headers).
- [ ] T004 [P] Write `DelayLine` doctest in `tests/core/delay-line-test.cpp` — closed-form fractional-read accuracy, in-range bounds at 0 / at max / beyond max, integer round-trip, `reset()` silence (FR-007).
- [ ] T005 [P] Implement the `Lfo` primitive in `core/primitives/lfo.h` per `contracts/lfo.md` — phase accumulator, shapes {sine, triangle, saw, smoothed-random via seedable xorshift}, bipolar `[-1,1]` output, sample-rate-independent rate (no platform headers, no `std::random` in the audio path).
- [ ] T006 [P] Write `Lfo` doctest in `tests/core/lfo-test.cpp` — shape values at known phases, all shapes within `[-1,1]`, identical period in samples across 44.1k/48k/96k, deterministic click-free `random` (FR-015).

---

## Phase 3: User Story 1 — Filtered-feedback delay (Priority: P1) 🎯 MVP

**Goal**: A delay whose feedback path is shaped by an in-loop SVF, with delay time,
feedback, mix, and filter cutoff/resonance/mode controls.

**Independent Test**: In the workbench, route audio, set audible delay + high feedback,
lower the feedback-filter cutoff in low-pass → successive echoes darken; switch mode →
band-pass tail; sweep mix → click-free; high feedback → no runaway; change delay time →
no clicks. Host-side: feedback-shaping, mix, click-free retune, stability, no-alloc tests pass.

- [ ] T007 [US1] Add the US1 doctest suite scaffold in `tests/core/modulated-delay-test.cpp` — progressive feedback-filter shaping (level + brightness decay across echoes), dry/wet mix correctness, click-free delay-time change (one-pole smoothing), feedback-stability at the max bound (no divergence), and the no-heap-allocation-in-`process()` invariant (reuse the allocation sentinel from `tests/core/no-allocation-test.cpp`).
- [ ] T008 [US1] Implement the `ModulatedDelayEffect` skeleton in `core/effects/modulated-delay/modulated-delay-effect.h` satisfying the `Effect` contract — `prepare`/`process`/`reset`/`setParameter`/`parameters()`, the `constexpr` parameter table with the US1 controls (delay_time [seconds, log], feedback [0..0.98], mix, fb_cutoff, fb_resonance, fb_mode), a `static_assert` over `isValidDescriptor`, and the lock-free pending-edit atomics (same pattern as `core/effects/svf/svf-effect.h`).
- [ ] T009 [US1] Implement the per-channel feedback-delay signal path in `modulated-delay-effect.h` — preallocate the 2 s main `DelayLine` in `prepare()`, wire read→`SvfPrimitive`→feedback-scale (clamped <1.0)→sum→write, post-filter wet tap, dry/wet mix, and one-pole smoothing of the base delay time on stepped control edits (research Decisions 2 & 5).
- [ ] T010 [US1] Register/select the effect in the desktop workbench using the same mechanism `SvfEffect` uses (locate the effect-selection/instantiation site under `adapters/workbench/` and add `ModulatedDelayEffect`); then run the US1 host-side tests green and confirm `stackctl spec-check` is unaffected.

**Checkpoint**: US1 is a complete, demonstrable filtered-feedback delay — shippable MVP.

---

## Phase 4: User Story 2 — Modulated delay time & feedback filter (Priority: P2)

**Goal**: Three independent LFOs (delay time, filter cutoff, filter resonance), each with
its own rate, depth, and selectable shape, giving the tail movement and warble.

**Independent Test**: Enable delay-time modulation → periodic pitch/time warble at the rate;
enable cutoff modulation → periodic brightness sweep; any depth 0 → static / identical to US1;
increase rate → faster movement, no clicks/aliasing; modulation never reads out of range.

- [ ] T011 [US2] Extend `tests/core/modulated-delay-test.cpp` — delay-time modulation produces periodic movement at the set rate; cutoff & resonance modulation produce periodic tonal movement; depth-0 equivalence per destination (indistinguishable from US1, FR-013); sample-rate independence of modulation; in-range reads under maximal modulation (FR-014); no audible stepping across the rate/depth range (FR-016).
- [ ] T012 [US2] Add the three modulation `Lfo` instances (delay / cutoff / resonance) and their parameters (rate [hz, log], depth, shape [discrete 4]) to the `ModulatedDelayEffect` parameter table in `core/effects/modulated-delay/modulated-delay-effect.h`; tick the delay LFO per sample and sum onto the smoothed base delay before the fractional read; apply the cutoff/resonance LFOs to the `SvfPrimitive` coefficients at the per-sample (or documented sub-block) cadence (research Decision 4).
- [ ] T013 [US2] Verify depth-0 holds each destination static and that modulated reads stay in range at all rates/depths; run the US2 host-side tests green.

**Checkpoint**: US1 + US2 — a moving, warbling filtered delay.

---

## Phase 5: User Story 3 — Wow & flutter on the input (Priority: P3)

**Goal**: An input-stage tape-instability processor on its own delay line: a slow wow and a
faster flutter, each with independent rate and depth, imparted before the main delay.

**Independent Test**: Mostly-dry mix; raise wow depth → slow pitch drift; raise flutter depth
→ faster shimmer; both depths 0 → passthrough; with the delay audible the instability is in the
tail too; reads stay in range at all amounts.

- [ ] T014 [US3] Extend `tests/core/modulated-delay-test.cpp` — wow produces a slow periodic pitch drift; flutter produces a faster shallower shimmer; both depths 0 → input passthrough (FR-019); instability present in the delayed/feedback signal (FR-020); wow/flutter delay-line reads in range at all amounts (FR-021).
- [ ] T015 [US3] Implement the `WowFlutterStage` in `core/effects/modulated-delay/wow-flutter.h` — its own short per-channel `DelayLine` plus a slow wow `Lfo` and a faster flutter `Lfo`; modulate the read tap around a nominal center; depth-0 → output equals input (research Decision 6, no platform headers).
- [ ] T016 [US3] Add the wow/flutter parameters (wow_rate, wow_depth, flutter_rate, flutter_depth) to the parameter table and wire `WowFlutterStage` on the input path ahead of the main delay in `core/effects/modulated-delay/modulated-delay-effect.h`; run the US3 host-side tests green.

**Checkpoint**: All three stories — the full modulated delay with tape character.

---

## Phase 6: Polish & Cross-Cutting Concerns

**Purpose**: Cross-platform parity, sample-rate verification, and quality gates.

- [ ] T017 [P] Build the new effect into every target without source changes — `cmake --preset desktop && cmake --build --preset desktop` (workbench + plugin), `--preset daisy`, `--preset teensy`; confirm no JUCE in the MCU dependency graphs and that the 2 s buffer fits device RAM (bound at `prepare()` with a descriptive limit if a target cannot hold 2 s at its native rate — research Decision 8, FR-024, SC-008).
- [ ] T018 [P] Add/confirm sample-rate-independence fixtures at 44.1k/48k/96k in `tests/core/modulated-delay-test.cpp` (a given delay time + modulation rate yields the same musical result — SC-009).
- [ ] T019 [P] Walk `specs/modulated-delay/quickstart.md` scenarios end-to-end and confirm every item in `specs/modulated-delay/checklists/rt-safety.md` is satisfied by the delivered requirements/behavior.
- [ ] T020 [P] File-size + strict-typing pass — confirm each new unit (`delay-line.h`, `lfo.h`, `modulated-delay-effect.h`, `wow-flutter.h`) is ≤~500 lines and free of unchecked casts (Constitution VII, FR-025); split if needed.

---

## Dependencies & Execution Order

- **Setup (Phase 1)** → no dependencies; do first.
- **Foundational (Phase 2)** → depends on Setup. **Blocks all user stories.** T002–T006 are
  all `[P]` (distinct files).
- **US1 (Phase 3)** → depends on Foundational (uses `DelayLine`, `SvfPrimitive`, the time
  unit). The MVP. T007 (tests) → T008 → T009 → T010.
- **US2 (Phase 4)** → depends on Foundational (`Lfo`) and on the US1 effect/param table it
  extends. T011 (tests) → T012 → T013.
- **US3 (Phase 5)** → depends on Foundational (`DelayLine` + `Lfo`) and on the US1 effect it
  wires into. Independent of US2. T014 (tests) → T015 → T016.
- **Polish (Phase 6)** → depends on the stories being delivered; T017–T020 are `[P]`.

Story independence: US2 and US3 each extend the US1 effect but do not depend on each other —
they may be implemented in either order after US1.

## Parallel Execution Examples

- **Foundational**: run T002, T003, T005 in parallel (three distinct new/edited files), with
  T004 following T003 and T006 following T005 (each test after its primitive).
- **Polish**: T017, T018, T019, T020 can run in parallel.

## Implementation Strategy

- **MVP = User Story 1** (filtered-feedback delay). Ship/demonstrate it before layering
  modulation.
- **Incremental delivery**: Foundational → US1 (MVP) → US2 (movement/warble) → US3 (tape
  wow & flutter) → Polish. Each story is independently testable and demonstrable.
- **Tests first within each story** (T007/T011/T014 precede their implementation tasks),
  consistent with the project's host-side test discipline.
- **Commit & push after each task** (acfx Commandment 1) — small atomic increments.

## Task Summary

- **Total tasks**: 20
- **Setup**: 1 (T001)
- **Foundational**: 5 (T002–T006)
- **US1 (P1, MVP)**: 4 (T007–T010)
- **US2 (P2)**: 3 (T011–T013)
- **US3 (P3)**: 3 (T014–T016)
- **Polish**: 4 (T017–T020)
- **Parallel opportunities**: T002/T003/T005 (foundational), T017–T020 (polish), plus each
  story's test task is independent of the other stories.
- **Suggested MVP scope**: Phase 1 + Phase 2 + Phase 3 (US1).
