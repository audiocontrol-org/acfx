> ‼ **acfx COMMANDMENTS — non-negotiable** ‼
> **1. COMMIT AND PUSH EARLY AND OFTEN** — version control is a distributed, journaled
> filesystem that safeguards your work, **NOT a sacred rite reserved for the blessed.**
> Small atomic commits, pushed promptly; never hoard unpushed work.
> **2. NO GIT HOOKS, EVER** — this repo uses zero git hooks; none exist, none get added.
> **3. DESCRIPTIVE NAMES, NEVER NUMERIC PREFIXES** — names carry information; fake sequence
> numbers (`001-`) imply false order and false precision (datestamps excepted).
> (acfx Constitution, Principles I–III — `.specify/memory/constitution.md`.)

# Feature Specification: Newton–Raphson iteration primitive

**Feature Branch**: `newton-iteration`

**Created**: 2026-07-07

**Status**: Draft

**Input**: Design record `docs/superpowers/specs/2026-07-07-newton-iteration-design.md`
(approved 2026-07-08); roadmap item `design:primitive/newton-iteration`, part of
`multi:feature/phase-numerical-solvers`, sibling to the shipped
`modified-nodal-analysis` primitive.

## Context

The shipped **MNA** primitive (`core/primitives/circuit/mna/`) solves a **linear**
circuit: given every element's contribution as a conductance, a branch constraint,
or a `Companion{Geq, Ieq}`, it assembles the augmented system and solves for node
voltages and branch currents. It is deliberately stateless and purely linear — it
**consumes** companions and never computes one.

A **diode is nonlinear** (Shockley law `I(vAK) = Is·(exp(vAK/nVt) − 1)`), so MNA
cannot solve a network containing one by itself. The standard resolution is
**Newton–Raphson**: repeatedly linearize each diode about the current voltage
iterate into a Norton `Companion`, re-solve the now-linear MNA system, and iterate
until the iterate stops moving. That nonlinear outer loop is this primitive.

The seam is already cut on both sides and left empty for exactly this:

- **`Diode` (`core/primitives/circuit/models/diode.h`)** owns physics only:
  `evaluate(vAK) → {current, conductance}` (the Shockley current and its analytic
  small-signal conductance `g = dI/dV`, the Jacobian Newton needs), and
  `limitJunctionVoltage(vNew, vOld)` (the SPICE `pnjlim` step limiter). Its header
  states: *"driving Newton is the solver's job (FR-006)."*
- **MNA's `MnaAssembler`** stamps every `Diode` from a **caller-supplied** Norton
  companion obtained through a `CompanionSupply` (any type exposing
  `Companion at(int componentIndex) const noexcept`), naming the supplier
  *"newton-iteration."* MNA never computes it.

Today the loop exists only as **lab scaffolding** — `OpAmpClipperSolver`
(`core/labs/opamp-stages/solver/opamp-clipper-solver.h`) and `TransientClipper`
(`core/labs/diode-clippers/solver/transient-clipper.h`) — which demonstrate the loop
shape but are explicitly *"non-normative, Phase-5-superseded scaffolding"* that
**refuse** ≥2 interacting nonlinearities at distinct node pairs. This primitive is
where that bounded charter is lifted onto the shared MNA core.

**Sibling boundary (three coordinated primitives):** MNA is the linear engine;
`implicit-integration` (planned) owns time-stepping and reactive companions;
`newton-iteration` owns the nonlinear outer loop. Newton **drives** MNA and
**composes with** implicit-integration's companions — it absorbs neither.

## User Scenarios & Testing *(mandatory)*

### User Story 1 - Solve a single-diode network to convergence (Priority: P1)

A caller has a netlist with one diode (e.g. a diode + series resistor + source) and
a planned `MnaAssembler`/`MnaSystem`. They call the Newton solver with an initial
node-voltage guess and get back the converged operating-point node voltages.

**Why this priority**: This is the minimal nonlinear solve — the MVP. Without it the
primitive does nothing; with it, MNA can solve its first nonlinear circuit.

**Independent Test**: Solve a single diode in series with a resistor driven by a DC
source; compare the converged node voltage against the exact operating point (Lambert-W
/ independently iterated fixed point) to a tight tolerance.

**Acceptance Scenarios**:

1. **Given** a diode+resistor+source netlist and a zero initial guess, **When** the
   solver runs, **Then** it returns `converged == true` within the iteration bound and
   node voltages matching the exact operating point to within the solve tolerance.
