---
description: "Task list — Modified Nodal Analysis (MNA) primitive"
---

> ‼ **acfx COMMANDMENTS — non-negotiable** ‼
> **1. COMMIT AND PUSH EARLY AND OFTEN** — version control is a distributed, journaled
> filesystem that safeguards your work, **NOT a sacred rite reserved for the blessed.**
> Small atomic commits, pushed promptly; never hoard unpushed work.
> **2. NO GIT HOOKS, EVER** — this repo uses zero git hooks; none exist, none get added.
> **3. DESCRIPTIVE NAMES, NEVER NUMERIC PREFIXES** — names carry information; fake sequence
> numbers (`001-`) imply false order and false precision (datestamps excepted).
> (acfx Constitution, Principles I–III — `.specify/memory/constitution.md`.)

# Tasks: Modified Nodal Analysis (MNA) primitive

**Input**: Design documents in `specs/modified-nodal-analysis/`
(plan.md, spec.md, research.md, data-model.md, contracts/, quickstart.md).

**Tests**: TDD is REQUESTED (acfx Principle VIII — test the core host-side). Every
implementation task is preceded by a failing doctest task in the same story.

**Organization**: grouped by the spec's user stories. `MnaSystem` (Layer 1) is
Foundational — every story depends on it. `[tier:]` hints drive model-sized dispatch
in `/stack-control:execute`.

**Out of scope (do NOT implement here)**: lab migration onto `MnaSystem` (backlog
TASK-14); complex/AC scalar; controlled sources; canonical `DiodeSpec` relocation.
These are captured follow-on / open questions, not this feature.

## Format: `[ID] [P?] [Story] [tier:] Description + file path`

- **[P]**: parallelizable (different file, no incomplete-task dependency).
- **[Story]**: US1..US6 from spec.md (Setup/Foundational/Polish carry no story label).

---

## Phase 1: Setup (shared infrastructure)

- [x] T001 [tier:fast] Inhabit the primitive directory `core/primitives/circuit/mna/` (create it in this commit — "inhabit before creating"; no empty pre-creation) and add a short `core/primitives/circuit/mna/README.md` stating: production primitive, namespace `acfx::mna`, two-layer (`MnaSystem` engine + `MnaAssembler`), consumes the frozen `circuit/` vocabulary, RT-safe two-phase contract, lab migration is out-of-scope follow-on (TASK-14).
- [x] T002 [P] [tier:fast] Register the four host test sources (`mna-system-test.cpp`, `mna-assembler-test.cpp`, `mna-invariants-test.cpp`, `mna-equivalence-test.cpp`) in `tests/CMakeLists.txt`, mirroring the existing `circuit-solver-test` / `opamp-stage-*` registrations. Files may be empty stubs at this point.

---

## Phase 2: Foundational — `MnaSystem` (Layer 1 abstract engine) — BLOCKS all stories

**Goal**: the reusable bordered linear engine (stamp API + partial-pivoting solve +
relative singular threshold + total noexcept accessors), the shared linear-algebra
core TASK-14 targets. Independent test: stamp a small system by hand and solve it;
assert singular→false and zero heap. Contract: `contracts/mna-system.md`.

- [x] T003 [tier:balanced] Write FAILING doctest `tests/core/mna-system-test.cpp`: (a) hand-stamped 2×2 conductance system solves to the analytic voltage (D2 four-corner, ground-aware); (b) a bordered system with a zero-diagonal constraint row solves ONLY because of partial pivoting (D5); (c) `solve()` returns `false` on a singular system and leaves no NaN readable (D1/D7); (d) a poorly-scaled but well-posed system (µS conductances beside unit branch value) still solves (relative threshold, D1); (e) after `reset()`, re-stamping identical values yields a bit-identical solution (statelessness); (f) `AllocationSentinel` reports zero alloc/dealloc across a reset→stamp→solve loop (SC-003). Tests fail until T004.
- [x] T004 [tier:powerful] Implement `core/primitives/circuit/mna/mna-system.h` — `template<int MaxNodes,int MaxBranches> class MnaSystem` in `acfx::mna`: `std::array` storage `Dim=MaxNodes+MaxBranches`; `reset()`; `stampConductance` (D2); `stampRhsCurrent`; `addBranch()` (throws on overflow — the ONLY throwing method); `stampBranchIncidence`/`stampBranchValue`/`stampBranchResistance` (D3); `bool solve() noexcept` = Gaussian elimination with partial pivoting + relative singular threshold `|piv| < 1e-12·matScale` (D1/D5); `nodeVoltage`/`branchCurrent` total & `noexcept` (D7). Header-only, C++17, no platform headers, ≤ ~300 lines. Make T003 pass. Contract: `contracts/mna-system.md`.

**Checkpoint**: `MnaSystem` solves, pivots, rejects singular, allocates zero — the engine every story stamps onto.

---

## Phase 3: User Story 1 — Solve a linear resistive/source network exactly (P1)

