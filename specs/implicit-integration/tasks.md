---
description: "Task list — Implicit-integration primitive"
---

> ‼ **acfx COMMANDMENTS — non-negotiable** ‼
> **1. COMMIT AND PUSH EARLY AND OFTEN** — version control is a distributed, journaled
> filesystem that safeguards your work, **NOT a sacred rite reserved for the blessed.**
> Small atomic commits, pushed promptly; never hoard unpushed work.
> **2. NO GIT HOOKS, EVER** — this repo uses zero git hooks; none exist, none get added.
> **3. DESCRIPTIVE NAMES, NEVER NUMERIC PREFIXES** — names carry information; fake sequence
> numbers (`001-`) imply false order and false precision (datestamps excepted).
> (acfx Constitution, Principles I–III — `.specify/memory/constitution.md`.)

# Tasks: Implicit-integration primitive

**Input**: Design documents in `specs/implicit-integration/`
(plan.md, spec.md, research.md, data-model.md, contracts/, quickstart.md).

**Tests**: TDD is REQUESTED (acfx Principle VIII — test the core host-side; the recorded
`circuit-model-validation-approach`). Every implementation task is preceded by a failing
doctest task in the same story.

**Organization**: grouped by the spec's user stories. The `ReactiveIntegrator` types + rule
policies + two-phase scaffolding are Foundational — every story depends on them. `[tier:]`
hints drive model-sized dispatch in `/stack-control:execute`
(fast→haiku, balanced→sonnet, powerful→opus).

**Out of scope (do NOT implement here)**: lab migration onto MNA+Newton+implicit-integration
(backlog TASK-14); a gmin / source-stepping / adaptive-order / any silent convergence or rule
fallback; variable-`dt` re-plan path (spec Open Question 2); DC-operating-point
auto-initialization (Open Question 4); complex/AC scalar. These are captured follow-on / open
questions, not this feature. The primitive DRIVES the shipped MNA core and COMPOSES the shipped
Newton core; it modifies neither, and adds no new component types.

## Format: `[ID] [P?] [Story] [tier:] Description + file path`

- **[P]**: parallelizable (different file, no incomplete-task dependency).
- **[Story]**: US1..US8 from spec.md (Setup/Foundational/Polish carry no story label).

---

## Phase 1: Setup (shared infrastructure)

- [x] T001 [tier:fast] Inhabit the primitive directory `core/primitives/circuit/integration/` (create it in this commit — "inhabit before creating"; no empty pre-creation) and add a short `core/primitives/circuit/integration/README.md` stating: production primitive, namespace `acfx::integration`, single `ReactiveIntegrator<Rule, …>` type owning reactive companions + history + time-stepping, selectable rule (backward-Euler default + trapezoidal), composes the shipped MNA (linear) / Newton (nonlinear) cores, stateful two-phase RT-safe contract, no-fallback / no silent rule switch, lab migration is out-of-scope follow-on (TASK-14).
- [x] T002 [P] [tier:fast] Register the six host test sources (`integration-integrator-test.cpp`, `integration-closed-form-test.cpp`, `integration-convergence-order-test.cpp`, `integration-invariants-test.cpp`, `integration-composition-test.cpp`, `integration-equivalence-test.cpp`) in `tests/CMakeLists.txt`, mirroring the existing `mna-*` / `newton-*` registrations. Files may be empty stubs at this point.

---

## Phase 2: Foundational — rule policies + `ReactiveIntegrator` types + two-phase scaffolding — BLOCKS all stories

**Goal**: the rule policies (`BackwardEuler`, `Trapezoidal`), the type surface (`StepResult`,
`ReactiveCompanionSupply`, `ReactiveIntegrator`), construction validation, and the
throw-permitted `plan()` (delegate to `MnaAssembler::plan` + scan reactive elements into the
is-reactive mask / reactive-index table + record `hasNonlinear_`). Contract:
`contracts/reactive-integrator.md`.

