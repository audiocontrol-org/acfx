---
description: "Task list for waveshapers feature implementation"
---

> ‼ **acfx COMMANDMENTS — non-negotiable** ‼
> **1. COMMIT AND PUSH EARLY AND OFTEN** — version control is a distributed, journaled
> filesystem that safeguards your work, **NOT a sacred rite reserved for the blessed.**
> Small atomic commits, pushed promptly; never hoard unpushed work.
> **2. NO GIT HOOKS, EVER** — this repo uses zero git hooks; none exist, none get added.
> **3. DESCRIPTIVE NAMES, NEVER NUMERIC PREFIXES** — names carry information; fake sequence
> numbers (`001-`) imply false order and false precision (datestamps excepted).
> (acfx Constitution, Principles I–III — `.specify/memory/constitution.md`.)

# Tasks: Waveshapers — Nonlinear Memoryless Primitive

**Input**: Design documents from `specs/waveshapers/`

**Prerequisites**: plan.md, spec.md, research.md, data-model.md, contracts/waveshaper-api.md, quickstart.md

**Tests**: INCLUDED. Constitution X makes objective measurement the acceptance evidence, and every
user story's Independent Test is measurement-based — so test tasks are first-class and authored
before the implementation they validate.

**Organization**: Tasks grouped by user story (US1–US5 from spec.md) for independent implementation
and testing. Pre-graduation everything lives under `core/labs/waveshaping/`; Phase US5 graduates the
kernel into `core/primitives/nonlinear/`.

**First graduated cut (resolves the design Open Question, sequencing-only)**: US1 lands `tanh`,
`hardClip`, and `cubicSoft` as the first shapes; US2 completes the full catalog. No scope is cut —
the full catalog is delivered across US1+US2; only the order is decided here.

## Format: `[ID] [P?] [Story] [tier:label] Description`

- **[P]**: can run in parallel (different files, no dependency on an incomplete task)
- **[Story]**: US1–US5; Setup/Foundational/Polish carry no story label
- **[tier:label]**: model-sized-dispatch tier (033) — `fast`/`balanced`/`powerful` resolve via `.stack-control/config.yaml` `tier_map`

## Path Conventions

- Lab (pre-graduation): `core/labs/waveshaping/` (kernel headers + `harness/`, `README.md`)
- Primitive (post-graduation target): `core/primitives/nonlinear/`
- Tests: `tests/core/` (doctest); shared helpers: `tests/core/measurement-support.h`
- Gate: `scripts/check-portability.sh`

---

## Phase 1: Setup (Shared Infrastructure)

**Purpose**: directories, build wiring, and the lab skeleton everything else builds on.

- [ ] T001 [tier:fast] Create the lab directory `core/labs/waveshaping/` (with `harness/`) and the empty primitive category `core/primitives/nonlinear/`; record `nonlinear/` in the primitive taxonomy doc (`core/primitives/README.md`).
- [ ] T002 [tier:balanced] Wire CMake (`cmake/acfx-effect-targets.cmake` / `CMakeLists.txt` as appropriate) to register the new `tests/core/waveshaper-*-test.cpp` doctest suites and a host-only harness target `acfx_lab_waveshaping_harness`.
- [ ] T003 [P] [tier:fast] Author the `core/labs/waveshaping/README.md` skeleton: theory placeholder + walkthrough outline naming `core/primitives/nonlinear/` as the graduation target (filled in T022).

---

## Phase 2: Foundational (Blocking Prerequisites)

**Purpose**: the memoryless shape contract surface, the wrapper's stateful primitives (DC-block,
gain-comp), and the shared test helpers all stories depend on.

**⚠️ No user-story work begins until this phase is complete.**

- [ ] T004 [tier:balanced] Define the memoryless contract surface in `core/labs/waveshaping/waveshaper-shapes.h`: `enum class Shape`, `enum class Evaluation`, and the `acfx::shape::*` pure-function declarations (no bodies yet) per `contracts/waveshaper-api.md` — pure `float→float`, no state, no DC-block.
- [ ] T005 [P] [tier:balanced] Implement the one-pole DC-blocker and the gain-compensation factor as wrapper-owned helpers in `core/labs/waveshaping/waveshaper.h` (declarations + state members; bodies wired in US1). DC-blocker MUST NOT live in `acfx::shape::*` (FR-008).
- [ ] T006 [P] [tier:powerful] Extend `tests/core/measurement-support.h` with waveshaper helpers reused across stories: pure-tone harmonic-signature capture (Goertzel/THD), inharmonic-energy (aliasing) measure, and a DC-offset measure — analytic-bound assertion style (no fabricated numbers).

---

