> ‼ **acfx COMMANDMENTS — non-negotiable** ‼
> **1. COMMIT AND PUSH EARLY AND OFTEN** — small atomic commits, pushed promptly.
> **2. NO GIT HOOKS, EVER** — zero hooks; gates are explicit build/test steps.
> **3. DESCRIPTIVE NAMES, NEVER NUMERIC PREFIXES** — names carry information.
> (acfx Constitution, Principles I–III.)

# Tasks: opamp-stages

**Feature dir**: `specs/opamp-stages/` | **Spec**: [spec.md](./spec.md) | **Plan**: [plan.md](./plan.md)

Tests are **included** — the spec mandates two validation tiers (Tier-1 primitive tests, Tier-2 solver/invariant) plus a lab harness (Constitution VIII, test the core host-side). Within each story, tests come before/with implementation.

## Format: `[ID] [P?] [Story] Description with file path`

- **[P]** = parallelizable (different files, no incomplete dependency).
- **[US1/US2/US3]** = user-story phase tasks only (Setup/Foundational/Polish carry no story label).

## Path Conventions

- Primitive (portable, C++17): `core/primitives/circuit/` (the `OpAmp` element) + `core/primitives/circuit/opamp-stage/` (the builders)
- Lab (host-only, C++20 ok): `core/labs/opamp-stages/`
- Tests (host-side): `tests/core/`

---

## Phase 1: Setup (Shared Infrastructure)

- [ ] T001 Create the primitive subfolder `core/primitives/circuit/opamp-stage/` and the lab tree `core/labs/opamp-stages/{solver,harness}/` (empty placeholders to be filled by later tasks).
- [ ] T002 [P] Register the two new host tests (`opamp-stage-builder-test.cpp`, `opamp-stage-solve-test.cpp`) in `tests/CMakeLists.txt`, and add the lab harness target `acfx_lab_opamp_stages_harness` (source `core/labs/opamp-stages/harness/opamp-stages-harness.cpp`, C++20) in the root `CMakeLists.txt`, mirroring the `component-abstractions` / `diode-clippers` lab-harness registration.
- [ ] T003 [P] Write the lab boundary note `core/labs/opamp-stages/README.md` (host-only, non-normative, **bounded nullor augmentation — not general MNA**, single-nonlinearity-location, does not modify `LinearSolver`, isolation guarantee) — mirrors `core/labs/component-abstractions/README.md` and `core/labs/diode-clippers/README.md`.

---

## Phase 2: Foundational (Blocking Prerequisites)

**Blocks US1, US2, US3** — the `OpAmp` vocabulary element and the config/value types are the shared vocabulary the builders emit and the solver/oracle read.

- [ ] T004 Add the **`OpAmp` vocabulary element** (the one sanctioned extension, design D2): implement `core/primitives/circuit/models/opamp.h` (`struct OpAmp { NodeId inPlus, inMinus, out; }` — ideal nullor, constraint not conductance, no `admittance()`/`companion()`, no non-ideality fields); add `OpAmp` to the `Component` `std::variant` in `components.h` and extend the classifiers (`isNonlinear`→false, `isReactive`→false, `isLinear`→true); extend `netlist.h` `terminalsOf(OpAmp)`→`{inPlus,inMinus}` and `contributesConductivePath(OpAmp)`→false (output excluded). Header-only, C++17, standard-library only, platform-independent. Contract: `contracts/opamp-element.md`.
- [ ] T005 Implement `core/primitives/circuit/opamp-stage/opamp-config.h`: the four BOM structs (`NonInvertingGainBom`, `InvertingGainBom`, `ActiveFirstOrderBom`, `OpAmpDiodeClipperBom{...,nUp,nDown}`), the per-topology `*Result` return structs `{netlist, inNode, outNode}`, the per-topology capacity aliases, and `detail::requirePositive` / population-validation helpers, per `data-model.md`. Header-only, C++17, standard-library only, ≤ ~300 lines. Contract: `contracts/opamp-stage-builder.md`.

**Checkpoint**: `opamp.h` (element) and `opamp-config.h` compile standalone; the vocabulary now carries `OpAmp`; US1/US2/US3 can begin.

---

## Phase 3: User Story 1 - Assemble a named op-amp stage, solver-neutrally (Priority: P1) 🎯 MVP

**Goal**: the four builders return a `prepare()`-valid `Netlist` of vocabulary components (including the `OpAmp`).
**Independent test**: build each stage at representative BOMs, `prepare()` passes, counts match the BOM, only vocabulary elements present (incl. `OpAmp`, nothing beyond it), port nodes correct — no solver.

