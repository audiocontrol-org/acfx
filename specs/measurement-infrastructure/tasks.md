---

description: "Task list for Measurement Infrastructure"
---

> ‼ **acfx COMMANDMENTS — non-negotiable** ‼
> **1. COMMIT AND PUSH EARLY AND OFTEN** — small atomic commits, pushed promptly.
> **2. NO GIT HOOKS, EVER** — this repo uses zero git hooks.
> **3. DESCRIPTIVE NAMES, NEVER NUMERIC PREFIXES** — datestamps excepted.
> (acfx Constitution, Principles I–III.)

# Tasks: Measurement Infrastructure

**Input**: Design docs in `specs/measurement-infrastructure/` (spec, plan, research, data-model, contracts, quickstart)

**Tests**: INCLUDED — Constitution VIII. The harness is test tooling, but it needs its OWN
correctness tests (analyzers/metrics asserted vs closed-form), plus an integration test that
exercises it on the real SVF + modulated-delay effects. doctest, host-side.

**Organization**: by user story (priority order). Harness lives in
`tests/support/measurement/`; the exercising test is `tests/core/measurement-test.cpp`.

## Format: `[ID] [P?] [Story] [tier:label] Description`
- **[P]**: parallelizable (distinct files, no incomplete-task dependency)
- **[Story]**: US1–US4 (Setup/Foundational/Polish unlabeled)
- **[tier:label]**: model-sized-dispatch tier (033) — `fast`/`balanced`/`powerful` resolve via `.stack-control/config.yaml` `tier_map`

## Path Conventions
Host-side test/support only — `tests/support/measurement/` + `tests/core/`. No `core/` audio
code, no adapters, no new dependency.

---

## Phase 1: Setup

- [x] T001 [tier:fast] Create `tests/support/measurement/` and register `tests/core/measurement-test.cpp` in the `acfx_core_tests` CMake target (mirror how `tests/core/svf-test.cpp` is registered in `tests/CMakeLists.txt`).

---

## Phase 2: Foundational (blocking prerequisites for all stories)

- [x] T002 [P] [tier:balanced] Implement the stimulus generators in `tests/support/measurement/stimulus.h` per `contracts/stimulus.md` — Impulse/Step/Sine/Sweep/Noise (deterministic, seedable noise; `fill(span<float>)`); platform-independent, no audio-path code.
- [x] T003 [P] [tier:balanced] Implement the effect-agnostic capture seam in `tests/support/measurement/analyzers.h` per `contracts/analyzer.md` — `capture(Effect, ctx, in, out)` and `captureCallable(fn, in, out)`.
- [x] T004 [P] [tier:balanced] Stimulus-generator tests in `tests/core/measurement-test.cpp` — sine/step/impulse match closed-form; noise reproducible for a fixed seed and bounded (FR-001).

---

## Phase 3: User Story 1 — Effect-agnostic response (Priority: P1) 🎯 MVP

**Goal**: magnitude/frequency, impulse, and phase response measured against analytic bounds,
effect-agnostically. **Independent test**: measure SVF + a known callable with the same calls.

- [x] T005 [US1] [tier:balanced] Implement `ImpulseAnalyzer` and `GoertzelAnalyzer` (magnitude + phase at a bin) in `tests/support/measurement/analyzers.h` per `contracts/analyzer.md`.
- [x] T006 [US1] [tier:balanced] Implement the response metrics in `tests/support/measurement/metrics.h` — `magnitude`, `phaseRad`, and impulse-response capture — generalizing `svf-reference::measureMagnitude` to any Effect/callable (FR-005/006/007).
- [x] T007 [US1] [tier:powerful] Tests in `tests/core/measurement-test.cpp`: Goertzel magnitude/phase match closed-form for a pure sine; SVF magnitude/impulse/phase asserted vs analytic bounds (passband≈unity, stopband attenuated) within named tolerances; **phase is reported NaN/skipped (never a spurious value) below the named magnitude floor** — asserted on a deep-stopband/silence input where the analyzed magnitude is near zero (FR-007 near-zero clause); the SAME calls measure a second effect/callable with no effect-specific code (FR-004, SC-001/002).

**Checkpoint**: reusable response measurement — MVP.

---

## Phase 4: User Story 2 — Distortion, delay, spectra (Priority: P2)

**Goal**: THD (Goertzel harmonics) + latency.

