> ‼ **acfx COMMANDMENTS — non-negotiable** ‼
> **1. COMMIT AND PUSH EARLY AND OFTEN** — small atomic commits, pushed promptly.
> **2. NO GIT HOOKS, EVER** — zero hooks; gates are explicit build/test steps.
> **3. DESCRIPTIVE NAMES, NEVER NUMERIC PREFIXES** — names carry information.
> (acfx Constitution, Principles I–III.)

# Tasks: diode-clippers

**Feature dir**: `specs/diode-clippers/` | **Spec**: [spec.md](./spec.md) | **Plan**: [plan.md](./plan.md)

Tests are **included** — the spec mandates two validation tiers (Tier-1 primitive tests, Tier-2 transient/invariant) plus a lab harness (Constitution VIII, test the core host-side). Within each story, tests come before/with implementation.

## Format: `[ID] [P?] [Story] Description with file path`

- **[P]** = parallelizable (different files, no incomplete dependency).
- **[US1/US2/US3]** = user-story phase tasks only (Setup/Foundational/Polish carry no story label).

## Path Conventions

- Primitive (portable, C++17): `core/primitives/circuit/diode-clipper/`
- Lab (host-only, C++20 ok): `core/labs/diode-clippers/`
- Tests (host-side): `tests/core/`

---

## Phase 1: Setup (Shared Infrastructure)

- [X] T001 Create the primitive subfolder `core/primitives/circuit/diode-clipper/` and the lab tree `core/labs/diode-clippers/{solver,harness}/` (empty placeholders to be filled by later tasks).
- [X] T002 [P] Register the two new host tests (`diode-clipper-builder-test.cpp`, `diode-clipper-transient-test.cpp`) in `tests/CMakeLists.txt`, and add the lab harness target `acfx_lab_diode_clippers_harness` (source `core/labs/diode-clippers/harness/diode-clippers-harness.cpp`, C++20) in the root `CMakeLists.txt`, mirroring the `component-abstractions` / `passive-tone-stacks` lab-harness registration.
- [X] T003 [P] Write the lab boundary note `core/labs/diode-clippers/README.md` (host-only, non-normative, **bounded transient — not MNA**, single-port single-nonlinearity, isolation guarantee) — mirrors `core/labs/component-abstractions/README.md` and `core/labs/passive-tone-stacks/README.md`.

---

## Phase 2: Foundational (Blocking Prerequisites)

**Blocks US1, US2, US3** — the config/value types are the shared vocabulary the builders emit and the solver/oracle read.

- [X] T004 Implement `core/primitives/circuit/diode-clipper/clipper-config.h`: `DiodeSpec{Is,n,Vt}`, `SymmetricShuntValues`, `AsymmetricShuntValues{...,upCount,downCount}`, `SeriesValues{...,seriesCount}`, the `Clipper<MaxNodes,MaxComponents>` return struct `{netlist,inNode,outNode,portP,portN}`, and the per-topology capacity aliases (`SymmetricShuntClipper`, `AsymmetricShuntClipper`, `SeriesClipper`) per `data-model.md`. Include `detail::requirePositive` and population-validation helpers. Header-only, C++17, standard-library only, ≤ ~300 lines. Contract: `contracts/diode-clipper-builder.md`.

**Checkpoint**: `clipper-config.h` compiles standalone; US1/US2/US3 can begin.

---

## Phase 3: User Story 1 - Assemble a named diode-clipper stage, solver-neutrally (Priority: P1) 🎯 MVP

**Goal**: the three builders return a `prepare()`-valid `Netlist` of frozen-vocabulary components.
**Independent test**: build each clipper at representative BOMs, `prepare()` passes, counts match the BOM, only frozen-vocabulary elements present, port nodes correct — no solver.

### Tests for User Story 1

- [X] T005 [P] [US1] `tests/core/diode-clipper-builder-test.cpp`: for each of the three builders assert `prepare()` succeeds at representative BOMs; component and node counts equal the topology's BOM; every held component is `Resistor`/`Capacitor`/`Diode`/`VoltageSource` (frozen vocabulary); the reported `portP/portN` are the diode-string node pair; and a compile-level check that `diode-clipper.h`/`clipper-config.h` include nothing under `core/labs/` (isolation, FR-019). Tests fail until T006–T009 land.

### Implementation for User Story 1