2. **Given** the same netlist driven at several DC levels across forward and reverse
   bias, **When** solved, **Then** every result matches the exact operating point and
   the transfer curve is monotonic.

---

### User Story 2 - Solve coupled multi-diode networks (Priority: P1)

A caller has a netlist with several diodes at distinct or shared node pairs
(antiparallel clipper pair, longer antiparallel string, bridge rectifier). The solver
linearizes **all** diodes together each iteration and converges the whole system —
without the lab's single-nonlinearity refusal.

**Why this priority**: This general multi-diode charter is the capability gain that
makes it a production primitive rather than a re-packaged lab. It is the reason the
primitive exists.

**Independent Test**: Solve a symmetric antiparallel diode pair across a resistor;
assert zero output at zero drive (symmetry), odd symmetry of the transfer curve, and —
against the lab clipper oracle on the shared topology — agreement to tolerance.

**Acceptance Scenarios**:

1. **Given** a symmetric antiparallel diode pair at zero drive, **When** solved,
   **Then** the port voltage is exactly zero (to solver tolerance) by symmetry.
2. **Given** a multi-diode network, **When** solved, **Then** all diodes are linearized
   and updated within a single global Newton step per iteration and the system
   converges jointly (no per-diode sequencing artifact).
3. **Given** a network with two interacting nonlinearities at distinct node pairs,
   **When** solved, **Then** the solver does **not** refuse it (unlike the lab) and
   converges or reports non-convergence honestly.

---

### User Story 3 - Compose with a caller-supplied base companion supply (Priority: P1)

A caller (ultimately `implicit-integration`) supplies a **base** `CompanionSupply`
holding the reactive elements' companions, fixed for this solve. Newton wraps it:
diode component indices get Newton's per-iteration linearization; every other index is
delegated to the base supply untouched.

**Why this priority**: This composition is how the three-primitive boundary stays
intact — MNA sees one `CompanionSupply`, reactive history stays with the caller, and
Newton adds only the nonlinear layer. Required for any circuit that mixes reactive and
nonlinear elements.

**Independent Test**: With a hand-written base supply returning a fixed companion for a
reactive slot and Newton overriding the diode slots, solve and confirm the reactive
companion is passed through unchanged every iteration while the diode companions update.

**Acceptance Scenarios**:

1. **Given** a base supply for the non-diode elements, **When** Newton composes and
   solves, **Then** `at(i)` returns Newton's diode linearization for diode indices and
   the base supply's companion for all other indices.
2. **Given** an empty base supply (no reactive elements — the v1 DC case), **When**
   solved, **Then** only diode indices are populated and the solve proceeds.

---

### User Story 4 - Stateless per solve with caller-owned warm start (Priority: P1)

The caller owns the initial node-voltage guess (typically the previous converged
sample) and passes it into each `solve()`. The solver holds no cross-sample history;
solving the same inputs twice yields identical results.

**Why this priority**: Statelessness mirrors MNA and keeps time-stepping ownership with
`implicit-integration`. It makes the primitive a pure function of its inputs and
independently testable.

**Independent Test**: Call `solve()` twice with identical (netlist, base supply, guess);
assert byte-identical results. Then vary only the initial guess and confirm convergence
to the same operating point (guess affects iteration count, not the fixed point).

**Acceptance Scenarios**:

1. **Given** identical inputs, **When** `solve()` is called repeatedly, **Then** the
   results are identical and no state persists between calls.
2. **Given** a warm-start guess near the solution vs. a cold zero guess, **When** solved,
   **Then** both converge to the same operating point; the warm start converges in no
   more iterations.
3. **Given** the initial guess, **When** it is supplied, **Then** it is the full
   node-voltage array (node voltages only — branch currents are not part of the guess).

---

### User Story 5 - Two-phase: plan once, RT-safe hot-path solve (Priority: P1)

The caller plans once per netlist (topology fixed, validation permitted to throw off the
hot path, delegating to `MnaAssembler::plan`), then calls `solve()` repeatedly on the
audio hot path with **zero** heap allocation and no locks.

**Why this priority**: Real-time safety (Constitution Principle VI) is non-negotiable for
a primitive intended for the audio path. The plan/solve split is how topology validation
and allocation stay off the per-sample path.

**Independent Test**: Plan once, then run many `solve()` calls under an `AllocationSentinel`
asserting zero heap allocations and confirming no re-planning occurs.

**Acceptance Scenarios**:

1. **Given** a planned solver, **When** `solve()` runs on the hot path, **Then** it
   performs zero heap allocations and does not call the throwing plan/allocate path.
2. **Given** an unplanned solver, **When** `solve()` is called, **Then** it is a
   precondition violation surfaced deterministically (not undefined behavior).

---

### User Story 6 - Report non-convergence and singular systems without fallback (Priority: P2)

When Newton does not converge within the iteration bound, or a linearized system is
singular, the solver returns a status marking the failure and the last iterate — never a
fabricated, gmin-patched, or substituted output.

**Why this priority**: No-fallback honesty (Constitution Principle V) is a hard project
rule. A masked non-convergence is a silent bug factory; a surfaced one is actionable.

**Independent Test**: Drive a network past the iteration bound (tight tolerance, low bound)
and assert `converged == false` with the last iterate returned and no state corruption;
feed a structurally singular linearization and assert it is reported, not patched.

**Acceptance Scenarios**:

1. **Given** a solve that cannot converge in the iteration bound, **When** it returns,
   **Then** `converged == false`, `iterations` equals the bound, the residuals are
   reported, and the returned voltages are the last iterate (documented as not physical).
2. **Given** a singular linearized system mid-loop, **When** `MnaSystem::solve()` returns
   false, **Then** the solver surfaces the failure by value (no throw on the hot path) and
   applies no gmin/source-step/substituted output.
3. **Given** a non-converged solve, **When** it returns, **Then** no solver state is
   mutated that could contaminate a subsequent solve (statelessness holds under failure).

---

### User Story 7 - Reproduce the existing lab solvers (equivalence oracle) (Priority: P2)

On the TS808 diode-clipper core topology that the lab `OpAmpClipperSolver` /
`TransientClipper` already solve, the primitive produces equivalent node voltages within
tolerance.

**Why this priority**: The labs are the trusted reference for these topologies; matching
them proves the primitive is correct on real circuits and de-risks the eventual lab
migration (TASK-14). Priority P2 because it validates rather than delivers new capability.

**Independent Test**: Run the primitive and the lab solver on the same clipper netlist at
matched operating points; assert node-voltage agreement to tolerance.

**Acceptance Scenarios**:

1. **Given** a diode-clipper topology solvable by the lab, **When** both solve at matched
   inputs, **Then** their converged node voltages agree to within the solve tolerance.

---

### Edge Cases

- **Zero diodes in the netlist**: the solve reduces to a single linear MNA solve; the
  solver must return the exact linear result in one iteration (no spurious iteration).
- **Zero drive on a symmetric network**: the operating point is exactly the symmetric
  one (e.g. 0 V across an antiparallel pair) to solver tolerance.
- **Extreme forward bias / stiff exponential**: `pnjlim` damping must keep the exp
  argument from exploding between iterations without moving the fixed point; if the
  network is too stiff to converge in the bound, that is reported (US6), not masked.
- **Reverse bias**: current approaches `−Is`; the current residual may remain above any
  fixed `currentTol` at a fully settled voltage — this must NOT reject a
  voltage-converged solve (current residual is reported, not gated).
- **Initial guess far from the solution**: increases iteration count but must converge to
  the same fixed point or report non-convergence; it must not change the answer.
- **Singular linear system for a valid nonlinear problem** (e.g. a floating subnetwork):
  reported via `MnaSystem::solve()` returning false, surfaced by value.

## Requirements *(mandatory)*

### Functional Requirements

- **FR-001**: The primitive MUST implement a Newton–Raphson outer loop that, given a
  netlist, a planned `MnaAssembler` + `MnaSystem`, a base `CompanionSupply`, and an initial
  node-voltage guess, converges the circuit's nonlinear diode elements and returns the
  operating-point node voltages plus a status.
- **FR-002**: Each iteration MUST, for every diode, read `vAK = V(anode) − V(cathode)` from
  the current node-voltage iterate, obtain `{I, g}` from `Diode::evaluate(vAK)`, and form the
  Norton companion `Companion{Geq: g, Ieq: I − g·vAK}`.
- **FR-003**: The primitive MUST linearize and update **all** diodes together within a single
  global Newton step per iteration (coupled multi-diode solve), NOT one diode at a time.