**Goal**: `MnaAssembler` plan/refresh for resistors, current sources, and ideal
voltage sources (grounded AND floating). Independent test: divider mid-node exact;
floating source between two non-ground nodes exact. Contract: `contracts/mna-assembler.md`.

- [x] T005 [US1] [tier:balanced] Write FAILING doctest in `tests/core/mna-assembler-test.cpp`: resistive divider → `V(mid)==Vin·R2/(R1+R2)` to FP precision; current source into R-to-ground → `V==I·R`; **floating** ideal voltage source between two non-ground nodes `a,b` → `V(a)−V(b)` exact and its branch current exact (SC-005). Tests fail until T006/T007.
- [x] T006 [US1] [tier:powerful] Implement the `MnaAssembler` skeleton + linear-element mapping in `core/primitives/circuit/mna/mna-assembler.h` — `template<int MaxNodes,int MaxComponents,int MaxBranches> class MnaAssembler` in `acfx::mna`: `plan(nl, sys)` walks the netlist, calls `sys.addBranch()` once per `VoltageSource` (grounded + floating), records `branchOf_[componentIndex]`, validates (out-of-range node, degenerate R≤0 → descriptive throw); `refresh(nl, comps, sys)` `noexcept` resets and re-stamps `Resistor→stampConductance(1/R)`, `CurrentSource→stampRhsCurrent(±I)`, `VoltageSource→incidence+value`. Header-only, C++17, ≤ ~300 lines. Contract: `contracts/mna-assembler.md`.
- [x] T007 [US1] [tier:balanced] Wire `refresh`+`sys.solve()`+`nodeVoltage`/`branchCurrent` read-back so T005 passes; confirm floating-source branch current sign convention matches the contract table.

**Checkpoint**: US1 is an independently demonstrable MVP — exact linear solves incl. floating sources (a capability the labs refuse).

---

## Phase 4: User Story 2 — Solve an op-amp (nullor) circuit exactly (P1)

**Goal**: nullor bordering in the assembler. Independent test: inverting and
non-inverting ideal-amp gains exact.

- [x] T008 [US2] [tier:balanced] Extend `tests/core/mna-assembler-test.cpp` with FAILING nullor cases: ideal inverting amp → `Vout==−Vin·Rf/Rin`; non-inverting → `Vout==Vin·(1+Rf/Rg)`; assert the nullator constraint `V(in+)−V(in−)=0` holds in the solution. Fails until T009.
- [x] T009 [US2] [tier:powerful] Add `OpAmp` mapping to `MnaAssembler`: `plan` allocates one branch per op-amp; `refresh` stamps the nullor border — norator current into the `out` KCL and the nullator constraint via incidence (D3), reproducing `NullorSolver`'s stamp. Make T008 pass.

**Checkpoint**: US2 done — nullor circuits solve exactly; the "Modified" augmentation is complete for ideal sources + op-amps.

---

## Phase 5: User Story 3 — Compose with caller-supplied companions (P1)

**Goal**: the `CompanionSupply` seam — MNA stamps caller-supplied `Companion{Geq,Ieq}`
for reactive/nonlinear elements, holding no history (D6). Independent test:
fed-companion RC step matches the backward-Euler recurrence.

- [x] T010 [US3] [tier:balanced] Define the `CompanionSupply` seam (non-owning, `at(componentIndex)->Companion`, `noexcept`) in `mna-assembler.h` per data-model.md, and write a hand-written test harness in `tests/core/mna-assembler-test.cpp`.
- [x] T011 [US3] [tier:balanced] Write FAILING doctest: a caller supplies `{Geq=C/dt, Ieq=Geq·vPrev}` for a capacitor each step; assembler stamps `Geq→conductance`, `Ieq→rhs`; sampled response matches the backward-Euler recurrence step-for-step; a diode reduced to a Norton companion by the harness solves to the linearized operating point; two identical `(nl,comps)` refreshes give bit-identical solutions (US3-AS2, FR-011). Fails until T012.
- [x] T012 [US3] [tier:powerful] Implement companion stamping in `MnaAssembler::refresh`: for each `Capacitor`/`Inductor`/`Diode`, fetch `comps.at(i)` and stamp `Geq`/`Ieq` with the sign convention matching `models/companion.h`; MNA computes no companion and stores no history. Make T010/T011 pass.

**Checkpoint**: US3 done — MNA is the stateless linear heart the sibling primitives call; all P1 solving behaviors exist.

---

## Phase 6: User Story 4 — Two-phase assembly with an RT-safe hot path (P1)

**Goal**: prove the plan-once / refresh-and-solve-many contract at the assembler
level and that plan-time faults throw before any solve (SC-006).

