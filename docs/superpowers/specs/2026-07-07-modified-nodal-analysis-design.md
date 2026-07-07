# Modified Nodal Analysis (MNA) primitive — design

- Roadmap item: `design:primitive/modified-nodal-analysis`
- Part of: `multi:feature/phase-numerical-solvers`
- Siblings (planned): `design:primitive/newton-iteration`, `design:primitive/implicit-integration`
- Date: 2026-07-07

## stack-control design house rules (stack-control-design-v1)

- **capture-over-yagni** (mechanical): capture everything known or knowably-implied; scoping is a separate, operator-driven pass AFTER capture.
- **solution-space-alternatives** (mechanical): the solution space enumerates ≥2 alternatives, including the rejected ones with reasons.
- **required-sections** (mechanical): problem-domain, solution-space, decisions, open-questions, provenance.
- **operator-approval** (operator): not done until the operator records the `design-approved:` marker on the roadmap node.
- **handoff-to-spec-kit** (soft): terminal handoff routes to `/stack-control:define`, never `writing-plans`.
- **installation-anchored-record** (mechanical): this record lives under `docs/superpowers/specs/`.

## Problem domain

The acfx circuit-modeling core needs a general **Modified Nodal Analysis** engine:
the linear heart that, given a circuit's elements at a fixed operating point,
assembles the augmented system `A·x = z` and solves it for node voltages and
branch currents.

Today no general MNA engine exists. What exists is:

- A **frozen, RT-safe component vocabulary** in `core/primitives/circuit/`
  (`Netlist`, `Resistor`/`Capacitor`/`Inductor`/`Diode`/`OpAmp`/`VoltageSource`/
  `CurrentSource`, and a shared backward-Euler `Companion{Geq, Ieq}` return type).
  All components are pure, stateless value types; history is owned by the solver.
- **Four hand-rolled lab solvers** that each re-derive Gaussian elimination with
  partial pivoting + fixed-node reduction + companion stamping:
  - `LinearSolver` (`core/labs/component-abstractions/solver/linear-solver.h`) —
    reduced nodal analysis, conductance-only, grounded voltage sources imposed by
    fixed-node reduction; floating sources **throw**; diodes skipped.
  - `NullorSolver` + `AugmentedSolver`
    (`core/labs/opamp-stages/solver/opamp-stage-solver.h`, `augmented-solve.h`) —
    the only genuine MNA-style augmentation: borders the nodal system with one
    row+col per op-amp (nullor stamp `[G B; C 0][v; j] = [i; 0]`), partial
    pivoting load-bearing because the `[C, 0]` block has a zero diagonal.
  - `TransientClipper` / `OpAmpClipperSolver` — separated timestep/Newton loops
    layered over the above.
  - `solveAC` (`core/labs/passive-tone-stacks/solver/ac-solver.h`) — complex
    phasor nodal analysis; explicitly not MNA.

Consequences that motivate the primitive:

- **Duplication (backlog TASK-14):** `NullorSolver` re-implements ~200 lines of
  `LinearSolver`'s Gaussian-elimination + fixed-node reduction + companion
  stamping; only bordering is new. The backlog explicitly asks for the shared
  linear-algebra core to be factored out. A general MNA engine is its natural home.
- **Latent defects the primitive should not inherit:** `NullorSolver::advanceHistory()`
  is `noexcept` yet calls a throwing `voltage()` (TASK-14, `std::terminate` risk);
  `OpAmpClipperSolver::buildAugmented` recomputes fixed companions every inner
  Newton iteration (TASK-13, wasteful).
- **A real capability gap:** the labs refuse floating (non-grounded) ideal voltage
  sources. True MNA handles them via branch-current augmentation.
- **Sibling boundaries:** MNA is one of three coordinated planned primitives.
  `newton-iteration` is the nonlinear outer loop; `implicit-integration` is the
  time-stepping / companion-model layer. MNA must be the linear engine both call,
  not absorb them.

RT-safety and portability bar (Constitution Principle VI, `core/primitives/README.md`):
header-only, template-sized, **no heap allocation or locks in the solve path**,
C++17, no platform headers, files within ~300–500 lines.

## Solution space

The numerics are common to every alternative: a dense, fixed-size matrix
(`std::array`, dimension fixed at instantiation), Gaussian elimination with
**partial pivoting** (mandatory — the constraint/border block has a zero
diagonal), and a **relative** singular-pivot threshold (`≈1e-12 · matScale`,
adopting `AugmentedSolver`'s gate over `LinearSolver`'s absolute `1e-300` floor).
Ground is node 0, the dropped reference. The alternatives differ in **where the
seam between "solve the matrix" and "map elements onto the matrix" sits.**

