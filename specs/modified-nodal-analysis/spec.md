> ‼ **acfx COMMANDMENTS — non-negotiable** ‼
> **1. COMMIT AND PUSH EARLY AND OFTEN** — version control is a distributed, journaled
> filesystem that safeguards your work, **NOT a sacred rite reserved for the blessed.**
> Small atomic commits, pushed promptly; never hoard unpushed work.
> **2. NO GIT HOOKS, EVER** — this repo uses zero git hooks; none exist, none get added.
> **3. DESCRIPTIVE NAMES, NEVER NUMERIC PREFIXES** — names carry information; fake sequence
> numbers (`001-`) imply false order and false precision (datestamps excepted).
> (acfx Constitution, Principles I–III — `.specify/memory/constitution.md`.)

# Feature Specification: Modified Nodal Analysis (MNA) primitive

**Feature Branch**: `modified-nodal-analysis`

**Created**: 2026-07-07

**Status**: Draft

**Input**: Approved design record — `docs/superpowers/specs/2026-07-07-modified-nodal-analysis-design.md`
(roadmap item `design:primitive/modified-nodal-analysis`, part-of
`multi:feature/phase-numerical-solvers`).

## Context

The acfx circuit-modeling core has no general Modified Nodal Analysis engine. Four
lab solvers (`LinearSolver`, `NullorSolver`/`AugmentedSolver`, the clipper solvers)
each hand-roll Gaussian-elimination-with-partial-pivoting + node reduction +
companion stamping; backlog **TASK-14** asks for that duplicated linear-algebra
core to be unified. This feature builds that shared core as a production primitive.

MNA is one of **three** coordinated sibling primitives under
`phase-numerical-solvers`. `design:primitive/newton-iteration` (the nonlinear outer
loop) and `design:primitive/implicit-integration` (the time-stepping / companion
layer) are **separate** siblings that *call* this primitive. MNA is the linear
heart both invoke; it MUST NOT absorb their responsibilities.

The "users" of this primitive are **calling code** — the sibling solvers, the
existing lab solvers that will later migrate onto it (follow-on, TASK-14), and DSP
developers assembling circuits. It consumes the frozen component vocabulary in
`core/primitives/circuit/` and adds no new component types.

## User Scenarios & Testing *(mandatory)*

### User Story 1 - Solve a linear resistive/source network exactly (Priority: P1)

Calling code has a netlist of resistors, current sources, and (grounded or
floating) ideal voltage sources. It wants exact node voltages and source branch
currents at a fixed operating point.

**Why this priority**: This is the irreducible core — the abstract linear engine
plus resistor/source stamping. Without it there is no MNA. It already delivers a
capability the labs lack: **floating** (non-grounded) ideal voltage sources, which
the labs refuse. It is a standalone, demonstrable MVP.

**Independent Test**: Assemble a resistive divider and read the mid-node voltage to
floating-point precision; assemble a voltage source between two non-ground nodes
and confirm the imposed difference and branch current exactly. No reactive or
nonlinear elements needed.

**Acceptance Scenarios**:

1. **Given** a divider `Vin–R1–node–R2–gnd`, **When** the netlist is assembled and
   solved, **Then** `V(node) == Vin·R2/(R1+R2)` to floating-point precision.
2. **Given** an ideal voltage source between two non-ground nodes `a` and `b`,
   **When** solved, **Then** `V(a) − V(b)` equals the source value exactly and the
   source branch current is available and correct.
3. **Given** a current source into a node with a resistor to ground, **When**
   solved, **Then** the node voltage equals `I·R` exactly.

---

### User Story 2 - Solve an op-amp (nullor) circuit exactly (Priority: P1)

Calling code has a netlist containing one or more ideal op-amps (nullors) with a
passive feedback network. It wants the exact ideal-op-amp transfer result.

**Why this priority**: Nullor bordering is the defining "Modified" augmentation
beyond plain nodal analysis and is required to reproduce the existing
`NullorSolver` capability inside the unified core. It is independently testable
against known closed-form gains.