- [x] T008 [US2] [tier:balanced] Implement `CorrelationAnalyzer` (delay lag) in `tests/support/measurement/analyzers.h` (FR-002).
- [x] T009 [US2] [tier:balanced] Implement `thd(...)` (Goertzel over fundamental + harmonics) and `latencySamples(...)` (impulse-peak / correlation, accounting for the effect's own delay) in `tests/support/measurement/metrics.h` (FR-008/009).
- [x] T010 [US2] [tier:balanced] Tests: THD ≈0 for a clean linear effect, elevated for a known nonlinearity (e.g. a hard-clip callable); latency matches a known processing delay within tolerance (SC-003).

**Checkpoint**: distortion + delay characterization.

---

## Phase 5: User Story 3 — Stability, allocation, cost (Priority: P2)

**Goal**: numerical stability (incl. silence/DC/denormal/idle), allocation, relative exec time.

- [x] T011 [US3] [tier:balanced] Implement `stability(...)` in `tests/support/measurement/metrics.h` — NaN/Inf/denormal + bounds scan PLUS explicit silence-in→silence-out, DC-offset, denormal-prone, idle-noise-floor cases, returning a verdict + failed-case (FR-012).
- [x] T012 [US3] [tier:balanced] Implement `relativeExecTime(...)` (desktop-relative host time-per-block, median of repeats, records block size; labeled a proxy) in `tests/support/measurement/metrics.h` (FR-010).
- [x] T013 [US3] [tier:balanced] Tests: stability verdicts correct for each special case (no NaN/Inf/denormal); allocation == 0 via `tests/support/allocation-sentinel` around `process()` (FR-011); a relative-exec-time figure is produced and labeled desktop-relative (SC-004).

**Checkpoint**: RT-safety + stability + cost metrics.

---

## Phase 6: User Story 4 — Optional CSV report (Priority: P3)

**Goal**: opt-in CSV report; CI still gates on assertions only.

- [x] T014 [US4] [tier:balanced] Implement `MeasurementRow` + `CsvReport` (add/write, well-formed header+rows) in `tests/support/measurement/report.h` per `contracts/metrics.md` (FR-014).
- [x] T015 [US4] [tier:balanced] Tests: with emission on, a well-formed CSV is written; with it off (default), no file is written and assertions alone gate (SC-005).

**Checkpoint**: optional engineering artifact (the lab-reuse seam).

---

## Phase 7: Polish & Cross-Cutting

- [x] T016 [P] [tier:fast] Run `make test` — full host suite (existing + new measurement tests) green.
- [x] T017 [P] [tier:fast] Portability/scope check: `./scripts/check-portability.sh` passes; `git diff --name-only origin/main...HEAD` touches only `tests/support/measurement/`, `tests/core/`, `tests/CMakeLists.txt`, `specs/` (+ CLAUDE.md marker) — no `core/`/`adapters/` audio code, no new dependency, no general FFT (FR-015/016/019, SC-006).
- [x] T018 [P] [tier:fast] File-size + strict-typing pass: each new unit (`stimulus.h`, `analyzers.h`, `metrics.h`, `report.h`) ≤ ~500 lines, no unchecked casts (FR-018).
- [x] T019 [P] [tier:fast] Walk `quickstart.md`; confirm every Success Criterion (SC-001..007) is satisfied; confirm all eight Principle-X metrics are represented (SC-007).

---

## Dependencies & Execution Order
- Setup (T001) → first.
- Foundational (T002–T004) → before all stories; T002/T003 `[P]`, T004 after T002.
- US1 (T005–T007) → after Foundational. MVP. US2/US3/US4 build on the US1 analyzers/metrics file.
- US2 (T008–T010), US3 (T011–T013) → after US1; independent of each other (both extend metrics.h — sequence to avoid edit races, or split files).
- US4 (T014–T015) → after US1 (consumes metric outputs).
- Polish (T016–T019) → after stories; `[P]`.

## Parallel Execution Examples
- Foundational T002 (stimulus) ∥ T003 (capture) — distinct files.
- Polish T016–T019 in parallel.

## Implementation Strategy
- **MVP = US1** (effect-agnostic response). Then US2 (distortion/delay), US3 (stability/cost),
  US4 (CSV). Tests-first within each story (the analyzer/metric correctness test precedes/accompanies
  its implementation). Commit + push at each story boundary.

## Task Summary
- **Total**: 19 · Setup 1 · Foundational 3 · US1 3 · US2 3 · US3 3 · US4 2 · Polish 4
- **Parallel**: T002∥T003 (foundational), T016–T019 (polish).
- **MVP scope**: Setup + Foundational + US1.