- [x] T003 [tier:balanced] Write FAILING doctest `tests/core/integration-integrator-test.cpp`: (a) constructing with `dt <= 0` (or invalid forwarded Newton config) throws `std::invalid_argument` (C1); (b) after `plan()` on a netlist with reactive elements at known component indices, the is-reactive mask + reactive-index table match the scan and `hasNonlinear_` matches the presence of a diode (`planned()` true); (c) calling `step()` before `plan()` returns `StepResult{converged=false, iterations=0, voltageResidual=0}` by value (deterministic, throw-free, not UB — contract S8). Tests fail until T004/T005.
- [x] T004 [tier:powerful] Implement the rule policies in `core/primitives/circuit/integration/reactive-integrator.h` (`acfx::integration`): `struct BackwardEuler` and `struct Trapezoidal`, each with static `noexcept` `capacitorCompanion(C, dt, vPrev, iPrev)` and `inductorCompanion(L, dt, vPrev, iPrev)` returning the `Companion{Geq, Ieq}` of research R1 (MNA convention `i = Geq·v − Ieq`). `BackwardEuler` MUST reuse the shipped `Capacitor::companion(dt, vPrev)` / `Inductor::companion(dt, iPrev)` so the `C/dt` / `dt/L` constants are single-sourced (R9); `Trapezoidal` computes `{2C/dt, Geq·vPrev + iPrev}` / `{dt/(2L), −(iPrev + Geq·vPrev)}`. Contract: RP1–RP2.
- [x] T005 [tier:powerful] Implement the `ReactiveIntegrator<Rule, MaxNodes, MaxComponents, MaxBranches>` scaffolding in the same header: `struct StepResult{bool converged;int iterations;double voltageResidual;}`; internal `ReactiveCompanionSupply` with `Companion at(int) const noexcept` = `isReactive_[i] ? reactiveCompanion_[i] : Companion{0,0}`; ctor validating C1 (throw); `plan(nl, assembler, sys)` delegating to `MnaAssembler::plan` then scanning the netlist once to fill `isReactive_`, `reactiveComponentIndex_`, `reactiveCount_`, `hasNonlinear_`, and zeroing cross-sample state; `reset()` (RS1); `planned()` accessor + a `step()`-before-`plan()` guard returning `StepResult{false,0,0}` by value (S8). Header-only, C++17, no platform headers, ≤ ~300 lines (split the supply helper into a second header only if over budget). Make T003 pass. Contract: C1–C2, P1–P3, RS1.

**Checkpoint**: the rule policies + type surface + plan phase exist; reactive topology + the linear/nonlinear branch are fixed once, off the hot path.

---

## Phase 3: User Story 1 — Discretize a single reactive element to its exact discrete response (P1) — MVP

**Goal**: the `step()` companion computation + composed linear solve + history read on one
reactive element. Independent test: RC / RL step under backward-Euler matches the exact discrete
response. Contract: `contracts/reactive-integrator.md` (S1–S4, S9).

- [x] T006 [US1] [tier:balanced] Write FAILING doctest `tests/core/integration-closed-form-test.cpp`: an RC low-pass at zero initial state driven by a voltage step, backward-Euler → at each sample the capacitor node voltage matches the exact discrete response `v[n] = v[n−1] + (dt/(RC+dt))·(Vin − v[n−1])` to tolerance (start `1e-12`), monotone to the DC steady state; the RL dual for the inductor current. Fails until T007.
- [x] T007 [US1] [tier:powerful] Implement `ReactiveIntegrator::step(...)` for the **linear** path (`hasNonlinear_ == false`): compute each reactive element's `Companion` once via `Rule` from `dt_` + history `{vPrev_, iPrev_}` (S1); expose them through `ReactiveCompanionSupply` and drive `MnaAssembler::refresh(nl, supply, sys)` + `MnaSystem::solve()` (S2); read converged node voltages, `v^n = V(a) − V(b)`, reconstruct `i^n = Geq·v^n − Ieq` (S3, R4); advance history once (`vPrev_ := v^n`, `iPrev_ := i^n`, `warmStart_ := ` node voltages) (S4); return `StepResult`. Make T006 pass. Contract S1–S4, S9.
- [x] T008 [US1] [tier:balanced] Extend `integration-closed-form-test.cpp`: a series/parallel LC network under backward-Euler → matches the analytic discrete solution; a **zero-reactive-element** netlist → `step()` is a clean passthrough (one composed solve, no reactive stamping, no-op history advance), not an error (S9).