### Tests for User Story 1

- [ ] T006 [P] [US1] `tests/core/opamp-stage-builder-test.cpp`: for each of the four builders assert `prepare()` succeeds at representative BOMs; component and node counts equal the topology's BOM; every held component is a vocabulary element (`Resistor`/`Capacitor`/`Diode`/`VoltageSource`/`OpAmp`) with **exactly one** `OpAmp` per stage; the reported `inNode`/`outNode` are correct; and a compile-level check that `opamp-stage.h`/`opamp-config.h`/`models/opamp.h` include nothing under `core/labs/` (isolation, FR-024). Tests fail until T007–T010 land.

### Implementation for User Story 1

- [ ] T007 [US1] Implement `nonInvertingGain(...)` in `core/primitives/circuit/opamp-stage/opamp-stage.h`: input `VoltageSource`→`inPlus`; `OpAmp{inPlus,inMinus,out}`; `Rf` `out`→`inMinus`; `Rg` `inMinus`→gnd; `prepare()`; return `{netlist, inNode=inPlus, outNode=out}` (gain `1+Rf/Rg`).
- [ ] T008 [US1] Implement `invertingGain(...)` in `opamp-stage.h`: input `VoltageSource`→`Rin`→`inMinus`; `inPlus`→gnd; `Rf` `out`→`inMinus`; `prepare()`; return `{outNode=out}` (gain `−Rf/Rin`).
- [ ] T009 [US1] Implement `activeFirstOrder(...)` in `opamp-stage.h` — the inverting first-order **low-pass** (OQ4): input→`Rin`→`inMinus`; `inPlus`→gnd; feedback `Rf` **and** `Cf` both `out`→`inMinus` (parallel); `prepare()`; return `{outNode=out}` (DC gain `−Rf/Rin`, corner `1/(2π·Rf·Cf)`).
- [ ] T010 [US1] Implement `opAmpDiodeClipper(...)` in `opamp-stage.h` — the TS808 core: input→`Rin`→`inMinus`; `inPlus`→gnd; feedback network `out`→`inMinus` = `Rf` ∥ `Cf` ∥ antiparallel diode string (`nUp` `Diode{out,inMinus}` + `nDown` `Diode{inMinus,out}`); `prepare()`; return `{outNode=out}`. Exactly one nonlinearity location (the feedback diode pair).
- [ ] T011 [US1] Add builder input validation in `opamp-stage.h`/`opamp-config.h`: any non-positive resistance/capacitance, non-positive diode parameter (`Is`/`n`/`Vt`), diode `count == 0`, a **floating op-amp input**, or a **missing feedback path** → descriptive `std::invalid_argument` naming the field (FR-010). No silent clamp, no fabricated topology.
- [ ] T012 [P] [US1] Update `core/primitives/README.md` to register the `OpAmp` element and the `circuit/opamp-stage/` subfolder with its four builders.

**Checkpoint**: US1 independently testable — MVP delivered (the `OpAmp` element + solver-neutral op-amp-stage builders the later named features compose).

---

## Phase 4: User Story 2 - Solve the assembled stage with a bounded nullor augmentation (Priority: P2)

**Goal**: the nullor-augmented solver realizes each ideal op-amp as one bordered row/column, solves the linear stages exactly, discretizes the active stage's reactive element, and couples the clipper's diode Newton around the bordered solve — reporting convergence.
**Independent test**: solve the linear stages to their analytic gains (~1e-9) and the active stage to its analytic first-order response (~1e-9); cross-check the clipper DC limit against an independent bisection oracle (~1e-6); drive deliberate non-convergence and confirm it is reported.
**Depends on**: Foundational (T004 element, T005 config). Gain/first-order/DC checks additionally consume the US1 builders.

### Implementation & Tests for User Story 2