- [X] T006 [US1] Implement `symmetricShuntClipper(...)` in `core/primitives/circuit/diode-clipper/diode-clipper.h`: `Vin`(grounded `VoltageSource`)→series `R`→`n1`; matched antiparallel `Diode` pair `Diode{n1,gnd}` + `Diode{gnd,n1}`; filter `Cf` `n1`→gnd; `prepare()`; return `SymmetricShuntClipper{netlist, inNode=Vin-node, outNode=n1, portP=n1, portN=gnd}`.
- [X] T007 [US1] Implement `asymmetricShuntClipper(...)` in `diode-clipper.h`: series `R`→`n1`; `upCount` `Diode{n1,gnd}` + `downCount` `Diode{gnd,n1}` (v1 canonical 2-up/1-down); `Cf` across; require `upCount != downCount` and `upCount+downCount ≤ MaxDiodes`; `prepare()`; return with port `(n1,gnd)`.
- [X] T008 [US1] Implement `seriesClipper(...)` in `diode-clipper.h`: `Vin`→input coupling `Cc`(series)→`n1`; `seriesCount` inline `Diode`s `n1`→`n2`; `R` `n2`→gnd; `prepare()`; return `{outNode=n2, portP=n1, portN=n2}` (coupling cap blocks DC).
- [X] T009 [US1] Add builder input validation in `diode-clipper.h`/`clipper-config.h`: any non-positive resistance/capacitance, non-positive diode parameter (`Is`/`n`/`Vt`), or out-of-range population (total diodes `> MaxDiodes`, or `upCount == downCount` for the asymmetric builder) → descriptive `std::invalid_argument` naming the field (FR-007). No silent clamp.
- [X] T010 [P] [US1] Update `core/primitives/README.md` to register the `circuit/diode-clipper/` subfolder and the three builders.

**Checkpoint**: US1 independently testable — MVP delivered (solver-neutral diode-clipper builders the later named-pedal features compose).

---

## Phase 4: User Story 2 - Solve the assembled clipper's reactive nonlinear transient (Priority: P2)

**Goal**: `TransientClipper` advances an assembled clipper one timestep — backward-Euler reactive companions + bounded Newton, with separated timestep/Newton loops — and reports convergence.
**Independent test**: run the solver on hand-built sanity nets (linear RC; resistor+diode at DC) to the closed form / bisection oracle; drive deliberate non-convergence and confirm it is reported.
**Depends on**: Foundational (T004). The DC-limit cross-checks (T013) additionally consume the US1 builders.

### Implementation & Tests for User Story 2

- [X] T011 [US2] Implement `core/labs/diode-clippers/solver/transient-clipper.h`: `TransientClipper<MaxNodes,MaxComponents,MaxDiodes=4>` with ctor `(maxIterations=50, voltageTol=1e-9, currentTol=1e-12)` (validate → `std::invalid_argument`), `reset()`, and `step(nl, dt) → NewtonStatus`. **Separated loops (FR-009):** compute reactive companions once per timestep from held history via the frozen `capacitor.h`/`inductor.h` `companion(dt,·)`; inner Newton holds companions fixed — companion-linearize the diode string into Norton pairs appended to a stack `Netlist<MaxNodes, MaxComponents + 2·MaxDiodes>`, solve with a nested `LinearSolver`, damp via `Diode::limitJunctionVoltage` (pnjlim), test `|Δv| < voltageTol`; advance reactive history exactly once after convergence. `dt ≤ 0` / singular → descriptive throw (FR-013); heap-free on the `step()` path (FR-008/FR-010/FR-014). Contract: `contracts/transient-clipper.md`.
- [X] T012 [P] [US2] `tests/core/diode-clipper-transient-test.cpp` sanity block: a **linear-only** RC network (no diode) stepped over many samples matches the closed-form backward-Euler recurrence `v[n]=α·Vin+(1−α)v[n−1]`, `α=dt/(dt+RC)`, to ~1e-9 — proving the reactive discretization + timestep/history handling before any nonlinearity is trusted (SC-002 part 1).
- [X] T013 [US2] DC-limit oracle cross-check (SC-002 part 2): implement an independent bisection root-find per topology (equations in `data-model.md`) and assert each clipper's **settled DC** port voltage matches the oracle to ~1e-6 **and** agrees with the existing static `component-abstractions` `NewtonClipper` curve. (Consumes the US1 builders.)
- [X] T014 [US2] Bound checks (FR-012): the asymmetric clipper (3 diodes) solves with **all** diodes present (the templated `MaxDiodes` admits the population, never dropping one); a netlist with a second nonlinearity at a **distinct** node pair → descriptive `std::runtime_error` (out of the bounded single-port scope; Phase 5).
- [X] T015 [US2] Non-convergence contract (SC-006): a starved iteration budget against a stiff excitation yields `NewtonStatus.converged == false` with the residual **surfaced** to the test — no fallback, no fabricated output (FR-011). Asserted in `diode-clipper-transient-test.cpp`.

**Checkpoint**: the transient solver is proven exact on the linear sanity net + the DC-limit oracle, its bound and non-convergence contract verified.

---

## Phase 5: User Story 3 - Validate the assembled clippers' behavior (Priority: P3)

**Goal**: each assembled clipper behaves as its topology dictates — symmetry, saturation, passivity, and the reactive signature the static curve cannot show.
**Independent test**: drive each builder's netlist through the transient solver with fixed excitation and check the invariants directly.
**Depends on**: US1 (netlists) + US2 (trustworthy solver).

### Implementation & Tests for User Story 3