## Phase 3: User Story 1 — Apply a memoryless transfer function to audio (Priority: P1) 🎯 MVP

**Goal**: a working `Waveshaper` wrapper applying a selected shape with drive/bias/gain-comp and a
DC-free output.

**Independent test**: drive a sine through the wrapper; assert the harmonic series matches the
analytic prediction within tolerance and the output is DC-free (US1 acceptance scenarios).

- [ ] T007 [US1] [tier:balanced] Write `tests/core/waveshaper-test.cpp`: signal-chain order (`drive·x+bias → shape → dcBlock → gainComp`), silence-in→silence-out, asymmetric-bias DC-free output, gain-compensation-toward-unity, no-stale-state-on-reset (FR-007/008/009, SC-002/005).
- [ ] T008 [US1] [tier:balanced] Write `tests/core/waveshaper-harmonics-test.cpp` (US1 slice): odd-only harmonics for a symmetric shape and even+odd for a biased shape, via the T006 helpers (SC-001/002).
- [ ] T009 [US1] [tier:balanced] Implement the first-cut closed-form shapes in `waveshaper-shapes.h`: `tanhShape`, `hardClip`, `cubicSoftClip` (bounded for all finite inputs).
- [ ] T010 [US1] [tier:powerful] Implement `Waveshaper` in `core/labs/waveshaping/waveshaper.h`: `init/setShape/setEvaluation(closedForm)/setDrive/setBias/setGainCompensation/reset/process`, RT-safe (`noexcept`, no alloc/lock), with the T005 DC-blocker.
- [ ] T011 [US1] [tier:balanced] Implement the default gain-compensation law (per research.md Decision; document which law) and make T007/T008 pass; assert RT-safety via the allocation sentinel.

**Checkpoint**: US1 is an independently demonstrable MVP — a usable nonlinear primitive.

---

## Phase 4: User Story 2 — Select any transfer function from a documented catalog (Priority: P1)

**Goal**: the full transfer-function catalog as pure functions, runtime-selectable on the wrapper.

**Independent test**: each pure shape matches its closed-form definition across the input domain;
runtime shape switching carries no stale state (US2 acceptance scenarios).

- [ ] T012 [US2] [tier:balanced] Write `tests/core/waveshaper-shapes-test.cpp`: per-shape analytic correctness (range, symmetry class, monotonicity, anchor points from research.md Decision 1) for every catalog member.
- [ ] T013 [US2] [tier:powerful] Implement the remaining catalog shapes in `waveshaper-shapes.h`: `arctanShape`, `algebraic`, `softKnee`, `chebyshev(n)`, `biasedAsym`, `diodeCurve`, `sineFold`, `triangleFold` (split into a second header if the size budget nears 500 lines — FR-023).
- [ ] T014 [US2] [tier:balanced] Wire all catalog members into the `Shape` enum dispatch in `Waveshaper`; add the runtime-switch no-stale-state assertion and document the `diodeCurve` boundary (memoryless curve ≠ circuit-solved diode-clipper, FR-004) in the README + header comment.

**Checkpoint**: US1 + US2 deliver the full catalog and the runtime-selectable wrapper.

---

## Phase 5: User Story 3 — Choose between exact and table-based evaluation (Priority: P2)

**Goal**: a LUT evaluation backend bounded against the closed-form reference.

**Independent test**: max LUT-vs-closed-form deviation ≤ named bound at the stated resolution; table
built in `init()`, never in `process()` (US3 acceptance scenarios).

- [ ] T015 [US3] [tier:balanced] Write `tests/core/waveshaper-lut-test.cpp`: per-shape max deviation from closed-form (the reference) ≤ named interpolation-error bound at a stated resolution; assert no per-sample allocation (allocation sentinel) (SC-004, FR-011/012).
- [ ] T016 [US3] [tier:balanced] Implement LUT support in `core/labs/waveshaping/waveshaper-lut.h`: fixed-size table built in `init()`, linear interpolation, edge-clamp out-of-domain policy (defined, bounded — not a silent fallback).
- [ ] T017 [US3] [tier:balanced] Wire `Evaluation::lut` into `Waveshaper::process` (select backend without per-sample branching cost where avoidable) and make T015 pass.

---

## Phase 6: User Story 4 — Reduce aliasing with an opt-in ADAA variant (Priority: P2)

**Goal**: `ADAAWaveshaper` (first-order) reducing aliasing for covered shapes, layered around the
memoryless contract.

**Independent test**: ADAA inharmonic energy lower than naive by ≥ named margin for a covered
aggressive shape; an uncovered shape raises a descriptive error (US4 acceptance scenarios).