### Chosen — Two-layer stamp primitive (Approach A)

Two header-only types under `core/primitives/circuit/mna/`, namespace `acfx::mna`:

- **Layer 1 — `MnaSystem<int MaxNodes, int MaxBranches>`**: the reusable
  augmented linear engine, knowing nothing about components. Dimension
  `Dim = MaxNodes + MaxBranches`, fixed at instantiation; storage
  `std::array<std::array<double, Dim>, Dim>` + RHS/solution arrays. Abstract stamp
  API, all O(1) and callable in the solve path:
  - `reset()`
  - `stampConductance(NodeId i, NodeId j, double g)` — four-corner, ground-aware
  - `stampRhsCurrent(NodeId n, double i)`
  - `int addBranch()` — allocate a branch-current unknown (throws at build time if `> MaxBranches`)
  - `stampBranchIncidence(int k, NodeId p, NodeId n)` — the ±1 B/C coupling
  - `stampBranchValue(int k, double rhs)` — branch constraint RHS
  - `stampBranchResistance(int k, double r)` — optional branch-block diagonal
  - `bool solve()` — Gaussian elimination with partial pivoting; returns false on singular
  - `double nodeVoltage(NodeId)` / `double branchCurrent(int k)`

  This is deliberately the superset of `LinearSolver` (nodal) and `AugmentedSolver`
  (nodal + border) — the unification TASK-14 asks for.

- **Layer 2 — `MnaAssembler<int MaxNodes, int MaxComponents, …>`**: walks a
  `Netlist` and stamps each component onto an `MnaSystem` — the single
  authoritative place for the element→matrix mapping:
  - `Resistor` → `stampConductance`
  - `CurrentSource` → `stampRhsCurrent`
  - `VoltageSource` → `addBranch` + incidence + value (**floating supported**)
  - `OpAmp` (nullor) → border (norator current into `out` KCL; nullator constraint `V(in+) − V(in−) = 0`)
  - `Capacitor`/`Inductor`/`Diode` → stamp a `Companion{Geq, Ieq}` **supplied by the
    caller**, never self-computed. This is the seam to `implicit-integration`
    (reactive companions) and `newton-iteration` (nonlinear linearizations).

  Because the assembler only consumes companions, MNA holds **no history state and
  no integration rule** — each `assemble + solve` is a pure function of (netlist,
  supplied companions), which makes it independently testable and lets Newton
  re-solve cheaply.

Chosen because it yields both a reusable abstract core (Layer 1 = the TASK-14
target) AND one authoritative element-mapping site (Layer 2), while preserving the
three-primitive boundary exactly. Cost: two types instead of one.

### Rejected — Single class over a pre-linearized netlist (Approach B)

One type that accepts a netlist already reduced to conductances/companions/branch
rows and solves it. Rejected: the element→matrix mapping and the "Modified"
augmentation logic get smeared across every caller — re-duplicating TASK-14's
target rather than eliminating it.

### Rejected — Abstract stamp-only core, no netlist knowledge (Approach C)

Purely Layer 1: stamp conductance / branch / rhs / solve, no `Netlist` awareness.
Rejected as the *whole* primitive — every caller re-writes the element walk and
re-decides which elements need branch augmentation. **Adopted as Layer 1 inside
Approach A**, so its reusability survives without exporting the duplication.

### Rejected — Full transient nonlinear engine (monolith)

MNA owns element stamping, companion integration, AND the Newton loop; the
siblings become thin config. Rejected: it collapses the deliberate three-primitive
decomposition the roadmap drew and re-creates the tangled, hard-to-test structure
the labs already demonstrate.

## Decisions

1. **Scope:** MNA owns element stamping + augmented-system assembly + the linear
   solve. It does **not** own the integration rule, reactive history, the Newton
   loop, or convergence — those are `implicit-integration` and `newton-iteration`,
   which feed MNA `Companion{Geq, Ieq}` stamps each timestep/iteration.
2. **Structure:** two layers — `MnaSystem` (abstract engine) + `MnaAssembler`
   (netlist mapping) — Approach A.
3. **Placement:** a **production primitive** under `core/primitives/circuit/mna/`
   (adjacent to the vocabulary it consumes), namespace `acfx::mna`, not a lab. Its
   purpose is to be the shared core the labs graduate onto. Folder created in the
   same commit as the primitive ("inhabit before creating").
