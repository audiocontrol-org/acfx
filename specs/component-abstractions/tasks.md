---

description: "Task list for component-abstractions implementation"
---

> ‼ **acfx COMMANDMENTS — non-negotiable** ‼
> **1. COMMIT AND PUSH EARLY AND OFTEN** — version control is a distributed, journaled
> filesystem that safeguards your work, **NOT a sacred rite reserved for the blessed.**
> Small atomic commits, pushed promptly; never hoard unpushed work.
> **2. NO GIT HOOKS, EVER** — this repo uses zero git hooks; none exist, none get added.
> **3. DESCRIPTIVE NAMES, NEVER NUMERIC PREFIXES** — names carry information; fake sequence
> numbers (`001-`) imply false order and false precision (datestamps excepted).
> (acfx Constitution, Principles I–III — `.specify/memory/constitution.md`.)

# Tasks: component-abstractions

**Input**: Design documents from `specs/component-abstractions/`

**Prerequisites**: plan.md, spec.md, research.md, data-model.md, contracts/ — all present.

**Tests**: INCLUDED. The spec's Success Criteria (SC-001..008) are test-defined and Constitution VIII ("Test the Core Host-Side") mandates host-side unit tests. Tests are written before/with the code they validate.

**Organization**: grouped by user story so each is independently implementable and testable.

## Format: `[ID] [P?] [Story?] [tier:label] Description with file path`

- **[P]**: can run in parallel (different files, no dependency on an incomplete task).
- **[Story]**: US1..US4 (setup/foundational/polish carry no story label).
- **[tier:label]**: model-sized-dispatch tier (033) — `fast`/`balanced`/`powerful` resolve via
  `.stack-control/config.yaml` `tier_map` to haiku/sonnet/opus.

## Path Conventions

acfx three-layer core: primitive vocabulary in `core/primitives/circuit/`, the reference
solver + harness in `core/labs/component-abstractions/`, host tests in `tests/core/`. Build
wiring in root `CMakeLists.txt` (lab harness) and `tests/CMakeLists.txt` (test suites).

---

## Phase 1: Setup (Shared Infrastructure)

- [ ] T001 [tier:fast] Create the lab scaffold `core/labs/component-abstractions/` (`README.md` stub stating "deliberately-naive reference solver — Phase 5 MNA supersedes it", `solver/`, `harness/`) and register a host-only `acfx_lab_component_abstractions_harness` executable in `CMakeLists.txt`, mirroring the existing `acfx_lab_svf_harness` block.
- [ ] T002 [P] [tier:fast] Create skeleton test files `tests/core/circuit-components-test.cpp`, `tests/core/circuit-netlist-test.cpp`, `tests/core/circuit-solver-test.cpp` (empty GoogleTest `TEST` stubs) and register all three in `tests/CMakeLists.txt`, mirroring `core/svf-test.cpp`.
- [ ] T003 [P] [tier:balanced] Extend `scripts/check-portability.sh` to cover `core/primitives/circuit/` (must be C++17-clean, no JUCE/libDaisy/Teensy include) and to assert `core/labs/component-abstractions/` is host-only (lab isolation).

---

## Phase 2: Foundational (blocking prerequisites — MUST complete before user stories)

**Purpose**: the node + container + netlist skeleton every story builds on. Creating `node.h` is what materializes the `circuit/` category folder (inhabit-before-creating, FR-018).

- [ ] T004 [tier:balanced] Create `core/primitives/circuit/node.h` — `NodeId` (integer handle, ground ≡ 0) and node-count helpers (data-model "NodeId"; FR-001). This is the `circuit/` category's first inhabitant.
- [ ] T005 [tier:balanced] Create `core/primitives/circuit/components.h` — the empty-but-typed `Component = std::variant<Resistor, Capacitor, Inductor, VoltageSource, CurrentSource, Diode>` alias with forward-declared per-type headers, plus `isLinear`/`isReactive`/`isNonlinear` `std::visit` classifiers (data-model "Component (container form)"; FR-008, R4). No physics yet — just the closed type set and dispatch.
- [ ] T006 [tier:powerful] Create `core/primitives/circuit/netlist.h` — `template <int MaxNodes, int MaxComponents> class Netlist` with `addNode()`, `add()` (over-capacity → descriptive throw), `prepare()` (stub validation for now), and immutable `components()`/counts accessors; `std::array`-backed, heap-free (contract `netlist.md`; FR-009).

