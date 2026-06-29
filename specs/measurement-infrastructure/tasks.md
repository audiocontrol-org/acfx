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

## Format: `[ID] [P?] [Story] Description`
- **[P]**: parallelizable (distinct files, no incomplete-task dependency)
- **[Story]**: US1–US4 (Setup/Foundational/Polish unlabeled)

## Path Conventions
Host-side test/support only — `tests/support/measurement/` + `tests/core/`. No `core/` audio
code, no adapters, no new dependency.

---

## Phase 1: Setup

- [ ] T001 Create `tests/support/measurement/` and register `tests/core/measurement-test.cpp` in the `acfx_core_tests` CMake target (mirror how `tests/core/svf-test.cpp` is registered in `tests/CMakeLists.txt`).

---

## Phase 2: Foundational (blocking prerequisites for all stories)

- [ ] T002 [P] Implement the stimulus generators in `tests/support/measurement/stimulus.h` per `contracts/stimulus.md` — Impulse/Step/Sine/Sweep/Noise (deterministic, seedable noise; `fill(span<float>)`); platform-independent, no audio-path code.
- [ ] T003 [P] Implement the effect-agnostic capture seam in `tests/support/measurement/analyzers.h` per `contracts/analyzer.md` — `capture(Effect, ctx, in, out)` and `captureCallable(fn, in, out)`.
- [ ] T004 [P] Stimulus-generator tests in `tests/core/measurement-test.cpp` — sine/step/impulse match closed-form; noise reproducible for a fixed seed and bounded (FR-001).

---

## Phase 3: User Story 1 — Effect-agnostic response (Priority: P1) 🎯 MVP

**Goal**: magnitude/frequency, impulse, and phase response measured against analytic bounds,
effect-agnostically. **Independent test**: measure SVF + a known callable with the same calls.

- [ ] T005 [US1] Implement `ImpulseAnalyzer` and `GoertzelAnalyzer` (magnitude + phase at a bin) in `tests/support/measurement/analyzers.h` per `contracts/analyzer.md`.
- [ ] T006 [US1] Implement the response metrics in `tests/support/measurement/metrics.h` — `magnitude`, `phaseRad`, and impulse-response capture — generalizing `svf-reference::measureMagnitude` to any Effect/callable (FR-005/006/007).
- [ ] T007 [US1] Tests in `tests/core/measurement-test.cpp`: Goertzel magnitude/phase match closed-form for a pure sine; SVF magnitude/impulse/phase asserted vs analytic bounds (passband≈unity, stopband attenuated) within named tolerances; the SAME calls measure a second effect/callable with no effect-specific code (FR-004, SC-001/002).

**Checkpoint**: reusable response measurement — MVP.

---

## Phase 4: User Story 2 — Distortion, delay, spectra (Priority: P2)

**Goal**: THD (Goertzel harmonics) + latency.

- [ ] T008 [US2] Implement `CorrelationAnalyzer` (delay lag) in `tests/support/measurement/analyzers.h` (FR-002).
- [ ] T009 [US2] Implement `thd(...)` (Goertzel over fundamental + harmonics) and `latencySamples(...)` (impulse-peak / correlation, accounting for the effect's own delay) in `tests/support/measurement/metrics.h` (FR-008/009).
- [ ] T010 [US2] Tests: THD ≈0 for a clean linear effect, elevated for a known nonlinearity (e.g. a hard-clip callable); latency matches a known processing delay within tolerance (SC-003).

**Checkpoint**: distortion + delay characterization.

---

## Phase 5: User Story 3 — Stability, allocation, cost (Priority: P2)

**Goal**: numerical stability (incl. silence/DC/denormal/idle), allocation, relative exec time.

- [ ] T011 [US3] Implement `stability(...)` in `tests/support/measurement/metrics.h` — NaN/Inf/denormal + bounds scan PLUS explicit silence-in→silence-out, DC-offset, denormal-prone, idle-noise-floor cases, returning a verdict + failed-case (FR-012).
- [ ] T012 [US3] Implement `relativeExecTime(...)` (desktop-relative host time-per-block, median of repeats, records block size; labeled a proxy) in `tests/support/measurement/metrics.h` (FR-010).
- [ ] T013 [US3] Tests: stability verdicts correct for each special case (no NaN/Inf/denormal); allocation == 0 via `tests/support/allocation-sentinel` around `process()` (FR-011); a relative-exec-time figure is produced and labeled desktop-relative (SC-004).

**Checkpoint**: RT-safety + stability + cost metrics.

---

## Phase 6: User Story 4 — Optional CSV report (Priority: P3)

**Goal**: opt-in CSV report; CI still gates on assertions only.

- [ ] T014 [US4] Implement `MeasurementRow` + `CsvReport` (add/write, well-formed header+rows) in `tests/support/measurement/report.h` per `contracts/metrics.md` (FR-014).
- [ ] T015 [US4] Tests: with emission on, a well-formed CSV is written; with it off (default), no file is written and assertions alone gate (SC-005).

**Checkpoint**: optional engineering artifact (the lab-reuse seam).

---

## Phase 7: Polish & Cross-Cutting

- [ ] T016 [P] Run `make test` — full host suite (existing + new measurement tests) green.
- [ ] T017 [P] Portability/scope check: `./scripts/check-portability.sh` passes; `git diff --name-only origin/main...HEAD` touches only `tests/support/measurement/`, `tests/core/`, `tests/CMakeLists.txt`, `specs/` (+ CLAUDE.md marker) — no `core/`/`adapters/` audio code, no new dependency, no general FFT (FR-015/016/019, SC-006).
- [ ] T018 [P] File-size + strict-typing pass: each new unit (`stimulus.h`, `analyzers.h`, `metrics.h`, `report.h`) ≤ ~500 lines, no unchecked casts (FR-018).
- [ ] T019 [P] Walk `quickstart.md`; confirm every Success Criterion (SC-001..007) is satisfied; confirm all eight Principle-X metrics are represented (SC-007).

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
