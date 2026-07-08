---
description: "Task list — Newton–Raphson iteration primitive"
---

> ‼ **acfx COMMANDMENTS — non-negotiable** ‼
> **1. COMMIT AND PUSH EARLY AND OFTEN** — version control is a distributed, journaled
> filesystem that safeguards your work, **NOT a sacred rite reserved for the blessed.**
> Small atomic commits, pushed promptly; never hoard unpushed work.
> **2. NO GIT HOOKS, EVER** — this repo uses zero git hooks; none exist, none get added.
> **3. DESCRIPTIVE NAMES, NEVER NUMERIC PREFIXES** — names carry information; fake sequence
> numbers (`001-`) imply false order and false precision (datestamps excepted).
> (acfx Constitution, Principles I–III — `.specify/memory/constitution.md`.)

# Tasks: Newton–Raphson iteration primitive

**Input**: Design documents in `specs/newton-iteration/`
(plan.md, spec.md, research.md, data-model.md, contracts/, quickstart.md).

**Tests**: TDD is REQUESTED (acfx Principle VIII — test the core host-side; the recorded
`circuit-model-validation-approach`). Every implementation task is preceded by a failing
doctest task in the same story.

**Organization**: grouped by the spec's user stories. The `NewtonSolver` types + two-phase
scaffolding are Foundational — every story depends on them. `[tier:]` hints drive
model-sized dispatch in `/stack-control:execute`.

**Out of scope (do NOT implement here)**: lab migration onto `NewtonSolver` (backlog
TASK-14); a gmin / source-stepping / any silent convergence fallback; damping for non-diode
nonlinearities; complex/AC scalar. These are captured follow-on / open questions, not this
feature. The primitive DRIVES the shipped MNA core and does not modify it.

## Format: `[ID] [P?] [Story] [tier:] Description + file path`

- **[P]**: parallelizable (different file, no incomplete-task dependency).
- **[Story]**: US1..US7 from spec.md (Setup/Foundational/Polish carry no story label).

---

## Phase 1: Setup (shared infrastructure)

- [ ] T001 [tier:fast] Inhabit the primitive directory `core/primitives/circuit/newton/` (create it in this commit — "inhabit before creating"; no empty pre-creation) and add a short `core/primitives/circuit/newton/README.md` stating: production primitive, namespace `acfx::newton`, single `NewtonSolver` type driving the shipped MNA core, general multi-diode charter, stateless two-phase RT-safe contract, no-fallback, lab migration is out-of-scope follow-on (TASK-14).
- [ ] T002 [P] [tier:fast] Register the five host test sources (`newton-solver-test.cpp`, `newton-closed-form-test.cpp`, `newton-invariants-test.cpp`, `newton-equivalence-test.cpp`, `newton-nofallback-test.cpp`) in `tests/CMakeLists.txt`, mirroring the existing `mna-*` registrations. Files may be empty stubs at this point.

---

## Phase 2: Foundational — `NewtonSolver` types + two-phase scaffolding — BLOCKS all stories

**Goal**: the type surface (`NewtonStatus`, `ComposedCompanionSupply`, `NewtonSolver`),
construction validation, and the throw-permitted `plan()` (delegate to `MnaAssembler::plan`
+ scan diodes into the is-diode mask / diode-index table). Independent test: invalid
construction throws; `plan()` builds the mask; `solve()` before `plan()` is a surfaced
precondition violation. Contract: `contracts/newton-solver.md`.

