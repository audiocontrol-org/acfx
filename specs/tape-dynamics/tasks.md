---

description: "Task list for tape-dynamics implementation"
---

> ‼ **acfx COMMANDMENTS — non-negotiable** ‼
> **1. COMMIT AND PUSH EARLY AND OFTEN** — version control is a distributed, journaled
> filesystem that safeguards your work, **NOT a sacred rite reserved for the blessed.**
> Small atomic commits, pushed promptly; never hoard unpushed work.
> **2. NO GIT HOOKS, EVER** — this repo uses zero git hooks; none exist, none get added.
> **3. DESCRIPTIVE NAMES, NEVER NUMERIC PREFIXES** — names carry information; fake sequence
> numbers (`001-`) imply false order and false precision (datestamps excepted).
> (acfx Constitution, Principles I–III — `.specify/memory/constitution.md`.)

# Tasks: tape-dynamics — Hysteresis Primitive + TapeDynamicsEffect

**Input**: Design documents from `specs/tape-dynamics/`
**Prerequisites**: plan.md, spec.md, research.md, data-model.md, contracts/ (all present)
**Tests**: INCLUDED — the platform validates the core host-side with objective measurements
(Constitution VIII/X; spec FR-018..024, SC-001..007). Test tasks are first-class here.

**Organization**: grouped by user story (spec.md priorities). US1 + US2 are co-P1 (the MVP: the
graduated primitive and the effect that composes it).

## Format: `[ID] [P?] [Story?] [tier:label] Description with file path`

- **[P]**: parallelizable (different files, no dependency on an incomplete task).
- **[Story]**: user-story label (US1..US7) for story-phase tasks only.
- **[tier:label]**: model-sized-dispatch tier (033) — `fast`/`balanced`/`powerful` resolve via
  the installation `tier_map` (`fast→haiku`, `balanced→sonnet`, `powerful→opus`). `fast` = mechanical
  (scaffolds, wiring, README edits); `balanced` = standard implementation + integration + tests;
  `powerful` = subtle numerical correctness / broad-context composition (the JA math, solvers, and the
  oversampler-dispatch core).

## Path Conventions

acfx single C++ DSP core: `core/primitives/`, `core/effects/`, `core/labs/`, `tests/core/`,
`scripts/`. Paths per plan.md "Project Structure".

---

## Phase 1: Setup (Shared Infrastructure)

- [x] T001 [tier:fast] Create `core/effects/tape-dynamics/` module and wire it into `acfx_core` in `CMakeLists.txt` (headers: core/effect/parameters/presets).
- [x] T002 [P] [tier:fast] Scaffold the lab `core/labs/tape-dynamics/` (`README.md` stub, `kernel/`, `harness/`) and add the host-only harness build target in `CMakeLists.txt`.
- [x] T003 [P] [tier:fast] Create `tests/core/hysteresis-test.cpp` skeleton and register it (plus placeholder `tape-dynamics-effect-test.cpp`, `tape-dynamics-alias-test.cpp`) in the test CMake wiring.
- [x] T004 [P] [tier:balanced] Extend `scripts/check-portability.sh` to cover `core/primitives/nonlinear/hysteresis.h`, `core/effects/tape-dynamics/`, and `core/labs/tape-dynamics/` (host-only lab isolation).

---

## Phase 2: Foundational (blocking prerequisites — MUST complete before user stories)

**Purpose**: the shared Jiles-Atherton substrate every story builds on.

- [x] T005 [tier:balanced] Define `Solver` enum, `JAParams` struct, and the `Hysteresis` class shell (state `M`/`Hprev`, `prepare(sampleRate)`, `reset()`, per-parameter setters, `setSolver`) in `core/primitives/nonlinear/hysteresis.h` (data-model "Hysteresis"; contract C2/C6).
- [x] T006 [tier:powerful] Implement the shared derivative `double dMdH(double H, double M, double dH) const` — Langevin `L(x)=coth(x)−1/x` with the small-`x` series near 0, effective field `H_e=H+α·M`, anhysteretic `M_an=Ms·L(H_e/a)`, and the irreversible+reversible split — in `core/primitives/nonlinear/hysteresis.h` (research R1).

**Checkpoint**: the JA model + types compile; no solver/guard yet.

---

## Phase 3: User Story 2 — Graduated stateful `Hysteresis` primitive + lab (Priority: P1)

