# Modified Nodal Analysis (MNA) — Production Primitive

The `core/primitives/circuit/mna/` primitive is the unified linear-system engine
for circuit modeling. It implements **two layers**: an abstract augmented linear
solver (`MnaSystem`) that knows nothing of components, and a netlist mapper
(`MnaAssembler`) that is the single authoritative site for mapping circuit elements
onto the matrix. It consumes the frozen component vocabulary in
`core/primitives/circuit/` (resistor, capacitor, inductor, diode, op-amp, voltage
source, current source, and the `Netlist`), adds **no new component types**, and
is designed for real-time safety and decomposability with its sibling primitives
`newton-iteration` and `implicit-integration`.

## Architecture — Two Layers

### Layer 1: `MnaSystem<MaxNodes, MaxBranches>` (namespace `acfx::mna`)

The abstract augmented linear engine: a fixed-dimension matrix over node voltages
and branch currents, with a stamp API and a pivoted Gaussian solve. It knows
nothing of resistors or op-amps — only:

- `stampConductance(i, j, g)` — four-corner, ground-aware
- `stampRhsCurrent(node, i)` — current injection into a KCL balance
- `int addBranch()` — allocate a branch-current unknown (topological, once per
  voltage source or op-amp)
- `stampBranchIncidence(branch, p, n)` — the ±1 coupling to node voltages
- `stampBranchValue(branch, rhs)` — branch constraint RHS
- `bool solve()` — Gaussian elimination with **partial pivoting** (mandatory for
  zero-diagonal constraint rows); returns false on singular systems, never throws
  or injects silent fallback conductance
- `nodeVoltage(n)` / `branchCurrent(k)` — read accessors, total over valid indices

This is the reusable core that will unify the hand-rolled Gaussian-elimination
duplication across the existing lab solvers (backlog TASK-14).

### Layer 2: `MnaAssembler<MaxNodes, MaxComponents, MaxBranches>` (namespace `acfx::mna`)

The netlist mapper: walks a `Netlist` and stamps each component onto an `MnaSystem`:

- `Resistor` → `stampConductance`
- `CurrentSource` → `stampRhsCurrent`
- `VoltageSource` → `addBranch` + incidence + value (**floating sources supported**
  — a capability gain over the existing labs)
- `OpAmp` (nullor) → border (norator current + nullator constraint `V(+) − V(−) = 0`)
- `Capacitor` / `Inductor` / `Diode` → stamp a `Companion{Geq, Ieq}` **supplied by
  the caller**, never self-computed

Because the assembler consumes companions and computes nothing, **each `assemble +
solve` is a pure function of the (netlist, supplied companions)**. MNA holds no
reactive history and no integration rule. This is the seam to the sibling primitives:
`implicit-integration` supplies reactive companions; `newton-iteration` supplies
nonlinear linearizations.

## Real-Time Safety — Two-Phase Assembly

The primitive is split into **two phases with a strict error contract**:

- **Plan phase** (`MnaAssembler::plan()`) — runs once per netlist, off the hot path:
  allocates branches, fixes the incidence topology, and validates. **May throw**
  descriptive errors for branch-count overflow, out-of-range node IDs, or degenerate
  element values.

- **Per-solve phase** (`MnaAssembler::refresh()` + `MnaSystem::solve()`) — runs hot,
  per sample and per Newton iteration: refreshes conductance/RHS/companion *values*
  into the already-fixed structure and solves. **Throws-free and heap-allocation-free**.
  The branch count
  is topological (one per voltage source + one per op-amp; companions add none) and
  therefore invariant across Newton iterations and timesteps.

`solve()` returns `bool` for singular or ill-posed systems — **no silent gmin
fallback, no NaN leakage**. Read accessors are total and never throw. This is
strictly stronger than "some phases may fail": the per-sample path never invokes
the throwing allocator at all and topology is not re-derived every iteration.

## Scope — What's Intentionally Not Here

- **Integration rule and reactive history** — owned by `implicit-integration`
  (backward-Euler, RK, etc.). MNA is agnostic to the rule; it stamps whatever
  companion it is given.
- **Nonlinear outer loop and convergence** — owned by `newton-iteration`. MNA solves
  a linear system; Newton feeds it linearized companions at each iteration.
- **Lab migration (TASK-14)** — retiring the duplicated Gaussian-elimination code in
  `LinearSolver` and `NullorSolver` onto `MnaSystem` is captured follow-on scope,
  sequenced after the sibling primitives land. Not part of this feature.
- **Complex / AC analysis** — v1 is real-valued (`double`). Generalization to
  complex scalars is a documented future direction.
- **Native inductor branch-current MNA** — v1 uses caller-supplied Norton companions
  (as the existing labs do). Native branch-current inductor augmentation is a
  documented future option.

## Design Record & Spec

The design-phase decisions and open questions are recorded at
`docs/superpowers/specs/2026-07-07-modified-nodal-analysis-design.md`.
The functional requirements and test acceptance criteria are at
`specs/modified-nodal-analysis/spec.md`. The per-layer API contracts are at
`specs/modified-nodal-analysis/contracts/mna-system.md` and
`specs/modified-nodal-analysis/contracts/mna-assembler.md`; the plan, research
decisions, and validation guide are alongside them in
`specs/modified-nodal-analysis/`.

Host-side validation lives in `tests/core/mna-*-test.cpp` (engine, assembler,
companions, RT-safety, invariants, and a lab-equivalence oracle proving parity
with `LinearSolver` and `NullorSolver`).