4. **Numerics:** dense fixed-size `std::array`; Gaussian elimination with partial
   pivoting; relative singular-pivot threshold. Real-valued (`double`) in v1.
5. **RT-safety:** header-only, template-sized, zero heap in `assemble`+`solve`,
   C++17, no platform headers, ≤~300–500 lines/file (split across the two headers).
6. **Two-phase assembly + error contract (no fallbacks):** branch count is
   topological (one branch per ideal voltage source + one per op-amp/nullor;
   diodes/capacitors/inductors are companion stamps and add no branches — invariant
   across Newton iterations and timesteps), so assembly splits into:
   - a **plan phase** run once per netlist at `prepare()` that allocates branches
     (`addBranch`), fixes the incidence topology, and validates — **throw-permitted,
     off the hot path** (branch overflow, out-of-range node, degenerate values);
   - a **per-solve phase** on the hot path that refreshes conductance/RHS/companion
     *values* into the already-fixed structure — **throw-free and alloc-free**, never
     calling `addBranch`.

   `solve()` returns `bool` for singular/ill-posed systems — **no silent gmin
   fallback**. Read accessors are total over valid indices and never throw (avoiding
   the `NullorSolver` `noexcept`/throw latent bug, TASK-14). This is strictly stronger
   than "assembly may fail before solve": the per-sample path never invokes the
   throwing allocator at all, and topology is not re-derived every iteration
   (sidestepping TASK-13's per-iteration recompute waste by construction).
7. **Capability gain:** floating ideal voltage sources are supported via branch
   augmentation, removing the labs' grounded-only restriction.
8. **Validation:** exact closed-forms + an equivalence oracle against the existing
   labs + physical invariants + well-posedness checks, per the recorded
   circuit-model-validation approach — not transcribed published values.
   `AllocationSentinel` asserts zero heap in the solve path.
9. **Lab migration (TASK-14) is follow-on, not v1** of this primitive: retiring the
   hand-rolled linear algebra in `LinearSolver`/`NullorSolver`/clipper solvers onto
   `MnaSystem` is captured scope, likely sequenced after all three sibling
   primitives land so the labs migrate to the full trio at once.
10. **Inductor treatment in v1: supplied Norton companion** (as the labs do today),
    not native branch-current inductor MNA. This keeps MNA stateless and aligned with
    `implicit-integration` (which owns the companion). Native branch-current inductor
    augmentation remains a documented future option, not v1. (Resolves the former
    open question on inductor treatment, per third-party review.)

## Open questions

> Resolved by third-party review (2026-07-07): **inductor treatment** — v1 uses the
> supplied Norton companion (see Decision 10). No longer open.

1. **Complex / AC scalar** — generalize `MnaSystem` on `Scalar ∈ {double,
   complex<double>}` to subsume `solveAC`, or keep AC a separate concern. Design the
   stamp API scalar-agnostic where free; default v1 real.
2. **Controlled sources (VCVS/CCVS/VCCS/CCCS)** — not in today's vocabulary; the
   branch-augmentation framework generalizes to them. Future.
3. **Canonical `DiodeSpec` home (TASK-12)** — whether this primitive's landing is
   the moment to promote `DiodeSpec`/`siliconSignalDiode()` into
   `circuit/models/diode.h`. Related but separable.
4. **Sequencing vs. siblings** — whether MNA lands first (with a hand-written test
   harness supplying companions) or in lockstep with `newton-iteration` /
   `implicit-integration`.

## Provenance

- Design conducted in-session via `superpowers:brainstorming`, driven by the
  `/stack-control:design` frontend (house rules `stack-control-design-v1` injected
  above).
- Compass verdict `on-course` for intent `design` from phase `planned`; `design:`
  pointer set on the roadmap node before authoring.
- Existing-code map produced by an `Explore` sub-agent over the repo at commit
  `ab187e6`; key references: `core/primitives/circuit/` (vocabulary),
  `core/labs/component-abstractions/solver/linear-solver.h`,
  `core/labs/opamp-stages/solver/{opamp-stage-solver.h, augmented-solve.h}`,
  `core/labs/passive-tone-stacks/solver/ac-solver.h`, tests under `tests/core/`.
- Backlog inputs: TASK-14 (unify duplicated linear algebra; `noexcept`/throw bug),
  TASK-13 (per-iteration companion recompute), TASK-12 (`DiodeSpec` ODR),
  TASK-11 (`acfx::detail` helper ODR).
- Recorded memory: `circuit-model-validation-approach` (prove the solver exact on
  closed forms, validate assembled circuits via exact limits + monotonic invariants
  + passivity).