**Checkpoint**: primitive compiles under `-std=c++17`; no story code yet.

---

## Phase 3: User Story 1 — Represent a component's physics, solver-neutrally (Priority: P1)

**Goal**: each v1 component carries its own physics; no solver concept leaks in.
**Independent test**: construct each component and query its physics directly against the closed form (no netlist, no solver).

- [ ] T007 [P] [US1] [tier:balanced] Implement `core/primitives/circuit/models/resistor.h` — `Resistor{a,b,R}` with `admittance() = 1/R` and correct nodal signs (contract `component-physics.md` §Linear; FR-003).
- [ ] T008 [P] [US1] [tier:balanced] Implement `core/primitives/circuit/models/sources.h` — `VoltageSource{p,n,V}` (ideal; marker for fixed-node reduction) and `CurrentSource{p,n,I}` (RHS contribution) (contract §Sources; FR-007).
- [ ] T009 [P] [US1] [tier:powerful] Implement `core/primitives/circuit/models/diode.h` — `Diode{anode,cathode,Is,n,Vt}` with `evaluate(vAK) → {current, conductance}` = Shockley `Is*(exp(vAK/(n*Vt))-1)` and `dI/dV`, computed in `double`; include a `Vcrit`/limiting helper for the solver's Newton step (research R2; FR-004).
- [ ] T010 [P] [US1] [tier:powerful] Implement `core/primitives/circuit/models/capacitor.h` — `Capacitor{a,b,C}` with `companion(dt, vPrev) → {Geq=C/dt, Ieq=Geq*vPrev}`; component holds no history (research R3; FR-005).
- [ ] T011 [P] [US1] [tier:powerful] Implement `core/primitives/circuit/models/inductor.h` — `Inductor{a,b,L}` with `companion(dt, iPrev) → {Geq=dt/L, Ieq=-iPrev}`, the dual of the capacitor (research R3; FR-005).
- [ ] T012 [US1] [tier:balanced] Fill `tests/core/circuit-components-test.cpp` — per-component physics vs closed form: resistor `G=1/R` exact; diode current + conductance at forward bias; capacitor & inductor companions for a chosen `dt`; and a grep/static assertion that no `stamp`/`scatter` symbol exists in `circuit/` (US1 acceptance 1–4; SC-001; FR-006).

**Checkpoint**: US1 independently testable and green.

---

## Phase 4: User Story 2 — Assemble a circuit as a typed netlist (Priority: P1)

**Goal**: components compose into a validated, heap-free netlist.
**Independent test**: a good divider validates; three ill-posed netlists each throw a distinct descriptive error.

- [ ] T013 [US2] [tier:powerful] Implement `prepare()` topology validation in `core/primitives/circuit/netlist.h` — missing-ground, floating-node (names the node), and over-capacity checks, each a **distinct descriptive throw**; guarantee the post-`prepare()` solve path neither throws nor allocates (contract `netlist.md`; FR-010/011).
- [ ] T014 [US2] [tier:balanced] Fill `tests/core/circuit-netlist-test.cpp` — good divider validates + reports counts; floating-node / missing-ground / over-capacity each raise their distinct error; wrap a representative solve loop in a **no-allocation assertion** (US2 acceptance 1–4; SC-005/006).

**Checkpoint**: US1+US2 = the MVP (the solver-independent vocabulary + validated assembly). Verify the primitive tests pass with `core/labs/component-abstractions/` absent (SC-007).

---

## Phase 5: User Story 3 — Solve and validate a linear circuit (reference solver, lab) (Priority: P2)

**Goal**: linear circuits run in the lab and match analytic references.
**Independent test**: divider exact; RC/RLC within backward-Euler tolerance.

- [ ] T015 [US3] [tier:powerful] Implement `core/labs/component-abstractions/solver/linear-solver.h` — assemble the reduced nodal system from component `admittance()`/`companion()`/current-source RHS, impose ideal `VoltageSource` by **fixed-node reduction** (research R1 — no gmin fallback, no MNA augmentation), solve by fixed-size Gaussian elimination with **no heap in the solve**; own per-node previous voltages + per-inductor previous currents for the companions (contract `reference-solver.md` §Linear; FR-013/R3).
- [ ] T016 [US3] [tier:balanced] Add divider / RC-sweep / RLC validation cases to `core/labs/component-abstractions/harness/component-abstractions-harness.cpp` and mirror the assertions in `tests/core/circuit-solver-test.cpp` — divider exact ratio; RC magnitude/phase vs `1/(1+jωRC)`; RLC vs analytic 2nd-order; all within documented tolerance (US3 acceptance 1–4; SC-002/003).