**Checkpoint**: US1 is an independently demonstrable MVP — a reactive element advanced through time against an exact oracle.

---

## Phase 4: User Story 2 — Select the integration rule and observe the accuracy gain (P1)

**Goal**: the trapezoidal policy is genuinely 2nd-order (not aliased to BE). Independent test:
convergence-order regression ≈1 (BE) / ≈2 (trap).

- [x] T009 [US2] [tier:balanced] Write FAILING doctest `tests/core/integration-convergence-order-test.cpp`: integrate an RC (and an LC) network under each rule across a sequence of shrinking timesteps; regress global error vs `dt` (log–log) and assert slope ≈1 for `BackwardEuler` (band `[0.9,1.2]`) and ≈2 for `Trapezoidal` (band `[1.8,2.2]`) (SC-002); also assert the trapezoidal companion values match research R1 for a known state. Fails until T010.
- [x] T010 [US2] [tier:balanced] Verify the `Trapezoidal` policy against T009 (companion formulas + the order test); confirm the rule is a template parameter with no per-sample branch on rule in `step()` (R2). Make T009 pass.

**Checkpoint**: US2 done — the selectable higher-order rule (the capability gain over the labs) is demonstrable.

---

## Phase 5: User Story 3 — Provide the base companion supply the siblings consume (P1)

**Goal**: prove `ReactiveCompanionSupply` is consumable as MNA's `refresh` input (linear) and as
Newton's `base` (nonlinear), `noexcept`, fixed across a solve (FR-006/007).

- [x] T011 [US3] [tier:balanced] Write FAILING doctest `tests/core/integration-composition-test.cpp`: (a) a linear reactive netlist → the supply fed to `MnaAssembler::refresh` stamps the reactive elements and `MnaSystem::solve()` gives the expected voltages; (b) a reactive+diode netlist → the supply used as `NewtonSolver::solve`'s `base` has its reactive companions stamped unchanged across Newton's iterations while diode companions update; (c) `supply.at()` satisfies Newton's `noexcept` requirement (compile-time + runtime). Fails until T012.
- [x] T012 [US3] [tier:balanced] Confirm `ReactiveCompanionSupply::at` returns the fixed reactive companion for reactive indices and is `noexcept`/O(1), and that the reactive companions are computed once per step and held fixed for the whole solve (R5); make T011 pass.