**Independent Test**: Build an ideal inverting amplifier and a non-inverting
amplifier and confirm the output equals the closed-form gain exactly.

**Acceptance Scenarios**:

1. **Given** an ideal inverting amplifier (`Rin`, `Rf`, op-amp), **When** solved,
   **Then** `Vout == −Vin·Rf/Rin` to floating-point precision.
2. **Given** an ideal non-inverting amplifier, **When** solved, **Then**
   `Vout == Vin·(1 + Rf/Rg)` to floating-point precision.
3. **Given** the nullator constraint `V(in+) − V(in−) = 0` is stamped, **When**
   solved, **Then** the constraint holds in the solution within pivoting tolerance.

---

### User Story 3 - Compose with caller-supplied companions (Priority: P1)

Calling code (a Newton loop, an integration step) has already reduced each reactive
or nonlinear element to a Norton companion `{Geq, Ieq}` at the current operating
point. It wants MNA to stamp those companions and solve, holding no history or
integration rule itself.

**Why this priority**: This is the seam to the two sibling primitives. It is what
makes MNA the reusable linear heart rather than a monolith. Without it the
decomposition the roadmap drew collapses.

**Independent Test**: Drive an RC network with a hand-written test harness that
supplies the backward-Euler capacitor companion each step, and confirm the sampled
step response matches the backward-Euler recurrence exactly — proving MNA stamps
supplied companions and holds no state.

**Acceptance Scenarios**:

1. **Given** a caller supplies a capacitor companion `{Geq = C/dt, Ieq = Geq·vPrev}`
   each step, **When** MNA assembles + solves and the caller advances history,
   **Then** the sampled response matches the backward-Euler recurrence exactly.
2. **Given** two successive solves with identical (netlist, supplied companions)
   inputs, **When** both are solved, **Then** the results are bit-identical (MNA is
   stateless / a pure function of its inputs).
3. **Given** a diode reduced to a Norton companion by the caller, **When** stamped
   and solved, **Then** the node voltages match the linearized-operating-point
   solution.

---

### User Story 4 - Two-phase assembly with an RT-safe hot path (Priority: P1)

Calling code runs MNA inside an audio-callback / per-sample loop. It plans the
netlist structure once, then refreshes values and solves every sample (and every
Newton iteration) with zero heap allocation and no exceptions on that path.

**Why this priority**: Real-time safety (Constitution Principle VI) is a hard
constraint, not a nicety. If assembly allocated or threw on the hot path the
primitive would be unusable in `process()`.

**Independent Test**: Wrap a plan-once / refresh-and-solve-many loop in the
`AllocationSentinel` and assert zero heap operations across the solve path; confirm
no exception can escape `solve()`.

**Acceptance Scenarios**:

1. **Given** a netlist planned once via the plan phase, **When** the per-solve phase
   refreshes values and solves repeatedly, **Then** `AllocationSentinel` reports
   zero heap allocations and zero deallocations across those solves.
2. **Given** the plan phase has fixed the branch count, **When** the per-solve phase
   runs, **Then** it never calls the branch allocator and never throws.
3. **Given** an over-capacity or malformed netlist, **When** the plan phase runs,
   **Then** it throws a descriptive error **before** any solve — never silently
   truncating and never deferring the failure to the hot path.

---

### User Story 5 - Report ill-posed systems without fallback or corruption (Priority: P2)

Calling code may present a singular / ill-posed circuit (a floating subgraph, a
redundant nullor). It wants an unambiguous "could not solve" signal, not a silently
patched or NaN-poisoned result.

**Why this priority**: A silent gmin fallback or a NaN leak is a bug factory (the
no-fallbacks commandment). The caller — typically the Newton loop — must decide how
to respond. Lower priority than P1 because it is a robustness guarantee on top of
the correct-solve core.

**Independent Test**: Assemble a floating subgraph and a redundant-nullor topology;
confirm `solve()` returns the false / not-solved signal, does not throw on the hot
path, and leaves no NaN in the readable solution.