- [X] T016 [US3] Symmetry invariants (SC-003) in `diode-clipper-transient-test.cpp`: the symmetric shunt clipper's transfer is odd-symmetric `y(−x)=−y(x)` within tolerance (no DC offset); the asymmetric shunt clipper's transfer carries a measurable DC-offset / even-harmonic component — the two are distinguishable by this invariant.
- [X] T017 [US3] Saturation + passivity (SC-004): each clipper driven far past the diode threshold clamps its output near the diode forward drop (bounded); over a bounded excitation, output energy ≤ input energy (the passive network dissipates, never adds gain).
- [X] T018 [US3] Reactive signature (SC-005): with a fixed 1 kHz sine driven into clipping at `dt=1e-5 s` (100 kHz), the output spectral energy above a 5 kHz cutoff strictly decreases at each step of an ascending `Cf` sweep — **for the two shunt clippers** (whose `Cf` sits across the diodes). The **series** clipper's coupling cap `Cc` is a high-pass element, not covered by this invariant (FR-017). (The behavior a static transfer curve cannot represent.)
- [X] T019 [US3] Implement `core/labs/diode-clippers/harness/diode-clippers-harness.cpp` mirroring the Tier-2 assertions (linear RC step, DC-limit oracle, symmetry, saturation, passivity, reactive signature, **and the non-convergence check**) with PASS/FAIL measured-vs-expected prints; exits nonzero on any failure (FR-018).

**Checkpoint**: the assembled clippers are validated by the proven-exact solver + the independent DC-limit oracle + the behavioral invariants.

---

## Phase 6: Polish & Cross-Cutting Concerns

- [X] T020 [P] Verify isolation (SC-007 / FR-019) at the **dependency level** (the substantive guarantee): the `diode-clipper/` primitive headers and the Tier-1 test (`diode-clipper-builder-test.cpp`) include **nothing** under `core/labs/` — grep-verified — so they compile independent of the lab. Only the Tier-2 `diode-clipper-transient-test.cpp` and the lab harness include the transient solver; those are the artifacts that "go away" when the lab is deleted. (As with `passive-tone-stacks` T019: Tier-1 and Tier-2 share one `acfx_core_tests` executable, so the guarantee asserted is the primitive's dependency-independence, not a single-target rebuild trick.)
- [X] T021 [P] No-heap audit (SC-008): confirm no `new`/`delete`/`std::vector` under `core/primitives/circuit/diode-clipper/`; the solver's `step()` path is `std::array`-backed; capacities are compile-time `Netlist`/template parameters.
- [X] T022 [P] Full green + hygiene: `make test` and the harness both pass; each new file ≤ ~500 lines (Constitution VII; split `transient-clipper.h` if it approaches the ceiling); the `component-abstractions` `circuit/` vocabulary is unmodified (FR-004).

---

## Dependencies & Story Completion Order

- **Setup (T001–T003)** → everything.
- **Foundational (T004, `clipper-config.h`)** → blocks **US1**, **US2**, **US3**.
- **US1 (T005–T010)** depends on T004. → **MVP.**
- **US2 (T011–T015)** depends on T004; the solver + linear/non-convergence checks (T011, T012, T014, T015) are independent of US1, while the DC-limit cross-check (T013) consumes the US1 builders.
- **US3 (T016–T019)** depends on US1 (built netlists) + US2 (trustworthy solver).
- **Polish (T020–T022)** depends on all prior.

Story order: **US1 (MVP) → US2 → US3.** US1 and the US2 solver core can proceed largely in parallel once T004 lands (different files: `diode-clipper.h` vs `transient-clipper.h`), converging at T013.

## Parallel Opportunities

- Setup: T002 ∥ T003.
- After T004: US1 impl (T006–T009) ∥ US2 solver (T011) ∥ Tier-1 test (T005). T010 ∥ US1 impl.
- Within US2: T012 (linear sanity) ∥ T011 authoring; T013/T014/T015 follow the solver.
- US3: T016 ∥ T017 ∥ T018 (distinct invariants), then T019 harness mirrors all.
- Polish: T020 ∥ T021 ∥ T022.

## Implementation Strategy

**MVP first**: Setup → Foundational → US1 yields working, solver-neutral diode-clipper builders with topology tests — deliverable and independently valuable (a solver author, and the later named-pedal features, can already consume them). Then US2 adds and proves the bounded transient nonlinear solver (the feature's defining increment over the shipped static curve), and US3 adds the assembled-clipper invariants that make each stage *recognizable* — including the reactive signature. Each story is a complete, independently testable increment; ship/commit at each checkpoint.

## Independent Test Criteria (per story)

- **US1**: the three builders return `prepare()`-valid, frozen-vocabulary netlists at representative BOMs; counts match the topology; port nodes correct; no lab include. (No solver.)
- **US2**: the solver matches the analytic backward-Euler RC recurrence to ~1e-9 and the independent bisection DC-limit oracle to ~1e-6 (agreeing with the static `NewtonClipper`); the diode-cap and interacting-nonlinearity bounds hold; a starved budget reports `converged=false` (surfaced).
- **US3**: symmetric clipper odd-symmetric / asymmetric shows DC offset; every clipper saturates near the diode drop and is passive; larger `Cf` strictly reduces post-clip >5 kHz energy at 1 kHz / 100 kHz.