- [ ] T003 [tier:balanced] Write FAILING doctest `tests/core/newton-solver-test.cpp`: (a) constructing with `maxIterations < 1` or non-positive `voltageTol`/`currentTol` throws `std::invalid_argument` (C1); (b) after `plan()` on a netlist with diodes at known component indices, the is-diode mask and diode-index table match the netlist scan (`planned()` true); (c) calling `solve()` before `plan()` returns `NewtonStatus{converged=false, iterations=0}` by value (deterministic, throw-free, not UB — contract S10). Tests fail until T004.
- [ ] T004 [tier:powerful] Implement `core/primitives/circuit/newton/newton-solver.h` scaffolding — in `acfx::newton`: `struct NewtonStatus{bool converged;int iterations;double voltageResidual;double currentResidual;}`; `template<class Base> ComposedCompanionSupply` with `Companion at(int i) const noexcept` = `isDiode_[i] ? diodeCompanion_[i] : base_.at(i)`; `template<int MaxNodes,int MaxComponents,int MaxBranches> class NewtonSolver` ctor validating C1 (throw); `plan(nl, assembler, sys)` delegating to `MnaAssembler::plan` then scanning the netlist once to fill `isDiode_`, `diodeComponentIndex_`, `diodeCount_`, setting `planned_`; `planned()` accessor + a `solve()`-before-`plan()` guard (returns `NewtonStatus{converged=false, iterations=0}` by value per contract S10, not a throw/UB). Header-only, C++17, no platform headers, ≤ ~300 lines. Make T003 pass. Contract: `contracts/newton-solver.md` (C1, P1–P3).

**Checkpoint**: the type surface + plan phase exist; the diode topology is fixed once, off the hot path.

---

## Phase 3: User Story 1 — Solve a single-diode network to convergence (P1) — MVP

**Goal**: the `solve()` Newton loop on one diode. Independent test: diode + series resistor
at several DC levels matches the exact operating point; zero-diode netlist → one linear solve.
Contract: `contracts/newton-solver.md` (S1–S9).