**Acceptance Scenarios**:

1. **Given** a singular system (floating subgraph), **When** solved, **Then**
   `solve()` returns the not-solved signal (`false`) and does not throw.
2. **Given** a not-solved result, **When** the caller inspects it, **Then** no
   silent gmin conductance was injected and no NaN leaks into subsequent stamps.
3. **Given** a well-posed system whose pivot magnitudes are small but nonzero,
   **When** solved with the relative singular-pivot threshold, **Then** it solves
   correctly rather than being falsely rejected.

---

### User Story 6 - Reproduce the existing lab solvers (equivalence oracle) (Priority: P2)

Before any lab migrates onto MNA (follow-on, TASK-14), calling code / tests must
prove MNA is a faithful superset of `LinearSolver` and `NullorSolver` on the
topologies those solvers already handle.

**Why this priority**: This is the safety net that makes the later migration
low-risk. It is not needed for MNA to be correct in isolation (US1–US3 establish
that), hence P2, but it is required before retiring duplicated code.

**Independent Test**: For each topology the labs cover, run both the lab solver and
MNA and compare node voltages / branch currents to a tight tolerance.

**Acceptance Scenarios**:

1. **Given** a topology within `LinearSolver`'s scope, **When** both solve, **Then**
   node voltages agree to ~1e-12.
2. **Given** a topology within `NullorSolver`'s scope, **When** both solve, **Then**
   node voltages and branch currents agree to ~1e-12.

---

### Edge Cases

- **Floating voltage source** between two non-ground nodes — solved via branch
  augmentation (a capability gain), not refused.
- **Ground node references** — node 0 is the dropped reference; stamps touching
  ground contribute only to the retained rows/columns.
- **Zero-diagonal constraint rows** — the nullator / voltage-source constraint rows
  have a zero diagonal; partial pivoting is required for a correct solve.
- **Branch capacity exceeded** — plan phase throws descriptively; never a silent
  truncation, never a hot-path failure.
- **Out-of-range or invalid node id** — rejected at plan/prepare time.
- **Degenerate element values** (e.g. non-positive resistance) — rejected at
  plan/prepare time.
- **Small-but-nonzero pivots** — the relative singular-pivot threshold must not
  reject a well-posed but poorly scaled system.
- **Repeated solves** — identical inputs must give identical outputs (no residual
  state between solves).

## Requirements *(mandatory)*

### Functional Requirements

**Abstract linear engine (Layer 1 — knows nothing about components)**

- **FR-001**: The engine MUST assemble and solve an augmented linear system
  `A·x = z` whose unknowns are node voltages plus branch currents, with a
  compile-time-fixed dimension (max nodes + max branches) and no dynamic growth.
- **FR-002**: The engine MUST expose a stamp API sufficient to build the system
  without knowledge of component types: reset; stamp a conductance between a node
  pair (ground-aware, four-corner); inject a current into a node's balance;
  allocate a branch-current unknown; stamp a branch's ±1 incidence against a node
  pair; stamp a branch constraint value; and stamp an optional branch-block
  resistance term.
- **FR-003**: The engine MUST solve by Gaussian elimination with **partial
  pivoting** (required because constraint/border rows have a zero diagonal).
- **FR-004**: The engine MUST detect a singular / ill-posed system using a
  **relative** singular-pivot threshold (scaled to the matrix magnitude) and report
  it via a boolean not-solved result — **never** by a silent conductance fallback.
- **FR-005**: The engine MUST expose read accessors for node voltage (by node id)
  and branch current (by branch index) that are total over valid indices and never
  throw.

**Netlist assembler (Layer 2 — the single element→matrix mapping site)**

- **FR-006**: The assembler MUST walk a `Netlist` and be the one authoritative place
  that maps each component onto engine stamps.
- **FR-007**: The assembler MUST stamp a resistor as a conductance and a current
  source as an injected current.