- **FR-004**: The primitive MUST NOT refuse a netlist for containing ≥2 interacting
  nonlinearities at distinct node pairs (the lab's charter restriction is lifted).
- **FR-005**: v1 MUST support the `Diode` nonlinearity (the only nonlinear element in the
  frozen vocabulary). The per-element "linearize + limit" interface MUST be shaped so a future
  nonlinear element can be added without reshaping the Newton loop.
- **FR-006**: The primitive MUST compose companions by wrapping a caller-supplied **base**
  `CompanionSupply`: `at(i)` returns Newton's per-iteration diode linearization for diode
  component indices and delegates to the base supply for all other indices, preserving MNA's
  single-supply `Companion at(int) const noexcept` contract.
- **FR-007**: The base companion supply's contents (reactive-element companions) MUST be held
  fixed for the duration of one `solve()` — Newton re-linearizes only diodes across iterations.
- **FR-008**: The primitive MUST be **stateless per solve**: `solve()` takes the initial
  node-voltage guess (caller-owned warm start) and returns a status; it retains no cross-sample
  history. Solving identical inputs MUST yield identical results.
- **FR-009**: The initial guess MUST be the **full node-voltage array** (node voltages only,
  not branch currents). Branch currents are pure linear outputs of each solve given the
  companions and MUST NOT be part of the guess.
- **FR-010**: The primitive MUST damp each junction between iterations via
  `Diode::limitJunctionVoltage` (`pnjlim`); the previous iterate `vOld` is internal to one
  `solve()` call (not cross-sample state).
- **FR-011**: Convergence MUST be gated on the **voltage residual** `max|Δv| < voltageTol`
  ONLY. The current residual MUST be **reported but never gated** (a diode's reverse-saturation
  current can exceed any fixed current tolerance at a fully settled voltage).
- **FR-012**: The iteration count MUST be bounded by a configurable `maxIterations`.
  `maxIterations`, `voltageTol`, and `currentTol` (the current-residual reporting reference)
  MUST be validated `> 0` at construction (throw on invalid), and MUST NEVER be silently
  retuned to hide a non-converging case.
- **FR-013**: On non-convergence within the bound, `solve()` MUST return a status with
  `converged == false`, the consumed `iterations`, the final `voltageResidual` and
  `currentResidual`, and the last iterate as the node voltages (documented as not physically
  trustworthy) — never a substituted or fabricated output.
- **FR-014**: A singular / ill-posed linearized system MUST be surfaced via
  `MnaSystem::solve()` returning false and reported by value on the hot path — NEVER a throw on
  the hot path, and NEVER a silent gmin, source-stepping, or substituted-conductance fallback.
- **FR-015**: The result MUST be a status value `NewtonStatus { bool converged; int iterations;
  double voltageResidual; double currentResidual; }`.
- **FR-016**: Assembly MUST be two-phase, mirroring MNA: a **plan phase** run once per netlist
  off the hot path (throw-permitted; delegates to `MnaAssembler::plan`, does NOT re-plan on each
  solve), and a **hot-path `solve()`** that refreshes values and iterates without allocation.
- **FR-017**: The `solve()` hot path MUST perform **zero** heap allocation and take no locks
  (Constitution Principle VI). All working storage MUST be fixed-capacity, sized by template
  parameters.
- **FR-018**: The primitive MUST be a production primitive under
  `core/primitives/circuit/newton/`, namespace `acfx::newton`, sibling to `acfx::mna`, exposing
  `NewtonSolver<int MaxNodes, int MaxComponents, int MaxBranches>`. The folder MUST be created in
  the same commit as the primitive.
- **FR-019**: The primitive MUST be header-only, template-sized, C++17, with no platform
  headers, and each source file MUST stay within the ~300–500 line budget.
- **FR-020**: The primitive MUST consume the existing frozen vocabulary
  (`core/primitives/circuit/`) and the shipped MNA primitive; it MUST add no new component types.
- **FR-021**: The primitive MUST be validated by **exact closed forms** where a diode network
  has an analytic operating point (single diode + series resistor via Lambert-W / independently
  iterated fixed point; symmetric antiparallel pair → zero at zero drive).
- **FR-022**: The primitive MUST be validated against **physical invariants**: monotonic transfer
  curve, passivity, antiparallel symmetry (odd transfer), and `I(0) = 0`.
- **FR-023**: An **equivalence-oracle** test suite MUST reproduce the lab `OpAmpClipperSolver` /
  `TransientClipper` node voltages on the TS808 diode-clipper core to within the solve tolerance.
  Validation MUST NOT rely on transcribed published values.
- **FR-024**: A test MUST assert (via `AllocationSentinel`) that the `solve()` hot path performs
  zero heap allocations across many solves.

### Key Entities *(include if feature involves data)*

- **NewtonSolver**: the nonlinear outer-loop driver over a planned `MnaAssembler` + `MnaSystem`.
  Template-sized (`MaxNodes`, `MaxComponents`, `MaxBranches`); holds convergence configuration
  (`maxIterations`, `voltageTol`) and fixed-capacity working storage; stateless per solve.
- **NewtonStatus**: the per-solve report — `converged`, `iterations`, `voltageResidual`,
  `currentResidual`. `converged == false` is a legitimate, surfaced outcome.
- **Composed CompanionSupply**: the wrapper exposing `Companion at(int) const noexcept` that
  returns Newton's per-iteration diode linearization for diode indices and delegates to the base
  supply otherwise. Consumed by `MnaAssembler::refresh`.
- **Diode linearization (Norton companion)**: `{Geq: g, Ieq: I − g·vAK}` from `Diode::evaluate`,
  the per-iteration stamp for one diode.

## Success Criteria *(mandatory)*

### Measurable Outcomes

- **SC-001**: On every exact-closed-form circuit in the test suite (single diode + resistor at
  multiple DC levels; symmetric antiparallel pair at zero drive), the converged node voltages
  match the analytic operating point to within the solve tolerance.
- **SC-002**: On the TS808 diode-clipper core topology, the primitive's converged node voltages
  agree with the lab `OpAmpClipperSolver` / `TransientClipper` to within the solve tolerance.
- **SC-003**: The `solve()` hot path performs exactly **zero** heap allocations and takes no
  locks across a large batch of solves (asserted by `AllocationSentinel`).
- **SC-004**: Every non-converging case in the test suite returns `converged == false` with the
  last iterate and reported residuals — with **zero** fabricated, gmin-patched, or substituted
  outputs, and no state corruption.
- **SC-005**: A coupled network with ≥2 interacting nonlinearities at distinct node pairs is
  solved (or honestly reported non-converged) — never refused — demonstrating the capability gain
  over the lab.
- **SC-006**: Every symmetric antiparallel network produces an odd (symmetric) transfer curve and
  exactly zero output at zero drive (to solver tolerance); every diode network's transfer curve is
  monotonic and `I(0) = 0`.
- **SC-007**: Solving identical inputs twice yields identical results (statelessness), and varying
  only the initial guess converges to the same operating point.
- **SC-008**: Each shipped source file remains within the ~300–500 line budget and introduces no
  new component types.

## Assumptions

- The shipped MNA primitive (`MnaSystem`, `MnaAssembler`, its two-phase plan/refresh/solve and
  `CompanionSupply` seam) is the linear engine Newton drives, unchanged. Any gap discovered in
  MNA is surfaced (per the MNA-residual backlog TASK-15/16/17), not worked around here.
- `Diode` owns the physics Newton consumes (`evaluate`, `vCrit`, `limitJunctionVoltage`); Newton
  computes no diode physics of its own.
- v1 is real-valued (`double`) and DC/transient — Newton stays real even if MNA later generalizes
  to a complex scalar.
- v1 lands ahead of `implicit-integration` and is validated with a **hand-written base companion
  supply** (DC / resistive-diode networks) plus the lab oracle, mirroring how MNA landed ahead of
  its consumers. (Sequencing is revisitable — see Open Questions.)
- Node 0 is ground / the dropped reference, consistent with MNA.

## Open Questions

1. **Damping generality beyond the diode** — `pnjlim` is diode-specific and lives on `Diode`. A
   future non-diode nonlinearity needs its own limiter obtained through the generic per-element
   interface. v1 is diode-only, so Newton calls `Diode::limitJunctionVoltage` directly; the
   generalization is a design note, not v1 surface.
2. **Sequencing vs. `implicit-integration`** — land Newton first (hand-written fixed reactive base
   supply + lab oracle) vs. in lockstep. Leaning land-first (see Assumptions).
3. **Multi-diode convergence robustness without gmin** — whether bounded `pnjlim`-damped Newton
   converges for stiff antiparallel strings at large drive. If not, the answer is an
   **explicitly-reported** convergence aid (never a silent fallback), captured for a later pass —
   not a v1 gmin path.
4. **Lab migration (TASK-14)** — retiring the hand-rolled Newton + linear algebra in the labs onto
   `NewtonSolver` + `MnaSystem` is follow-on, likely once all three sibling primitives land; out of
   scope for v1.