- [x] T013 [US4] [tier:balanced] Write FAILING doctest in `tests/core/mna-assembler-test.cpp`: wrap a `plan()`-once then N×(`refresh`+`solve`) loop in `AllocationSentinel` → zero alloc/dealloc across the per-solve phase (SC-003); assert `refresh` never triggers `addBranch` (branch count stable); assert no exception escapes the per-solve phase. Fails until T014.
- [x] T014 [US4] [tier:balanced] Harden the two-phase split in `MnaAssembler`: guard `refresh` against being called before `plan` (`planned_` precondition); ensure `plan` throws descriptively on branch overflow / out-of-range node / degenerate value BEFORE any solve, and `refresh` is `noexcept` + alloc-free (D4/D7). Make T013 pass; add a plan-time-throw case (over-capacity branches, out-of-range node) asserting the descriptive throw (SC-006).

**Checkpoint**: US4 done — the primitive is safe to call inside `process()` per Principle VI.

---

## Phase 7: User Story 5 — Report ill-posed systems; physical invariants (P2)

**Goal**: singular topologies return not-solved without throw or NaN; assembled
circuits honor physical invariants (FR-022). File: `tests/core/mna-invariants-test.cpp`.

- [x] T015 [P] [US5] [tier:balanced] Write FAILING doctest `tests/core/mna-invariants-test.cpp` (ill-posed): floating subgraph and redundant-nullor topologies → `solve()` returns `false`, no throw on the solve path, no NaN in `nodeVoltage` (SC-004); a poorly-scaled well-posed system still solves. Fails until T016 confirms behavior end-to-end through the assembler.
- [x] T016 [US5] [tier:balanced] Verify/adjust assembler+system so ill-posed assembled circuits surface as `solve()==false` (never a silent gmin, never a throw on the hot path — Principle V); make T015 pass.
- [x] T017 [P] [US5] [tier:balanced] Add invariant doctests to `mna-invariants-test.cpp`: passivity (dissipated energy ≤ source energy on a passive network); reciprocity/symmetry of the conductance block; monotonicity where expected (FR-022).

**Checkpoint**: US5 done — robustness + physical-invariant validation beyond exact closed-forms.

---

## Phase 8: User Story 6 — Reproduce the existing lab solvers (equivalence oracle) (P2)

**Goal**: prove MNA is a faithful superset of `LinearSolver` and `NullorSolver`
ahead of any migration. File: `tests/core/mna-equivalence-test.cpp`.

- [x] T018 [P] [US6] [tier:balanced] Write doctest `tests/core/mna-equivalence-test.cpp`: for each topology within `LinearSolver` scope, run both `LinearSolver` and MNA → node voltages agree ≤ 1e-12 (SC-002).
- [x] T019 [US6] [tier:balanced] Extend `mna-equivalence-test.cpp`: for each topology within `NullorSolver` scope, run both `NullorSolver` and MNA → node voltages AND branch currents agree ≤ 1e-12 (SC-002).

**Checkpoint**: US6 done — the safety net that makes the future lab migration low-risk.

---

## Phase 9: Polish & cross-cutting

- [x] T020 [P] [tier:fast] Run `scripts/check-portability.sh` and confirm both headers are C++17, header-only, no platform headers, and within the ~300–500 line budget (SC-007); split a header if over budget.
- [x] T021 [P] [tier:fast] Confirm the full `mna*` doctest suite passes and cross-link the `core/primitives/circuit/mna/README.md` to the spec + contracts; verify no `core/labs/` include leaks into the primitive headers.
- [x] T022 [tier:fast] Final build + run `./build/tests/core/acfx_core_tests --test-suite=mna*` per quickstart.md; confirm all Success Criteria (SC-001..SC-007) are demonstrably met.

---

## Dependencies & completion order

- **Setup (T001–T002)** → **Foundational `MnaSystem` (T003–T004)** blocks everything.
- **US1 (T005–T007)** depends on Foundational; it is the MVP.
- **US2 (T008–T009)**, **US3 (T010–T012)** depend on US1's assembler skeleton (T006).
- **US4 (T013–T014)** depends on US1–US3 (the elements it stress-tests exist).
- **US5 (T015–T017)** and **US6 (T018–T019)** depend on US1–US2 (need solvable circuits + nullor path); they are P2, run after the P1 stories.
- **Polish (T020–T022)** last.

## Parallel opportunities

- T002 ∥ T001 (different files).
- Within US5: T015 ∥ T017 (distinct test cases, same file — coordinate to avoid edit races) ; across stories, US5 (T015/T017) and US6 (T018) test files are independent and can be drafted in parallel once US1–US2 land.
- Polish T020 ∥ T021.

## Implementation strategy

- **MVP = Setup + Foundational + US1** (T001–T007): an exact linear/source solver
  incl. floating sources — a standalone, demonstrable increment.
- Then layer US2 (nullor), US3 (companions) to complete the P1 solving surface, then
  US4 (RT-safety proof). US5/US6 (P2) add robustness + the lab-equivalence safety net.
- Commit per task; push promptly (Commandment 1).