- **FR-008**: The assembler MUST stamp an ideal voltage source as an augmented
  branch (incidence + value), supporting **both grounded and floating** sources.
- **FR-009**: The assembler MUST stamp an ideal op-amp as a nullor border — a
  norator current into the output node's balance and a nullator constraint
  `V(in+) − V(in−) = 0`.
- **FR-010**: For reactive and nonlinear elements (capacitor, inductor, diode) the
  assembler MUST stamp a Norton companion `{Geq, Ieq}` **supplied by the caller**;
  it MUST NOT compute companions, hold reactive history, or embed any integration
  rule.
- **FR-011**: A single assemble + solve MUST be a pure function of (netlist, supplied
  companions): identical inputs produce identical outputs, with no residual state
  carried between solves.

**Two-phase assembly + error contract**

- **FR-012**: Assembly MUST be two-phase. A **plan phase**, run once per netlist,
  allocates branches, fixes the incidence topology, and validates; it MAY throw
  descriptive errors (branch-count overflow, out-of-range node, degenerate values)
  because it is off the hot path.
- **FR-013**: A **per-solve phase** MUST refresh conductance / RHS / companion
  *values* into the already-fixed structure and solve; it MUST be throw-free and
  heap-allocation-free and MUST NOT allocate branches.
- **FR-014**: Branch count MUST be treated as topological (one per ideal voltage
  source + one per op-amp; reactive/nonlinear companions add none) and therefore
  invariant across Newton iterations and timesteps for a given netlist.
- **FR-015**: The system MUST NOT provide any silent fallback or mock path: an
  unrepresentable request or an ill-posed system surfaces a descriptive error (plan
  phase) or the not-solved signal (solve phase), never a patched result.

**Placement, structure, and constraints**

- **FR-016**: The primitive MUST be a production primitive under
  `core/primitives/circuit/mna/`, in a dedicated sub-namespace (e.g. `acfx::mna`)
  to avoid the `acfx::detail` ODR hazard (TASK-11); the directory is created in the
  same change that inhabits it ("inhabit before creating").
- **FR-017**: The primitive MUST be header-only, template-sized, C++17, with no
  platform/hardware headers and no dependency pointing outward from the DSP core.
- **FR-018**: Each source file MUST stay within the ~300–500 line budget (the engine
  and the assembler split across separate headers).
- **FR-019**: The primitive MUST consume the existing frozen vocabulary in
  `core/primitives/circuit/` and MUST NOT introduce new component types.
- **FR-020**: v1 MUST operate on real-valued (`double`) systems, treat node 0 as the
  dropped ground reference, and treat inductors via a **caller-supplied Norton
  companion** (native branch-current inductor MNA is explicitly out of v1 scope).

**Validation**

- **FR-021**: The primitive MUST be validated by exact closed-forms (resistive
  divider; floating voltage source between two non-ground nodes; ideal
  inverting/non-inverting op-amp gain; fed-companion RC step matching the
  backward-Euler recurrence).
- **FR-022**: The primitive MUST be validated against physical invariants
  (passivity: dissipated energy ≤ source energy; monotonicity where expected;
  reciprocity / symmetry of the conductance block) and well-posedness behavior
  (singular topologies return not-solved, do not throw on the hot path, leak no
  NaN) — following the recorded circuit-model-validation approach rather than
  transcribed published values.
- **FR-023**: An equivalence-oracle test suite MUST reproduce `LinearSolver` and
  `NullorSolver` outputs to ~1e-12 on the topologies those solvers cover.
- **FR-024**: A test MUST assert (via `AllocationSentinel`) that the per-solve phase
  performs zero heap allocations and zero deallocations. Tests use doctest under
  `tests/core/`.

### Key Entities *(include if feature involves data)*

- **MnaSystem** *(Layer 1)*: the abstract augmented linear system — fixed-dimension
  matrix + RHS + solution over (nodes + branches); owns the stamp API and the
  pivoted solve; knows nothing of components.