**Checkpoint**: US3 done — the sibling seam (this primitive IS Newton's `base`) is proven end-to-end.

---

## Phase 6: User Story 4 — Own reactive history and advance it once per converged timestep (P1)

**Goal**: the rule-agnostic history-advance contract, once per timestep (FR-008/009).

- [x] T013 [US4] [tier:balanced] Write FAILING doctest in `tests/core/integration-integrator-test.cpp`: after each converged step, assert stored `{vPrev, iPrev}` equals `{v^n, Geq·v^n − Ieq}` using that step's stamped companion, for every reactive element, under BOTH rules (S3/S4, R3); on a reactive+diode transient assert history advances exactly once per step (not per Newton iteration). Fails until T014.
- [x] T014 [US4] [tier:balanced] Confirm `step()` advances history via `iPrev := Geq·v^n − Ieq` reusing the step's stamped companion (not a re-derived per-rule formula), exactly once after convergence; make T013 pass.

**Checkpoint**: US4 done — the history contract holds under both rules and consolidates the labs' 4× hand-rolled advance.

---

## Phase 7: User Story 5 — Own time-stepping: drive a full transient by composing Newton/MNA (P1)

**Goal**: the composed per-sample loop (Newton for nonlinear, MNA for linear) with
integrator-owned warm start (FR-010).

- [x] T015 [US5] [tier:powerful] Implement the **nonlinear** branch of `step()` (`hasNonlinear_ == true`): drive `newton.solve(nl, ReactiveCompanionSupply, warmStart_, assembler, sys)` with the reactive supply as Newton's fixed `base` and the integrator's `warmStart_` as the guess; propagate `NewtonStatus` into `StepResult`; on convergence advance history + `warmStart_` (S2–S4, R6). Make the nonlinear cases of T011/T013 pass. Header ≤ budget.
- [x] T016 [US5] [tier:balanced] Extend `tests/core/integration-composition-test.cpp`: integrate a reactive diode-clipper transient sample-by-sample → a stable output waveform; assert the warm start + reactive history carry forward across samples (owned by the integrator) and companions are computed once per sample (S7, R6).

**Checkpoint**: US5 done — a full reactive+nonlinear transient runs end-to-end through the composed trio.

---

## Phase 8: User Story 6 — Two-phase: plan once, RT-safe hot-path step (P1)

**Goal**: prove plan-once / step-many performs zero heap allocation and takes no locks (SC-004).

- [x] T017 [US6] [tier:balanced] Write FAILING doctest in `tests/core/integration-integrator-test.cpp`: wrap a `plan()`-once then N×`step()` loop (linear AND reactive+diode) in `AllocationSentinel` → zero alloc/dealloc across the hot path (SC-004); assert no exception escapes `step()`; assert `step()` never triggers `MnaAssembler` re-plan / `addBranch`, and that reactive companions are computed once per step, not per Newton iteration (S1/S8). Fails until T018.
- [x] T018 [US6] [tier:balanced] Harden `step()` to be allocation-free and throw-free (all scratch fixed-capacity `std::array`; the only throws are construction-time C1 and plan-time delegation); make T017 pass (S8).

**Checkpoint**: US6 done — the primitive is safe to call inside `process()` per Principle VI, and TASK-13 is dissolved by construction.

---

## Phase 9: User Story 7 — No fallback: surface non-convergence and never silently switch the rule (P2)

**Goal**: surfaced failure + rule fidelity, never fabrication or silent switch (Principle V).

- [x] T019 [P] [US7] [tier:balanced] Write FAILING doctest in `tests/core/integration-integrator-test.cpp`: force a non-converged composed nonlinear step (tight `voltageTol`, low `maxIterations`) → `StepResult.converged == false` surfaced by value; reactive history is **not** advanced from the untrustworthy iterate (S5); a following clean step is unaffected (S7). Assert the selected rule is used verbatim — trapezoidal ringing on a stiff node is produced, not silently switched to backward-Euler (S6); confirm no gmin / rule-switch / substituted-output path exists anywhere in the primitive. Fails until T020.
- [x] T020 [US7] [tier:balanced] Verify `step()` surfaces the failure by value, leaves history un-advanced on non-convergence, and contains no fallback / rule-switch path; make T019 pass.

**Checkpoint**: US7 done — honest failure reporting and rule fidelity, the no-fallback contract enforced.

---

## Phase 10: User Story 8 — Reproduce the lab backward-Euler solvers (equivalence oracle) (P2)

**Goal**: match the trusted lab reference on shared topologies with rule = backward-Euler
(SC-003). File: `tests/core/integration-equivalence-test.cpp`.

- [x] T021 [P] [US8] [tier:balanced] Write doctest `tests/core/integration-equivalence-test.cpp`: a reactive topology (and a reactive+diode topology) integrated across a transient by both the primitive (rule = `BackwardEuler`) and a lab solver (`LinearSolver` / `TransientClipper` / `OpAmpClipperSolver`) → node voltages agree to tolerance at matched samples (SC-003). Confirm the lab includes are confined to this test (no `core/labs/` include leaks into the primitive header).

**Checkpoint**: US8 done — the safety net that de-risks the eventual lab migration (TASK-14).

---

## Phase 11: Polish & cross-cutting

- [x] T022 [P] [tier:balanced] Add invariant doctests to `tests/core/integration-invariants-test.cpp`: DC steady state (driven to settle, capacitor → open `i → 0`, inductor → short `v → 0`) under both rules; passivity / no-energy-gain for a passive RLC network across a transient (SC-006, FR-023).
- [x] T023 [P] [tier:fast] Run `scripts/check-portability.sh` and confirm the header is C++17, header-only, no platform headers, and within the ~300–500 line budget (SC-008); split the supply/rule helpers into a second header only if over budget.
- [x] T024 [P] [tier:fast] Confirm the full `integration*` doctest suite passes and cross-link `core/primitives/circuit/integration/README.md` to the spec + contracts; verify no `core/labs/` include leaks into the primitive header (lab includes belong only to the equivalence test).
- [x] T025 [tier:fast] Final build + run `./build/tests/core/acfx_core_tests --test-suite=integration*` per quickstart.md; confirm all Success Criteria (SC-001..SC-008) are demonstrably met.

---

## Dependencies & completion order

- **Setup (T001–T002)** → **Foundational: rule policies + types + plan (T003–T005)** blocks everything.
- **US1 (T006–T008)** depends on Foundational; it implements the linear `step()` path and is the MVP.
- **US2 (T009–T010)** depends on US1 (the step + companion path); it adds/validates the trapezoidal rule.
- **US3 (T011–T012)** depends on US1 (the supply exists); it proves consumption by MNA and (with US5's nonlinear branch) Newton.
- **US4 (T013–T014)** depends on US1 (history advance exists); validates the advance contract under both rules.
- **US5 (T015–T016)** depends on US1 + US3; it implements the nonlinear (Newton-composed) branch of `step()` and the full transient.
- **US6 (T017–T018)** depends on US1 + US5 (both step paths exist); proves zero-heap RT-safety.
- **US7 (T019–T020)** depends on US5 (nonlinear step); P2 no-fallback / rule fidelity.
- **US8 (T021)** depends on US1 + US5; P2 lab oracle.
- **Polish (T022–T025)** last.

## Parallel opportunities

- T002 ∥ T001 (different files).
- The two rule policies (T004) and the type scaffolding (T005) are in one header — coordinate to avoid edit races (same file), so they are sequential within Foundational.
- Once US1's linear `step()` lands, US2 (`integration-convergence-order-test.cpp`), US8 (`integration-equivalence-test.cpp`), and Polish invariants (`integration-invariants-test.cpp`) are independent files and can be drafted in parallel (T009 ∥ T021 ∥ T022). US3/US4/US6/US7 tests mostly touch `integration-integrator-test.cpp` / `integration-composition-test.cpp` — coordinate to avoid edit races.
- Polish T023 ∥ T024.

## Implementation strategy

- **MVP = Setup + Foundational + US1** (T001–T008): a reactive element advanced through time,
  matching its exact discrete response under backward-Euler — a standalone, demonstrable increment.
- Then US2 (the trapezoidal accuracy gain — the capability that justifies the primitive), US3
  (the sibling supply seam), US4 (the history contract), US5 (the composed transient — Newton/MNA),
  US6 (the RT-safety proof) to complete the P1 surface; then US7 (no-fallback / rule fidelity) and
  US8 (lab oracle) for P2 robustness + the equivalence safety net.
- Commit per task; push promptly (Commandment 1).