- [ ] T018 [US4] [tier:balanced] Write `tests/core/waveshaper-adaa-test.cpp`: high-frequency stimulus, naive-vs-ADAA inharmonic-energy comparison (≥ named margin, SC-003); assert selecting an antiderivative-less shape raises a descriptive error (naive-only, Constitution V); assert the base `Shape`/`Waveshaper` are unchanged.
- [ ] T019 [US4] [tier:powerful] Implement antiderivatives in `waveshaper-shapes.h` for the covered shapes (e.g. `tanhAntideriv = log(cosh)`, `hardClipAntideriv`, ...); flag uncovered shapes explicitly.
- [ ] T020 [US4] [tier:powerful] Implement `ADAAWaveshaper` in `core/labs/waveshaping/adaa-waveshaper.h`: first-order `(F(u)−F(uPrev))/(u−uPrev)` with the small-denominator midpoint fallback, same drive/bias/DC-block/gain-comp staging, history reset; refuse uncovered shapes. (Second-order ADAA left as the documented Open Question.)

---

## Phase 7: User Story 5 — Learn from the lab + graduate the kernel (Priority: P2)

**Goal**: the lab harness produces harmonic evidence; the kernel graduates into the primitive layer;
the portability gate covers the new locations.

**Independent test**: the harness regenerates per-shape harmonic evidence + naive-vs-ADAA comparison
host-side; the graduated primitive is the relocated lab kernel; the gate passes (US5 acceptance
scenarios).

- [ ] T021 [US5] [tier:balanced] Implement `core/labs/waveshaping/harness/waveshaping-harness.cpp`: drive each shape, emit per-shape harmonic signatures and the naive-vs-ADAA aliasing comparison via the measurement infra (oversampled arm contingent only — FR-018); host-only target.
- [ ] T022 [US5] [tier:balanced] Complete `core/labs/waveshaping/README.md`: theory + walkthrough + the measured evidence, naming the graduation target.
- [ ] T023 [US5] [tier:balanced] Extend `scripts/check-portability.sh` to cover `core/labs/waveshaping/**` and `core/primitives/nonlinear/**` for harness-isolation (C-1/C-3), dependency-direction (C-2), platform-independence, and file-size (FR-022); run it green.
- [ ] T024 [US5] [tier:balanced] Graduate: `git mv` the kernel headers (`waveshaper-shapes.h`, `waveshaper.h`, `adaa-waveshaper.h`, `waveshaper-lut.h`) from `core/labs/waveshaping/` into `core/primitives/nonlinear/`; update `#include` paths in tests + harness; confirm the lab persists as README + harness now driving the graduated primitive; full suite + gate green (SC-007).

---

## Phase 8: Polish & Cross-Cutting Concerns

- [ ] T025 [P] [tier:fast] (Open question) Optional CSV harmonic-spectrum dump from the harness for cross-lab comparison; gate it behind a flag so the default run stays assertion-only.
- [ ] T026 [P] [tier:fast] Finalize the taxonomy/README cross-references (effect-consumes-primitive convention note) and the diode-altitude boundary statement.
- [ ] T027 [tier:balanced] Verify the full `ctest --preset test` suite, `scripts/check-portability.sh`, and the `daisy`/`teensy` cross-compiles are green (SC-006); confirm zero unpushed commits.

---

## Dependencies & Execution Order

- **Setup (P1–3)** → **Foundational (T004–T006)** block everything.
- **US1 (P1, MVP)** depends only on Foundational. **US2 (P1)** depends on Foundational + the `Shape`
  dispatch from US1 (T010). **US3/US4 (P2)** depend on US1 (wrapper) and the catalog/antiderivatives
  from US1/US2. **US5 (P2)** depends on US1–US4 existing (harness exercises them) and performs the
  graduation last.
- **Polish (P8)** last.

**Story completion order**: US1 → US2 → (US3 ∥ US4) → US5 → Polish.

## Parallel Opportunities

- Setup: T003 ∥ T001/T002 wiring.
- Foundational: T005 ∥ T006 (different files).
- US3 and US4 are independent of each other (different files/types) once US1+US2 land — their phases
  can proceed in parallel.
- Within a story, the test task is authored first, then implementation; the per-shape implementations
  in T013 and the antiderivatives in T019 are internally parallelizable by shape.

## Implementation Strategy

- **MVP = US1** (Phase 3): a usable memoryless nonlinearity with gain-staging — independently
  demonstrable.
- **Incremental delivery**: US2 completes the catalog; US3 adds the MCU-friendly LUT backend; US4
  adds ADAA; US5 produces the lab evidence and graduates the kernel. Commit and push per task.

## Total

- **27 tasks**: Setup 3, Foundational 3, US1 5, US2 3, US3 3, US4 3, US5 4, Polish 3.