- **MnaAssembler** *(Layer 2)*: the netlist→stamp mapper; the single authoritative
  element→matrix site; holds no reactive history.
- **Branch**: an augmented current-unknown introduced by a voltage source or op-amp;
  its count is topological and fixed at plan time.
- **Companion `{Geq, Ieq}`**: a caller-supplied Norton reduction of a reactive or
  nonlinear element; consumed, never produced, by MNA (the sibling-primitive seam).
- **Netlist / components**: the existing frozen vocabulary in
  `core/primitives/circuit/` (resistor, capacitor, inductor, voltage/current
  source, diode, op-amp), consumed as-is.

## Success Criteria *(mandatory)*

### Measurable Outcomes

- **SC-001**: On every exact-closed-form circuit (resistive divider, floating
  voltage source, ideal inverting and non-inverting op-amp, fed-companion RC step),
  the computed result matches the analytic value to floating-point precision
  (relative error ≤ 1e-12).
- **SC-002**: On every topology the existing `LinearSolver` and `NullorSolver`
  handle, MNA reproduces node voltages and branch currents to within 1e-12 —
  demonstrating a faithful superset ahead of any lab migration.
- **SC-003**: The per-solve path performs exactly **zero** heap allocations and zero
  deallocations, as measured by `AllocationSentinel`, and no exception escapes it.
- **SC-004**: Every ill-posed topology in the test suite returns the not-solved
  signal with no thrown exception on the solve path and no NaN in the readable
  solution.
- **SC-005**: Floating (non-grounded) ideal voltage sources are solved correctly —
  a capability the pre-existing labs refuse — with the imposed voltage difference
  and branch current exact.
- **SC-006**: Every over-capacity, out-of-range, or degenerate netlist is rejected
  with a descriptive error at plan/prepare time (never a silent truncation, never a
  hot-path failure).
- **SC-007**: Each shipped source file remains within the ~300–500 line budget and
  passes the repository portability check (`scripts/check-portability.sh`): C++17,
  header-only, no platform headers.

## Assumptions

- The primitive's consumers are calling code (the sibling `newton-iteration` and
  `implicit-integration` primitives, the lab solvers slated to migrate, and DSP
  developers), not end users; "success" is measured as correctness, RT-safety, and
  faithful-superset behavior.
- The two sibling primitives (`newton-iteration`, `implicit-integration`) own the
  nonlinear loop and the integration rule respectively and supply companions to
  MNA; this spec covers only the MNA linear heart plus its element stamping.
- The existing `core/primitives/circuit/` vocabulary is stable and reused as-is; no
  new component types are added here.
- Backward-Euler is the companion form the current caller/test harness supplies; MNA
  is agnostic to the integration rule and stamps whatever companion it is given.
- Lab migration onto `MnaSystem` (TASK-14) is captured **follow-on** scope,
  sequenced after the sibling primitives land, and is **not** implemented within
  this feature.
- The dense fixed-size solve is appropriate at the node/branch counts of the target
  analog stages; sparse/factorization-reuse strategies are not needed for v1.

## Open Questions

*(Captured, not resolved — a later scoping pass owns these. Recorded from the design
record; none blocks the v1 scope above.)*

- **Complex / AC scalar**: whether to generalize the engine on a scalar type
  (`double` vs `complex`) to subsume the existing AC solver (`solveAC`), or keep AC
  a separate concern. v1 is real-valued; the stamp API should stay scalar-agnostic
  where that is free.
- **Controlled sources** (VCVS/CCVS/VCCS/CCCS): not in today's vocabulary; the
  branch-augmentation framework generalizes to them. Future.
- **Canonical `DiodeSpec` home (TASK-12)**: whether this primitive's landing is the
  moment to promote `DiodeSpec` / `siliconSignalDiode()` into
  `circuit/models/diode.h`. Related but separable.
- **Sequencing vs. siblings**: whether MNA lands first (validated with a hand-written
  companion test harness) or in lockstep with `newton-iteration` /
  `implicit-integration`.