**Goal**: deliver the reusable, tested stateful primitive and its lab (the concept's graduation).
**Independent test**: `nonlinear/hysteresis.h` exists and is README-listed; a sinusoid yields a closed
`M`-vs-`H` loop with area > 0; `reset()` reproducible; all three solvers finite.

- [x] T007 [US2] [tier:powerful] Implement the RK2 and RK4 explicit steppers over `dMdH` in `core/primitives/nonlinear/hysteresis.h` (research R3).
- [x] T008 [US2] [tier:powerful] Implement the Newton-Raphson implicit stepper — bounded fixed iteration count + divergence bail to the explicit estimate — in `core/primitives/nonlinear/hysteresis.h` (research R3/R5; Constitution VI).
- [x] T009 [US2] [tier:balanced] Implement the stiff-solver stability guard (`std::isfinite` + `Ms`-multiple clamp, reset-to-last-finite) inside `Hysteresis::process(float H)` in `core/primitives/nonlinear/hysteresis.h` (FR-006; contract C3).
- [x] T010 [P] [US2] [tier:balanced] Write `core/labs/tape-dynamics/README.md` — JA `dM/dH`, Langevin curve, explicit-vs-implicit solver tradeoff + order-of-accuracy/stability under oversampling, emergent compression, and why ADAA does not apply (state carries across samples) (FR-015).
- [x] T011 [P] [US2] [tier:balanced] Implement the RT-safe lab kernel in `core/labs/tape-dynamics/kernel/` mirroring the primitive (the graduation source) (FR-015; Constitution IX).
- [x] T012 [US2] [tier:balanced] `tests/core/hysteresis-test.cpp`: closed-loop area > 0 vs a static-waveshaper area ≈ 0 (SC-001); `reset()` reproducibility (FR-003); `k` widens / `Ms` raises the loop (US2.2); all three solvers finite on a hot transient (SC-005).
- [x] T013 [US2] [tier:fast] Edit `core/primitives/README.md`: list `nonlinear/hysteresis.h` as the **first stateful** inhabitant of `nonlinear/`, with lab + consumers referenced (FR-016; SC-006).

**Checkpoint**: US2 independently testable and green.

---

## Phase 4: User Story 1 — Process a signal through magnetic hysteresis (Priority: P1)

**Goal**: the MVP effect — audio through the JA core under oversampling.
**Independent test**: a full-scale sinusoid at moderate `drive` yields a saturated, band-limited output
tracing a closed loop; `drive`=0 is unity; a hot transient stays finite.

- [x] T014 [US1] [tier:balanced] Implement `TapeDynamicsParameters` (`drive`, `saturation`→`Ms`, `width`→`k`, `mix`, `output`; `solver`/`oversampling` fields present, default 8×) with descriptors in `core/effects/tape-dynamics/tape-dynamics-parameters.h` (FR-010; data-model).
- [x] T015 [US1] [tier:powerful] Implement `TapeDynamicsCore<Factor>` at the default factor: `x·drive → Oversampler<Factor>::process(·, JA step) → mix(dry,wet)·output`, per-channel `Hysteresis`, in `core/effects/tape-dynamics/tape-dynamics-core.h` (FR-009; contract E4).
- [x] T016 [US1] [tier:balanced] Implement the `TapeDynamicsEffect` host wrapper (`prepare(ProcessContext)`/`process(AudioBlock)`/`reset()`, Effect concept) in `core/effects/tape-dynamics/tape-dynamics-effect.h` (FR-008).
- [x] T017 [US1] [tier:balanced] `tests/core/tape-dynamics-effect-test.cpp`: closed hysteresis loop at moderate drive (US1.1); `drive`=0 unity passthrough (US1.2, E2); hot-transient finiteness (US1.3, E3).

**Checkpoint**: MVP (US1+US2) delivers a working tape-dynamics effect.

---

## Phase 5: User Story 3 — Choose the numerical solver (Priority: P2)

**Goal**: runtime solver selection as an accuracy/CPU control.
**Independent test**: each solver produces stable output; loops agree within tol and tighten as
oversampling rises; none diverge on a transient.

- [x] T018 [US3] [tier:balanced] Wire the `solver` parameter through `tape-dynamics-parameters.h` and `TapeDynamicsCore` to `Hysteresis::setSolver` in `core/effects/tape-dynamics/tape-dynamics-core.h`.
- [x] T019 [US3] [tier:balanced] Extend `tests/core/hysteresis-test.cpp`: RK2/RK4/Newton loop agreement within a stated tolerance, tightening with oversampling; no divergence on a hot transient (SC-002; contract C4).

---

## Phase 6: User Story 4 — Emergent dynamic compression (Priority: P2)

**Goal**: measure/teach the compression that emerges from the magnetics (no control path).
**Independent test**: trim OFF, level sweep → monotonic compressive curve; DRR rises with drive.

- [x] T020 [US4] [tier:balanced] Add the closed-loop-area and dynamic-range-reduction metrics to `core/labs/tape-dynamics/harness/tape-dynamics-harness.cpp` (research R9).
- [x] T021 [US4] [tier:balanced] Extend `tests/core/tape-dynamics-effect-test.cpp`: with `trim.enabled=false`, output-vs-input level curve monotonic + compressive above threshold; DRR(high drive) > DRR(low drive) (SC-003); assert no "compression" parameter exists (FR-012, US4.3).

---

## Phase 7: User Story 5 — Safe host-facing effect wrapper (Priority: P2)

**Goal**: RT-safe, lock-free, block-robust wrapper conforming to the Effect concept.
**Independent test**: no allocation/locks in `process()`; click-free across block sizes incl. 1 and large.

- [x] T022 [US5] [tier:balanced] Implement lock-free parameter handoff (consume edits at the top of `process()`) and block-boundary continuity in `core/effects/tape-dynamics/tape-dynamics-effect.h` (FR-008; contract E8).
- [x] T023 [US5] [tier:balanced] Extend `tests/core/no-allocation-test.cpp`: zero heap allocation and no locks in `TapeDynamicsEffect::process()` across all configs; correct output for 1-sample and large blocks (SC-007, US5).

---

## Phase 8: User Story 7 — Oversampling factor menu (Priority: P2)

**Goal**: expose {2×,4×,8×} (default 8×) with runtime dispatch; control aliasing.
**Independent test**: alias metric falls as factor rises; JA runs as the oversampler's `evalAtHighRate`.

- [x] T024 [US7] [tier:powerful] Instantiate `Oversampler<2>/<4>/<8>` in `TapeDynamicsCore` and dispatch on the runtime `oversampling` parameter (default 8×) in `core/effects/tape-dynamics/tape-dynamics-core.h` and `-parameters.h` (FR-010; research R4).
- [x] T025 [US7] [tier:balanced] `tests/core/tape-dynamics-alias-test.cpp`: alias-sweep metric decreases monotonically as the factor rises (SC-004, US7.1); assert the JA step runs strictly as `Oversampler<Factor>::process`'s `evalAtHighRate` callable (US7.2, E4).

---

## Phase 9: User Story 6 — Optional explicit envelope-driven trim (Priority: P3)

**Goal**: layer an optional tape-leveling trim composing shipped `EnvelopeFollower`+`GainComputer`.
**Independent test**: disabled → bit-exact core; enabled → envelope-driven GR tracks attack/release.

- [x] T026 [US6] [tier:balanced] Compose `EnvelopeFollower` + `GainComputer` as the optional trim (`trim.enabled/attack/release/amount`) in `core/effects/tape-dynamics/tape-dynamics-core.h`; guarantee a bit-exact bypass when disabled (FR-011; contract E7).
- [x] T027 [US6] [tier:balanced] Extend `tests/core/tape-dynamics-effect-test.cpp`: trim-off path bit-exact the magnetics-only core; trim-on envelope-driven gain reduction follows the attack/release controls (US6).

---

## Phase 10: Polish & Cross-Cutting Concerns

- [x] T028 [P] [tier:fast] Finalize `core/effects/tape-dynamics/tape-dynamics-presets.h` named starting points (e.g. gentle "glue" vs aggressive "saturate") (FR-013).
- [x] T029 [P] [tier:balanced] Add THD/alias reporting via `host/analysis/thdn.h` + `alias-sweep.h` to the harness output (FR-021).
- [x] T030 [P] [tier:balanced] Run `scripts/check-portability.sh` over the new paths and fix any layering/platform leak (FR-017, SC-006).
- [x] T031 [P] [tier:balanced] `quickstart.md` validation pass: `make test`, the harness, and the portability gate all green.

---

## Dependencies & Story Completion Order

- **Setup (P1–P4 tasks T001–T004)** → **Foundational (T005–T006)** block everything.
- **US2 (T007–T013)** and **US1 (T014–T017)** are co-P1: US1 depends on the primitive from US2 (T007–T009). Complete US2's solver/guard before US1's core.
- **US3, US4, US5, US7 (P2)** each depend only on the MVP (US1+US2); mutually independent → parallelizable across contributors.
- **US6 (P3)** depends on the MVP core (T015).
- **Polish (T028–T031)** last.

MVP = **US2 + US1** (T001–T017). Everything after is incremental.

## Parallel Opportunities

- Setup: T002, T003, T004 in parallel (distinct files) after T001.
- US2: T010 (README) and T011 (lab kernel) in parallel with the solver work; T007→T008→T009 are sequential (same header).
- Across stories once MVP lands: US3 (T018–T019), US4 (T020–T021), US5 (T022–T023), US7 (T024–T025) can proceed in parallel.
- Polish: T028–T031 all [P].

Note: tasks touching the **same file** are sequential — `hysteresis.h` (T005–T009, extended T019) and `tape-dynamics-effect-test.cpp` (T017, extended T021/T027) must not be parallelized against each other.

## Implementation Strategy

Ship the **MVP first**: Setup → Foundational → US2 (primitive+lab) → US1 (effect) → validate the closed
loop, unity passthrough, and finiteness. Then layer P2 stories (solver selection, emergent-compression
measurement, wrapper hardening, oversampling menu) as independent increments, US6 (optional trim) at P3,
and finish with polish. Commit atomically per task and push promptly (Constitution I).