**Checkpoint**: linear circuits validated end-to-end.

---

## Phase 6: User Story 4 — Solve a single nonlinearity: the diode clipper (Priority: P2)

**Goal**: one nonlinearity resolved by bounded Newton; ≥2 refused.
**Independent test**: single/antiparallel clipper transfer matches analytic soft-clip; ≥2-nonlinearity netlist refused.

- [ ] T017 [US4] [tier:powerful] Implement `core/labs/component-abstractions/solver/newton-clipper.h` — bounded fixed-iteration, **voltage-limited** Newton around `Diode.evaluate` on top of the linear solver; report residual/status; on non-convergence within the bound, report — never fall back or fabricate (contract §Nonlinear; FR-015/R2).
- [ ] T018 [US4] [tier:powerful] Enforce the scope boundary: the reference solver **refuses** a netlist with ≥2 interacting nonlinear components with the descriptive `"out of reference-solver scope — deferred to Phase 5"` error (contract §Scope; FR-016).
- [ ] T019 [US4] [tier:balanced] Add single-diode + antiparallel clipper DC-sweep cases (vs analytic soft-clip) and the ≥2-nonlinearity refusal case to the harness and `tests/core/circuit-solver-test.cpp` (US4 acceptance 1–4; SC-004/005).

**Checkpoint**: all four stories independently green.

---

## Phase 7: Polish & Cross-Cutting Concerns

- [ ] T020 [P] [tier:fast] Update `core/primitives/README.md` — register the `circuit/` category and its six inhabitants (R, C, L, V, I, diode) with consumers/lab, matching the existing taxonomy discipline (FR-021; SC-008).
- [ ] T021 [P] [tier:balanced] Write `core/labs/component-abstractions/README.md` — what the lab teaches, the fixed-node-reduction/backward-Euler/Newton choices, and the explicit "naive, non-normative, Phase-5-superseded" boundary.
- [ ] T022 [tier:balanced] Verify + document **OQ5**: measure the `std::variant` + templated-`Netlist<N,M>` + `double` instantiation code-size/`std::visit` cost on the Teensy build (`make teensy` if toolchain present, else record the host `-Os` size delta and note the Teensy measurement as outstanding); record the finding in `research.md` (R4) — do not change the container unless the measurement demands it.
- [ ] T023 [tier:fast] Full-suite green + isolation check: `make test` passes; re-confirm the primitive compiles under `-std=c++17` and its `circuit-components`/`circuit-netlist` tests pass with the lab directory temporarily excluded (SC-006/007).

---

## Dependencies & Story Completion Order

- **Setup (T001–T003)** → **Foundational (T004–T006)** block everything.
- **US1 (T007–T012)** and **US2 (T013–T014)** are both P1 and form the MVP; US2 depends on Foundational, not on US1's physics being complete (the netlist stores `Component`s opaquely) — but the US2 tests exercise real components, so run US1 first in practice.
- **US3 (T015–T016)** depends on US1 (physics) + US2 (validated netlist).
- **US4 (T017–T019)** depends on US3 (the linear solver it iterates on).
- **Polish (T020–T023)** last.

## Parallel Opportunities

- T002 ∥ T003 (setup, different files).
- The per-component headers **T007 ∥ T008 ∥ T009 ∥ T010 ∥ T011** are fully parallel (distinct files, no interdependency).
- T020 ∥ T021 (two READMEs).

## Implementation Strategy

- **MVP = US1 + US2** (Phases 3–4): the solver-independent vocabulary and validated assembly — deliverable and testable with **no lab/solver at all** (SC-007). Ship/checkpoint here first.
- Then **US3 → US4** (Phases 5–6) add runnability in the lab, in dependency order.
- **Polish** closes the taxonomy docs, the OQ5 measurement, and the isolation re-check.

**Total: 23 tasks** — Setup 3, Foundational 3, US1 6, US2 2, US3 2, US4 3, Polish 4.