- [ ] T005 [US1] [tier:balanced] Write FAILING doctest `tests/core/newton-closed-form-test.cpp`: single diode + series resistor + DC source at several forward/reverse levels → converged `MnaSystem::nodeVoltage` matches an in-test Lambert-W / independently-iterated reference operating point to tolerance (start `1e-9` relative), transfer curve monotonic; a **zero-diode** netlist → exactly one solve, `iterations == 1`, exact linear result (S6). Fails until T006.
- [ ] T006 [US1] [tier:powerful] Implement `NewtonSolver::solve(nl, base, initialNodeVoltages, assembler, sys)`: seed diode biases from the node-voltage guess (S9); iterate ≤ `maxIterations`: for each diode read `vAK = nodeVoltage(anode) − nodeVoltage(cathode)`, `{I,g} = Diode::evaluate(vAK)`, set `diodeCompanion_[idx] = {Geq:g, Ieq:g·vAK − I}` (S1; sign per the shipped MnaAssembler's `i = Geq·(V(a)−V(c)) − Ieq`); `MnaAssembler::refresh(nl, ComposedCompanionSupply{base, diodeCompanion_, isDiode_}, sys)` (S2); `if(!sys.solve()) return surfaced-failure` (S7); read voltages; `pnjlim` damp each `vAK` via `Diode::limitJunctionVoltage(vNew, prevBias)` (S4); gate `max|Δv| < voltageTol` (S5). Return `NewtonStatus`. Header ≤ budget. Make T005 pass.
- [ ] T007 [US1] [tier:balanced] Extend `newton-closed-form-test.cpp`: symmetric antiparallel diode pair across a resistor at **zero drive** → port voltage exactly 0 V to solver tolerance (symmetry); assert `currentResidual` is populated in `NewtonStatus` but never used as a convergence gate (S5, FR-011).

**Checkpoint**: US1 is an independently demonstrable MVP — a diode network solved to its exact operating point.

---

## Phase 4: User Story 2 — Solve coupled multi-diode networks (P1)

**Goal**: the global multi-diode step lifts the lab's single-nonlinearity refusal.
Independent test: antiparallel pair / string / bridge solve; ≥2 interacting nonlinearities
NOT refused.

- [ ] T008 [US2] [tier:balanced] Write FAILING doctest in `tests/core/newton-solver-test.cpp`: an antiparallel pair, a longer antiparallel string, and a bridge (4 diodes) — i.e. ≥2 interacting nonlinearities at distinct node pairs — each solve (or report non-convergence honestly) and are **never refused** (contrast the lab, which throws); assert all diodes are linearized against the same iterate and updated within one `sys.solve()` per iteration (S3). Fails until T009.
- [ ] T009 [US2] [tier:balanced] Verify/extend `solve()` so the per-iteration loop covers all `diodeCount_` diodes as one global Newton step (no per-diode sequencing); confirm no charter refusal exists anywhere in the primitive. Make T008 pass.

**Checkpoint**: US2 done — the capability gain over the lab is demonstrable.

---

## Phase 5: User Story 3 — Compose with a caller-supplied base companion supply (P1)

**Goal**: prove `ComposedCompanionSupply` delegates non-diode indices to the base supply and
overrides diode indices, holding the base fixed for the solve (D6 / FR-006/007).

- [ ] T010 [US3] [tier:balanced] Write FAILING doctest in `tests/core/newton-solver-test.cpp`: a hand-written base supply returns a fixed `Companion` for a designated reactive component slot; Newton overrides the diode slots → across iterations the base slot's companion passes through unchanged while diode companions update; an **empty** base supply (v1 DC case) → only diode indices are populated and the solve proceeds. Fails until T011.
- [ ] T011 [US3] [tier:balanced] Confirm `ComposedCompanionSupply::at` delegates/overrides per the is-diode mask and that `base` is read-only for the solve duration; make T010 pass.

**Checkpoint**: US3 done — the sibling seam for `implicit-integration` is proven with a hand-written base.

---

## Phase 6: User Story 4 — Stateless per solve with caller-owned warm start (P1)

**Goal**: statelessness + node-voltage-array initial guess (FR-008/009).

- [ ] T012 [US4] [tier:balanced] Write FAILING doctest in `tests/core/newton-solver-test.cpp`: two `solve()` calls with identical `(nl, base, initialNodeVoltages)` → bit-identical `NewtonStatus` and node voltages (S8); a cold zero guess vs a near-solution warm start → both converge to the same operating point, the warm start in no more iterations (S9); the guess is the full node-voltage array (branch currents are not part of it). Fails until T013.
- [ ] T013 [US4] [tier:balanced] Ensure per-solve scratch (`diodeCompanion_`, `prevBiasAK_`) is reset at the top of `solve()` and no field persists solve→solve except immutable config + plan-time topology; make T012 pass.

**Checkpoint**: US4 done — the primitive is a pure function of its inputs.

---

## Phase 7: User Story 5 — Two-phase, RT-safe hot-path solve (P1)

**Goal**: prove plan-once / solve-many performs zero heap allocation and takes no locks (SC-003).

- [ ] T014 [US5] [tier:balanced] Write FAILING doctest in `tests/core/newton-solver-test.cpp`: wrap a `plan()`-once then N×`solve()` loop in `AllocationSentinel` → zero alloc/dealloc across the hot path (SC-003); assert no exception escapes `solve()`; assert `solve()` never triggers `MnaAssembler` re-plan / `addBranch`. Fails until T015.
- [ ] T015 [US5] [tier:balanced] Harden `solve()` to be allocation-free and throw-free (all scratch fixed-capacity `std::array`; the only throws are construction-time C1 and plan-time delegation); make T014 pass (S10).

**Checkpoint**: US5 done — the primitive is safe to call inside `process()` per Principle VI.

---

## Phase 8: User Story 6 — Report non-convergence and singular systems without fallback (P2)

**Goal**: surfaced failure, never fabrication (Principle V). File: `tests/core/newton-nofallback-test.cpp`.

- [ ] T016 [P] [US6] [tier:balanced] Write FAILING doctest `tests/core/newton-nofallback-test.cpp`: forced non-convergence (tight `voltageTol`, low `maxIterations`) → `converged == false`, `iterations == maxIterations`, residuals reported, node voltages left at the last iterate; a following identical `solve()` is unaffected (no state corruption, S8). A structurally singular linearized system → `MnaSystem::solve()` returns false → `solve()` returns `converged == false` **by value**, no throw on the hot path, and no gmin / source-step / substituted output anywhere (S7). Fails until T017.
- [ ] T017 [US6] [tier:balanced] Verify `solve()` surfaces both failure modes exactly as specified and that no fallback path exists; make T016 pass.

**Checkpoint**: US6 done — honest failure reporting, the no-fallback contract enforced.

---

## Phase 9: User Story 7 — Reproduce the existing lab solvers (equivalence oracle) (P2)

**Goal**: match the trusted lab reference on the TS808 clipper core (SC-002). File:
`tests/core/newton-equivalence-test.cpp`.

- [ ] T018 [P] [US7] [tier:balanced] Write doctest `tests/core/newton-equivalence-test.cpp`: on the TS808 diode-clipper core, run the primitive and the lab `OpAmpClipperSolver` / `TransientClipper` at matched inputs (reactive companion, if present, supplied to Newton via a hand-written base supply matching the lab's per-step backward-Euler companion, or compared at DC steady state) → converged node voltages agree to tolerance (SC-002). Confirm the op-amp/nullor path is handled by the driven `MnaAssembler`, not Newton.

**Checkpoint**: US7 done — the safety net that de-risks the eventual lab migration.

---

## Phase 10: Polish & cross-cutting

- [ ] T019 [P] [tier:balanced] Add invariant doctests to `tests/core/newton-invariants-test.cpp`: antiparallel odd (symmetric) transfer curve; `I(0) = 0`; monotonic transfer where expected; passivity (dissipated ≤ source energy) (SC-006, FR-022).
- [ ] T020 [P] [tier:fast] Run `scripts/check-portability.sh` and confirm the header is C++17, header-only, no platform headers, and within the ~300–500 line budget (SC-008); split the composed-supply helper into a second header only if over budget.
- [ ] T021 [P] [tier:fast] Confirm the full `newton*` doctest suite passes and cross-link `core/primitives/circuit/newton/README.md` to the spec + contracts; verify no `core/labs/` include leaks into the primitive header (lab includes belong only to the equivalence test).
- [ ] T022 [tier:fast] Final build + run `./build/tests/core/acfx_core_tests --test-suite=newton*` per quickstart.md; confirm all Success Criteria (SC-001..SC-008) are demonstrably met.

---

## Dependencies & completion order

- **Setup (T001–T002)** → **Foundational types + plan (T003–T004)** blocks everything.
- **US1 (T005–T007)** depends on Foundational; it implements `solve()` and is the MVP.
- **US2 (T008–T009)**, **US3 (T010–T011)**, **US4 (T012–T013)**, **US5 (T014–T015)** depend on US1's `solve()` loop; they extend/validate distinct facets (multi-diode, composition, statelessness, RT-safety) and are otherwise independent P1 stories.
- **US6 (T016–T017)** and **US7 (T018)** depend on a working `solve()` (US1); they are P2, run after the P1 stories.
- **Polish (T019–T022)** last.

## Parallel opportunities

- T002 ∥ T001 (different files).
- Once US1's `solve()` lands, the P1 validation stories touch mostly `newton-solver-test.cpp` (coordinate to avoid edit races) while US6 (`newton-nofallback-test.cpp`), US7 (`newton-equivalence-test.cpp`), and Polish invariants (`newton-invariants-test.cpp`) are independent files and can be drafted in parallel (T016 ∥ T018 ∥ T019).
- Polish T020 ∥ T021.

## Implementation strategy

- **MVP = Setup + Foundational + US1** (T001–T007): a diode network solved to its exact
  operating point — a standalone, demonstrable increment.
- Then layer US2 (multi-diode charter), US3 (composition), US4 (statelessness), US5
  (RT-safety proof) to complete the P1 surface, then US6 (no-fallback) and US7 (lab oracle)
  for P2 robustness + the equivalence safety net.
- Commit per task; push promptly (Commandment 1).