- [ ] T013 [US2] Implement the **nullor augmentation** in `core/labs/opamp-stages/solver/opamp-stage-solver.h`: assemble the reduced nodal system as the lab already does, then **border** it — per `OpAmp`, add one unknown (norator `out`-node branch current) and one constraint row (`V(inPlus)−V(inMinus)=0`) → the `[[G,B],[C,0]]` system (R2) — and solve with the lab's fixed-size Gaussian elimination with partial pivoting. Ctor validates params; `dt ≤ 0` and a **singular augmented system** → descriptive throw (FR-016, the authoritative well-posedness gate); heap-free `std::array`-backed, sized at instantiation (`reducedNodes + numOpAmps`). Does **not** modify `component-abstractions` `LinearSolver`. Contract: `contracts/opamp-stage-solver.md`.
- [ ] T014 [US2] Extend the solver for the **active first-order stage**: discretize the feedback `Cf` as a backward-Euler companion via the frozen `capacitor.h::companion(dt,·)`, solving the bordered system per timestep and advancing reactive history **exactly once** per step (FR-012).
- [ ] T015 [US2] Extend the solver for the **op-amp feedback-diode clipper**: reuse `diode-clippers`' separated timestep/Newton structure around the bordered solve — companions once per step; inner Newton holds them fixed, companion-linearizes the diode string into Norton pairs appended to a stack sized `+2·MaxDiodes`, solves the **bordered** linear system (nullor rows included), pnjlim-damps via `Diode::limitJunctionVoltage`, tests `|Δv|<voltageTol`; advance history once after convergence; return `NewtonStatus{converged,iterations,voltageResidual,currentResidual}` (defaults `50/1e-9/1e-12`) (FR-013/FR-014).
- [ ] T016 [P] [US2] Analytic-gain sanity in `tests/core/opamp-stage-solve-test.cpp`: the non-inverting stage solves to `1+Rf/Rg` and the inverting stage to `−Rf/Rin` to ~1e-9 — the nullor augmentation is exact (SC-002, rung a).
- [ ] T017 [US2] First-order-response check (SC-002, rung b): the active first-order low-pass matches its closed-form backward-Euler recurrence (DC gain `−Rf/Rin`, corner `1/(Rf·Cf)`) to ~1e-9 — nullor+reactive exact before any nonlinearity.
- [ ] T018 [US2] DC-limit oracle cross-check (SC-003, rung c): implement an independent bisection root-find of the clipper's KCL at the virtual-short node (equation in `data-model.md`) and assert the clipper's settled DC output matches it to ~1e-6 — a genuine cross-check, not solver-vs-itself. (Consumes the US1 clipper builder.)
- [ ] T019 [US2] Bounded-charter tripwires (FR-015, SC-007): assert (i) the augmentation touches only `OpAmp` branches (a `VoltageSource` in the net is still fixed-node-reduced, not augmented); (ii) a netlist with ≥2 interacting nonlinearities at distinct node pairs → descriptive out-of-scope `std::runtime_error` (deferred to Phase 5); (iii) the augmented dimension is the compile-time `reducedNodes + numOpAmps` (+`2·MaxDiodes` for the clipper) — one row/col per op-amp, no dynamic growth.
- [ ] T020 [US2] Non-convergence contract (SC-006): a starved iteration budget against a stiff excitation yields `NewtonStatus.converged == false` with the residual **surfaced** to the test — no fallback, no fabricated output (FR-014). Asserted in `opamp-stage-solve-test.cpp`.

**Checkpoint**: the nullor-augmented solver is proven exact on the linear + first-order stages and the DC-limit oracle; its bounded charter and non-convergence contract are verified.

---

## Phase 5: User Story 3 - Validate the assembled stages' behavior (Priority: P3)

**Goal**: each assembled stage behaves as its topology dictates — linear gains hold, and the op-amp+diode clipper saturates, carries its population's symmetry, dissipates in its passive sub-network, and softens its HF as the feedback cap grows.
**Independent test**: drive each builder's netlist through the solver with fixed excitation and check the invariants directly.
**Depends on**: US1 (netlists) + US2 (trustworthy solver).

### Implementation & Tests for User Story 3

- [ ] T021 [US3] Saturation + symmetry invariants (SC-004) in `opamp-stage-solve-test.cpp`: the clipper driven far past the feedback-diode threshold clamps its output near the diode forward drop (bounded, not tracking input linearly); a symmetric feedback-diode population yields an odd-symmetric transfer `y(−x)=−y(x)` within tolerance, an asymmetric population a measurable DC-offset / even-harmonic component — the two are distinguishable.
- [ ] T022 [US3] Passivity (SC-004): over a bounded excitation, the clipper's **passive sub-network** dissipates — its output energy ≤ input energy (the op-amp's active gain accounted separately).
- [ ] T023 [US3] Reactive signature (SC-005): with a fixed 1 kHz sine driven into clipping at `dt=1e-5 s` (100 kHz), the output spectral energy above a 5 kHz cutoff strictly decreases at each step of an ascending feedback-`Cf` sweep — the TS "soft" clipping band the static transfer curve cannot represent.
- [ ] T024 [US3] Implement `core/labs/opamp-stages/harness/opamp-stages-harness.cpp` mirroring the Tier-2 assertions (analytic gains, first-order response, DC-limit oracle, saturation, symmetry, passivity, reactive signature, **the non-convergence check, and the bounded-charter refusal**) with PASS/FAIL measured-vs-expected prints; exits nonzero on any failure (FR-023).

**Checkpoint**: the assembled stages are validated by the proven-exact nullor-augmented solver + the independent DC-limit oracle + the behavioral invariants.

---

## Phase 6: Polish & Cross-Cutting Concerns

- [ ] T025 [P] Verify isolation (SC-008 / FR-024) at the **dependency level**: the `OpAmp` element (`models/opamp.h`), the `opamp-stage/` builder headers, and the Tier-1 test (`opamp-stage-builder-test.cpp`) include **nothing** under `core/labs/` — grep-verified — so they compile independent of the lab. Only the Tier-2 `opamp-stage-solve-test.cpp` and the lab harness include the solver; those are the artifacts that "go away" when the lab is deleted. (As with `diode-clippers` T020: Tier-1 and Tier-2 share one `acfx_core_tests` executable, so the guarantee asserted is the primitive's dependency-independence.)
- [ ] T026 [P] No-heap audit (SC-009): confirm no `new`/`delete`/`std::vector` under `core/primitives/circuit/opamp-stage/` or in `models/opamp.h`; the solver's solve path is `std::array`-backed; capacities are compile-time `Netlist`/template parameters.
- [ ] T027 [P] Full green + hygiene: `make test` and the harness both pass; each new file ≤ ~500 lines (Constitution VII; split `opamp-stage-solver.h` and/or `opamp-stage-solve-test.cpp` if they approach the ceiling); the existing `component-abstractions` `LinearSolver` is **unmodified** (FR-015); the vocabulary gained **exactly one** new element (`OpAmp`) and no existing element was modified beyond the additive classifier/terminal cases (FR-007).

---

## Dependencies & Story Completion Order

- **Setup (T001–T003)** → everything.
- **Foundational (T004 `OpAmp` element, T005 `opamp-config.h`)** → blocks **US1**, **US2**, **US3**.
- **US1 (T006–T012)** depends on T004+T005. → **MVP.**
- **US2 (T013–T020)** depends on T004+T005; the solver core (T013–T015) is independent of US1, while the gain/first-order/DC checks (T016–T018) consume the US1 builders.
- **US3 (T021–T024)** depends on US1 (built netlists) + US2 (trustworthy solver).
- **Polish (T025–T027)** depends on all prior.

Story order: **US1 (MVP) → US2 → US3.** US1 and the US2 solver core can proceed largely in parallel once T004+T005 land (different files: `opamp-stage.h` vs `opamp-stage-solver.h`), converging at T016–T018.

## Parallel Opportunities

- Setup: T002 ∥ T003.
- After T004+T005: US1 impl (T007–T011) ∥ US2 solver (T013) ∥ Tier-1 test (T006). T012 ∥ US1 impl.
- Within US2: T016 (analytic-gain sanity) ∥ T013 authoring; T017/T018/T019/T020 follow the solver.
- US3: T021 ∥ T022 ∥ T023 (distinct invariants), then T024 harness mirrors all.
- Polish: T025 ∥ T026 ∥ T027.

## Implementation Strategy

**MVP first**: Setup → Foundational → US1 yields the `OpAmp` vocabulary element plus working, solver-neutral op-amp-stage builders with topology tests — deliverable and independently valuable (a solver author, and the later `tube-screamer` / `rat-distortion` / `neve-preamp` features, can already consume them). Then US2 adds and proves the bounded nullor-augmented solver (the feature's defining increment — the first taste of MNA), and US3 adds the assembled-stage invariants that make each stage *recognizable* — including the reactive signature. Each story is a complete, independently testable increment; ship/commit at each checkpoint.

## Independent Test Criteria (per story)

- **US1**: the four builders return `prepare()`-valid netlists holding only vocabulary elements (exactly one `OpAmp` each) at representative BOMs; counts match the topology; port nodes correct; no lab include. (No solver.)
- **US2**: the solver matches the analytic gains (`1+Rf/Rg`, `−Rf/Rin`) and the first-order response to ~1e-9 and the independent bisection DC-limit oracle to ~1e-6; the three bounded-charter tripwires hold; a starved budget reports `converged=false` (surfaced).
- **US3**: the clipper saturates near the feedback-diode drop and is passive; a symmetric population is odd-symmetric / an asymmetric one shows a DC offset; larger feedback `Cf` strictly reduces post-clip >5 kHz energy at 1 kHz / 100 kHz.
