> ‼ **acfx COMMANDMENTS — non-negotiable** ‼
> **1. COMMIT AND PUSH EARLY AND OFTEN** — version control is a distributed, journaled
> filesystem that safeguards your work, **NOT a sacred rite reserved for the blessed.**
> Small atomic commits, pushed promptly; never hoard unpushed work.
> **2. NO GIT HOOKS, EVER** — this repo uses zero git hooks; none exist, none get added.
> **3. DESCRIPTIVE NAMES, NEVER NUMERIC PREFIXES** — names carry information; fake sequence
> numbers (`001-`) imply false order and false precision (datestamps excepted).
> (acfx Constitution, Principles I–III — `.specify/memory/constitution.md`.)

# Feature Specification: Implicit-integration primitive

**Feature Branch**: `implicit-integration`

**Created**: 2026-07-08

**Status**: Draft

**Input**: Design record `docs/superpowers/specs/2026-07-08-implicit-integration-design.md`
(approved 2026-07-08, third-party-reviewed); roadmap item
`design:primitive/implicit-integration`, part of `multi:feature/phase-numerical-solvers`,
sibling to the shipped `modified-nodal-analysis` and `newton-iteration` primitives.

## Context

The circuit-modeling solver is deliberately decomposed into **three coordinated
primitives**, and this is the last of the trio:

- The shipped **MNA** primitive (`core/primitives/circuit/mna/`) is the **linear**
  engine: given every element's contribution as a conductance, a branch constraint,
  or a `Companion{Geq, Ieq}`, it assembles the augmented system and solves for node
  voltages and branch currents. It is stateless and purely linear — it **consumes**
  companions, holds **no reactive history and no integration rule**
  (`mna/README.md`: *"Integration rule and reactive history — owned by
  `implicit-integration`"*).
- The shipped **newton-iteration** primitive (`core/primitives/circuit/newton/`) is
  the **nonlinear** outer loop. It composes over a caller-supplied **base**
  `CompanionSupply` via `ComposedCompanionSupply`: `at(i)` returns Newton's
  per-iteration diode companion for diode indices and **delegates to the base** for
  every other index — i.e. the reactive elements. `NewtonSolver::solve()`
  `static_assert`s that `base.at()` is `noexcept`.

A **capacitor and an inductor are reactive**: their constitutive laws are
differential (`i = C·dv/dt`, `v = L·di/dt`), so they have no single-timestep
conductance until an **implicit integration rule** discretizes the time derivative.
The standard resolution is a **companion model** — each timestep, replace the
reactive element with a Norton `Companion{Geq, Ieq}` (a resistor-plus-source)
computed from the element parameter, `dt`, and the element's **history** (the
previous timestep's terminal voltage/current). MNA then stamps that companion like
any other conductance; Newton fixes it across its iterations and varies only the
diodes. Owning that discretization — the **integration rule**, the **reactive
history**, and the **per-sample time advance** — is this primitive.

**The seams are already cut and named for this primitive:**

- **`Companion{Geq, Ieq}`** (`models/companion.h`) is the shared,
  **integration-rule-agnostic** output type. Trapezoidal produces the same
  `{Geq, Ieq}` shape as backward-Euler; only the *formula* differs.
- **The reactive elements are stateless physics value types.**
  `Capacitor::companion(dt, vPrev)` returns `Geq = C/dt, Ieq = Geq·vPrev`;
  `Inductor::companion(dt, iPrev)` returns `Geq = dt/L, Ieq = −iPrev`. This is
  **backward-Euler, hardcoded**, and the elements hold **no state** — `vPrev`/`iPrev`
  are supplied by whoever drives them. Both headers carry the anchor note:
  *"a better integrator (trapezoidal, etc.) would change `companion()`."*
- **`CompanionSupply`** (`mna-assembler.h`) — any type exposing
  `Companion at(int componentIndex) const noexcept` — is the consumption seam, and
  Newton's **`base` supply** is the composition slot: this primitive is exactly the
  `base` Newton composes over.

**What exists today instead is lab scaffolding, duplicated four times.** The
reactive-history + backward-Euler `advanceHistory` logic is hand-rolled in
`LinearSolver`, `NullorSolver` + `OpAmpClipperSolver`, and `TransientClipper`, each
owning its own history arrays and each hardcoding the first-order update
`i^n = Geq·v^n + iPrev`. These are explicitly *non-normative, Phase-5-superseded
scaffolding*. This primitive lifts that onto the shared MNA + Newton core.

**Sibling boundary (three coordinated primitives):** MNA is the linear engine;
`newton-iteration` owns the nonlinear outer loop; `implicit-integration` owns
**time-stepping and reactive companions**. This primitive **composes** Newton
(nonlinear) / MNA (linear) — it absorbs neither. The capability gain that earns it
production status over the labs' hardcoded first-order backward-Euler is a
**selectable, higher-order rule** (backward-Euler + trapezoidal), mirroring how MNA
generalized past grounded-source-only and Newton past single-nonlinearity.

## User Scenarios & Testing *(mandatory)*

### User Story 1 - Discretize a single reactive element to its exact discrete response (Priority: P1)

A caller has a first-order reactive network with a known closed-form discrete
solution — an RC or RL circuit driven by a step — and integrates it forward one
timestep at a time with the default backward-Euler rule, reading back node voltages
that match the analytic discrete response.

**Why this priority**: This is the minimal companion-model time-step — the MVP.
Without it the primitive does nothing; with it, a reactive element can be advanced
through time against an exact oracle.

**Independent Test**: Integrate an RC low-pass driven by a voltage step; at each
sample compare the capacitor node voltage against the exact backward-Euler discrete
response `v[n] = v[n−1] + (dt/(RC+dt))·(Vin − v[n−1])` (and the RL dual) to a tight
tolerance.

**Acceptance Scenarios**:

1. **Given** an RC network at zero initial state and a step input, **When** it is
   integrated sample-by-sample with the backward-Euler rule, **Then** each node
   voltage matches the exact discrete response to within tolerance and the sequence
   monotonically approaches the DC steady state.
2. **Given** an RL network under the same drive, **When** integrated, **Then** the
   inductor current matches the exact discrete response and approaches its DC value.

---

### User Story 2 - Select the integration rule and observe the accuracy gain (Priority: P1)

A caller selects the integration rule at plan time — backward-Euler (1st-order,
damped) or trapezoidal (2nd-order) — and the same network integrated under each rule
exhibits the rule's characteristic accuracy, with trapezoidal converging to the
continuous solution one order faster as the timestep shrinks.

**Why this priority**: The selectable rule is the capability gain that justifies a
production primitive over the labs' hardcoded backward-Euler. It is the reason the
primitive exists.

**Independent Test**: Integrate an RC (or LC) network under each rule at a sequence
of shrinking timesteps; fit the global error vs. `dt` and assert the observed
convergence order is ≈1 for backward-Euler and ≈2 for trapezoidal — a distinguishing
test that proves the rule is actually selectable, not aliased to one implementation.

**Acceptance Scenarios**:

1. **Given** a reactive network and the trapezoidal rule, **When** integrated, **Then**
   node voltages match the exact trapezoidal discrete response
   (`Geq = 2C/dt, Ieq = Geq·vPrev + iPrev` for the capacitor; the inductor dual) to
   within tolerance.
2. **Given** the same network integrated under both rules across shrinking timesteps,
   **When** the global error is regressed against `dt`, **Then** backward-Euler shows
   first-order and trapezoidal second-order convergence.

---

### User Story 3 - Provide the base companion supply the siblings consume (Priority: P1)

The primitive exposes its per-timestep reactive companions as a `CompanionSupply`
(`Companion at(int) const noexcept`) — handed to `NewtonSolver::solve` as the **base**
when the netlist has nonlinear elements, or straight to `MnaAssembler::refresh` for a
purely linear reactive circuit.

**Why this priority**: This composition is how the three-primitive boundary stays
intact — the reactive companions live here, MNA sees one supply, and Newton adds only
the diode layer over this base. Required for any circuit mixing reactive and nonlinear
elements.

**Independent Test**: Build the supply for a reactive index set; hand it to
`MnaAssembler::refresh` directly (linear case) and to `NewtonSolver::solve` as `base`
(nonlinear case); confirm the reactive companions are stamped unchanged across Newton's
iterations while only the diode companions vary.

**Acceptance Scenarios**:

1. **Given** the reactive companion supply and a linear reactive netlist, **When**
   fed to `MnaAssembler::refresh`, **Then** the reactive elements are stamped from this
   supply and the linear system solves.
2. **Given** a mixed reactive+diode netlist, **When** the supply is used as Newton's
   `base`, **Then** `at(i)` returns the fixed reactive companion for reactive indices
   and Newton overrides only the diode indices, held fixed across its iterations.
3. **Given** the supply's `at()`, **When** referenced from Newton's `solve()` path,
   **Then** it satisfies the `noexcept` requirement (no throw on the hot path).

---

### User Story 4 - Own reactive history and advance it once per converged timestep (Priority: P1)

The primitive owns each reactive element's history `{vPrev, iPrev}` and, after a
timestep's solve converges, advances it exactly once from the converged terminal
voltage via the rule-agnostic contract `iPrev := Geq·v^n − Ieq`, `vPrev := v^n`, using
that step's stamped companion.

**Why this priority**: History ownership is the primitive's defining charter and the
consolidation of the 4×-duplicated hand-rolled `advanceHistory`. The rule-agnostic
contract prevents companion/history drift between rules.

**Independent Test**: Step a reactive network and, after each converged solve, assert
the stored `{vPrev, iPrev}` equals `{v^n, Geq·v^n − Ieq}` using the companion stamped
that step; confirm history advances exactly once per timestep (not per Newton
iteration).

**Acceptance Scenarios**:

1. **Given** a converged timestep, **When** history is advanced, **Then**
   `vPrev = v^n` and `iPrev = Geq·v^n − Ieq` for every reactive element, using this
   step's stamped companion (computed from the prior history, not recomputed from the
   new one).
2. **Given** a reactive element's reconstructed current `i^n` (caps/inductors are
   Norton-stamped, not branch unknowns), **When** it is computed post-solve, **Then**
   it equals `Geq·v^n − Ieq` — the same relation that defines the stamp.
3. **Given** a multi-iteration Newton solve on a reactive+diode network, **When** it
   converges, **Then** reactive history is advanced once (after convergence), not once
   per Newton iteration.

---

### User Story 5 - Own time-stepping: drive a full transient by composing Newton/MNA (Priority: P1)

The primitive owns the per-sample time advance. Given a planned netlist, per sample it
computes the fixed reactive companions from history, drives the solve by **composing**
the appropriate sibling — Newton for a nonlinear netlist, MNA directly for a linear one
— using its companion supply and the warm-start node voltages it holds, then advances
history from the converged solution.

**Why this priority**: Time-stepping ownership is the charter. Driving a real transient
end-to-end (e.g. a reactive diode clipper) is the capability that makes the trio usable
for circuit models, and keeps warm-start/time state out of the stateless Newton.

**Independent Test**: Integrate a reactive diode-clipper transient sample-by-sample;
confirm the primitive computes companions once per sample, composes Newton, advances
history, and produces a stable output waveform — with warm-start voltages carried across
samples by the primitive.

**Acceptance Scenarios**:

1. **Given** a linear reactive netlist (no diodes), **When** a sample is stepped, **Then**
   the primitive drives `MnaAssembler::refresh` + `MnaSystem::solve` directly (no Newton)
   and advances history.
2. **Given** a reactive+diode netlist, **When** a sample is stepped, **Then** the
   primitive supplies its reactive companions as Newton's `base`, passes the warm-start
   guess, and advances history from Newton's converged voltages.
3. **Given** consecutive samples, **When** stepped, **Then** the warm-start node voltages
   and reactive history carry forward across samples, owned by this primitive.

---

### User Story 6 - Two-phase: plan once, RT-safe hot-path step (Priority: P1)

The caller plans once per netlist (topology fixed, reactive index map built, history
storage sized — validation permitted to throw off the hot path), then calls the
per-sample `step()` repeatedly on the audio hot path with **zero** heap allocation and
no locks.

**Why this priority**: Real-time safety (Constitution Principle VI) is non-negotiable
for a primitive on the audio path. The plan/step split keeps topology validation and
allocation off the per-sample path, and — by computing companions once per step rather
than per Newton iteration — dissolves backlog TASK-13 by construction.

**Independent Test**: Plan once, then run many `step()` calls under an
`AllocationSentinel` asserting zero heap allocations and confirming no re-planning and
no per-iteration companion re-assembly.

**Acceptance Scenarios**:

1. **Given** a planned integrator, **When** `step()` runs on the hot path, **Then** it
   performs zero heap allocations, takes no locks, and does not call the throwing
   plan/allocate path.
2. **Given** an unplanned integrator, **When** `step()` is called, **Then** it is a
   precondition violation surfaced deterministically (not undefined behavior).
3. **Given** a reactive+diode step, **When** Newton iterates, **Then** the fixed
   reactive companions are computed once for the step, not recomputed each iteration.

---

### User Story 7 - No fallback: surface non-convergence and never silently switch the rule (Priority: P2)

When the composed Newton solve does not converge within its bound, the failure is
surfaced by value (the `NewtonStatus` is propagated); history is not silently advanced
from an untrustworthy iterate as if physical. The integration rule is **never silently
switched** (e.g. trapezoidal→backward-Euler) to rescue a stiff or ringing node.

**Why this priority**: No-fallback honesty (Constitution Principle V) is a hard project
rule, consistent with MNA (no silent gmin) and Newton (no fabricated output). A masked
non-convergence or a silent rule switch is a bug factory; a surfaced one is actionable.

**Independent Test**: Drive a composed solve past the Newton bound and assert the
non-converged status is propagated by value; assert the selected rule is used verbatim
with no automatic substitution, and that trapezoidal ringing on a stiff node is reported
as the rule's documented behavior, not silently damped.

**Acceptance Scenarios**:

1. **Given** a step whose composed Newton solve does not converge, **When** it returns,
   **Then** the non-converged `NewtonStatus` is surfaced by value and the caller can see
   the failure (no throw on the hot path, no fabricated output).
2. **Given** the trapezoidal rule and a stiff reactive node that rings, **When**
   integrated, **Then** the ringing is produced faithfully (A-stable, not L-stable) and
   the rule is **not** silently switched to backward-Euler.

---

### User Story 8 - Reproduce the lab backward-Euler solvers (equivalence oracle) (Priority: P2)

On topologies the lab solvers (`LinearSolver`, `TransientClipper`, `OpAmpClipperSolver`)
already integrate with hardcoded backward-Euler, the primitive — with its rule set to
backward-Euler — produces equivalent node voltages across a transient within tolerance.

**Why this priority**: The labs are the trusted reference for these topologies; matching
them with the rule pinned to backward-Euler proves the primitive is correct on real
circuits and de-risks the eventual lab migration (TASK-14). Priority P2 because it
validates rather than delivers new capability.

**Independent Test**: Run the primitive (rule = backward-Euler) and a lab solver on the
same reactive (and reactive+diode) topology across a transient; assert node-voltage
agreement to tolerance at matched samples.

**Acceptance Scenarios**:

1. **Given** a reactive topology a lab integrates, **When** both integrate the same
   transient with backward-Euler, **Then** their node voltages agree to within tolerance
   across the transient.

---

### Edge Cases

- **Zero reactive elements**: the integrator's companion supply is empty; a step reduces
  to a single linear (or Newton) solve with no reactive stamping. It must be a clean
  no-op passthrough, not an error.
- **DC steady state**: driven to steady state, a capacitor behaves as an open circuit
  (`i → 0`) and an inductor as a short (`v → 0`); both rules must reach the correct DC
  operating point.
- **Initial conditions**: history defaults to zero state on `plan`/`reset`; the first
  step integrates from that state. A settled DC-operating-point warm start is a caller
  concern (out of scope for v1) — the primitive provides the *mechanism* to express it
  (`seedHistory(reactiveSlot, vPrev, iPrev)`, contract IC1/IC2) but does not auto-compute
  the operating point. This mechanism matters for the higher-order rule: a hard
  step-from-zero-state is inconsistent initial data for **trapezoidal** (its non-state
  history term must equal the true initial derivative), so demonstrating trapezoidal's
  2nd-order convergence (SC-002) requires seeding the consistent IC; backward-Euler is
  unaffected (it reads only its own state term).
- **Timestep change**: companions depend on `dt`; a `dt` change requires recomputing
  them (fine off the hot path). v1 targets a fixed audio `dt` per plan.
- **Trapezoidal ringing on a stiff node**: trapezoidal is A-stable but not L-stable and
  can ring; this is a documented property, surfaced faithfully, never silently damped by
  switching the rule (US7).
- **Non-convergence of a composed nonlinear step**: surfaced by value via the propagated
  `NewtonStatus`; reactive history is not advanced as if the untrustworthy iterate were
  physical.

## Requirements *(mandatory)*

### Functional Requirements

- **FR-001**: The primitive MUST discretize each `Capacitor` and `Inductor` in a planned
  netlist into a Norton `Companion{Geq, Ieq}` per the selected integration rule, from the
  element parameter, the timestep `dt`, and the element's history.
- **FR-002**: The primitive MUST support a **selectable integration rule** — backward-Euler
  (1st-order, L-stable, the default) and trapezoidal (2nd-order, A-stable) — with the
  companion formulas: capacitor BE `Geq = C/dt, Ieq = Geq·vPrev`; capacitor trapezoidal
  `Geq = 2C/dt, Ieq = Geq·vPrev + iPrev`; inductor BE `Geq = dt/L, Ieq = −iPrev`; inductor
  trapezoidal `Geq = dt/(2L), Ieq = −(iPrev + Geq·vPrev)`. Signs match the MNA convention
  (`i(a,b) = Geq·(V(a)−V(b)) − Ieq`).
- **FR-003**: The integration rule MUST be **fixed per plan**, selected as a **template
  parameter** on the integrator type (not switched per sample), keeping the per-sample
  companion path branch-free.
- **FR-004**: The interface MUST stay generic enough that a further implicit rule
  (e.g. BDF2/Gear) can be added without reshaping the primitive.
- **FR-005**: The primitive MUST own each reactive element's history as `{vPrev, iPrev}`
  — the superset both supported rules need (backward-Euler reads one term; trapezoidal
  reads both).
- **FR-006**: The primitive MUST expose its per-timestep reactive companions as a
  `CompanionSupply` exposing `Companion at(int componentIndex) const noexcept` (returning
  the fixed reactive companion for reactive indices), satisfying MNA's single-supply
  contract and Newton's `noexcept` `static_assert` on `base.at()`.
- **FR-007**: The reactive companions MUST be **fixed for the duration of one timestep**:
  computed once per `step()` (before the solve), stamped by MNA / used as Newton's `base`,
  and reused for the history advance — never recomputed inside the Newton loop.
- **FR-008**: The primitive MUST advance reactive history **once** per timestep, after the
  solve converges, via the rule-agnostic contract `vPrev := v^n` and
  `iPrev := i^n = Geq·v^n − Ieq`, using that step's stamped companion (`Geq`, `Ieq`
  computed from the prior history, not recomputed from the new history).
- **FR-009**: A reactive element's current `i^n` MUST be reconstructed post-solve from the
  converged terminal voltage and the stamped companion (`i^n = Geq·v^n − Ieq`); reactive
  elements are Norton-stamped and are NOT MNA branch unknowns.
- **FR-010**: The primitive MUST own the **per-sample time advance** and the warm-start
  node voltages (previous converged sample), and MUST drive the solve by **composing**
  Newton (nonlinear netlists) or `MnaAssembler`/`MnaSystem` directly (linear netlists) —
  reimplementing neither the linear algebra nor the Newton loop.
- **FR-011**: Assembly MUST be **two-phase**: a `plan()` run once per netlist off the hot
  path (throw-permitted: scan the netlist, record reactive component indices, size
  fixed-capacity history storage; delegating topology work to `MnaAssembler::plan`), and a
  hot-path `step()` that computes companions, drives the composed solve, and advances
  history without re-planning.
- **FR-012**: The `step()` hot path MUST perform **zero** heap allocation and take no locks
  (Constitution Principle VI). All working storage MUST be fixed-capacity, sized by
  template parameters.
- **FR-013**: The primitive MUST be **stateful across samples**, owning reactive history +
  `dt` + warm-start voltages, with an explicit state lifecycle: `plan()` sizes it,
  `reset()` returns it to a known zero-state initial condition, `step()` advances it. Given
  identical (state, inputs), `step()` MUST yield identical output.
- **FR-014**: On a composed nonlinear step, a non-converged `NewtonStatus` MUST be surfaced
  **by value**; the primitive MUST NOT advance reactive history from an untrustworthy
  iterate as if it were physical, and MUST NOT throw on the hot path.
- **FR-015**: The integration rule MUST NEVER be silently switched or adapted to rescue a
  stiff/ringing case (no hidden fallback, Principle V). Trapezoidal ringing on a stiff node
  is a documented, faithfully-produced property.
- **FR-016**: History MUST default to **zero state** on `plan`/`reset`. A DC-operating-point
  warm start is a caller concern, out of scope for v1 — but the primitive MUST expose an
  off-hot-path `seedHistory(reactiveSlot, vPrev, iPrev)` mechanism (contract IC1/IC2) so a
  caller can supply a consistent initial condition (an explicit opt-in; the default stays zero).
  Auto-computing the DC operating point remains out of scope for v1.
- **FR-017**: v1 MUST consume the existing frozen vocabulary (`core/primitives/circuit/`),
  the shipped MNA primitive, and the shipped Newton primitive; it MUST add **no new
  component types**.
- **FR-018**: The backward-Euler companion physics MUST be single-sourced (not a third copy
  of the `C/dt` / `dt/L` constants) — reusing the element `companion(dt, ·)` methods for the
  BE case where practical; trapezoidal is computed in the integrator.
- **FR-019**: The primitive MUST be a **production primitive** under
  `core/primitives/circuit/integration/`, namespace `acfx::integration`, sibling to
  `acfx::mna` / `acfx::newton`, template-sized (`<MaxNodes, MaxComponents, MaxBranches>`
  plus the rule parameter). The folder MUST be created in the same commit as the primitive.
- **FR-020**: The primitive MUST be **header-only**, C++17, with no platform headers, and
  each source file MUST stay within the ~300–500 line budget.
- **FR-021**: The primitive MUST be validated by **exact closed forms** where a reactive
  network has an analytic discrete response (RC / RL step response for both rules;
  series/parallel LC), NOT transcribed published values.
- **FR-022**: The primitive MUST be validated by a **convergence-order** test: by timestep
  refinement, backward-Euler MUST exhibit ≈1st-order and trapezoidal ≈2nd-order global
  convergence — proving the rule is genuinely selectable, not aliased.
- **FR-023**: The primitive MUST be validated against **physical invariants**: DC steady
  state (cap → open, inductor → short) and passivity/energy behavior for a passive network.
- **FR-024**: An **equivalence-oracle** test MUST reproduce the lab backward-Euler solvers'
  (`LinearSolver` / `TransientClipper` / `OpAmpClipperSolver`) node voltages on shared
  topologies, with the rule set to backward-Euler, across a transient to within tolerance.
- **FR-025**: A test MUST assert (via `AllocationSentinel`) that the `step()` hot path
  performs zero heap allocations across many steps.

### Key Entities *(include if feature involves data)*

- **ReactiveIntegrator**: the stateful per-sample time-stepping driver. Template-sized
  (`MaxNodes`, `MaxComponents`, `MaxBranches`) and parameterized by the integration rule.
  Owns reactive history, `dt`, and warm-start node voltages; two-phase (`plan`/`step`);
  composes Newton/MNA. **Stateful across samples** — the one stateful sibling.
- **Reactive history record**: `{vPrev, iPrev}` per reactive element — the superset both
  rules consume; advanced once per converged timestep.
- **Integration rule** (template parameter): backward-Euler (default) or trapezoidal;
  fixed per plan, selecting the per-element companion formula.
- **Reactive CompanionSupply**: the view exposing `Companion at(int) const noexcept` that
  returns the fixed reactive companion per reactive index. Consumed by
  `MnaAssembler::refresh` (linear) or as Newton's `base` (nonlinear).
- **Companion `{Geq, Ieq}`**: the shared, rule-agnostic Norton stamp; its `{Geq, Ieq}`
  values are the per-element, per-rule discretization output.

## Success Criteria *(mandatory)*

### Measurable Outcomes

- **SC-001**: On every exact-closed-form network in the suite (RC and RL step response,
  under both rules; series/parallel LC), the integrated node voltages match the analytic
  discrete response to within tolerance.
- **SC-002**: By timestep refinement, the observed global-error convergence order is ≈1 for
  backward-Euler and ≈2 for trapezoidal — demonstrating the rule is genuinely selectable.
  (Trapezoidal is integrated from a caller-seeded consistent initial condition — `seedHistory`,
  contract IC1/IC2 — since a step-from-zero-state is inconsistent initial data for a 2nd-order
  rule; backward-Euler needs no seeding.)
- **SC-003**: On topologies the lab solvers integrate, the primitive with rule =
  backward-Euler agrees with `LinearSolver` / `TransientClipper` / `OpAmpClipperSolver`
  node voltages to within tolerance across a transient.
- **SC-004**: The `step()` hot path performs exactly **zero** heap allocations and takes no
  locks across a large batch of steps (asserted by `AllocationSentinel`), and reactive
  companions are computed once per step (not per Newton iteration).
- **SC-005**: After each converged timestep, stored history equals `{v^n, Geq·v^n − Ieq}`
  using that step's stamped companion, for every reactive element (the rule-agnostic
  advance contract holds under both rules).
- **SC-006**: Driven to steady state, every passive reactive network reaches the correct DC
  operating point (cap → open, inductor → short), and passive networks exhibit no energy
  gain.
- **SC-007**: Stepping identical (state, inputs) twice yields identical output
  (determinism); the integration rule is used verbatim with **zero** silent switches or
  fabricated outputs, and non-converged composed steps surface the failure by value.
- **SC-008**: Each shipped source file remains within the ~300–500 line budget and
  introduces no new component types.

## Assumptions

- The shipped MNA primitive (`MnaSystem`, `MnaAssembler`, its two-phase
  plan/refresh/solve and `CompanionSupply` seam) is the linear engine, unchanged; the
  shipped Newton primitive (`NewtonSolver`, `ComposedCompanionSupply` `base`) is the
  nonlinear engine, unchanged. Any gap discovered in either is surfaced (per the MNA
  backlog TASK-15/16/17), not worked around here.
- The reactive elements (`Capacitor`, `Inductor`) own their physics parameters and remain
  **stateless** value types; the integrator owns all reactive history.
- v1 is real-valued (`double`) and transient — the integrator stays real even if MNA later
  generalizes to a complex scalar for AC analysis.
- v1 targets a fixed audio sample rate → a fixed `dt` per plan; a `dt`-change path (re-plan)
  is deferred.
- Node 0 is ground / the dropped reference, consistent with MNA and Newton.
- Lab migration (TASK-14) — retiring the 4× hand-rolled `advanceHistory` and the lab outer
  loops onto MNA + Newton + implicit-integration — is follow-on, sequenced once the trio is
  complete; out of scope for v1.

## Open Questions

1. **Exact backward-Euler single-sourcing mechanism (FR-018)** — reuse the element
   `companion(dt, ·)` for the BE case vs. centralize both rules in the integrator (demoting
   the element method to a documented reference). Both keep the physics constant
   single-sourced; a code-shape call for the plan.
2. **Variable timestep** — v1 holds `dt` fixed per plan; whether to expose a supported
   `dt`-change (re-plan) path is deferred (audio rate is effectively fixed within a render
   block).
3. **Trapezoidal ringing on stiff nodes** — surfaced as a documented property with
   backward-Euler the damped default; an *explicitly-selected, reported* adaptive/higher-order
   integrator (BDF2/Gear) is a future capability, never a silent switch (FR-015).
4. **DC-operating-point initialization** — v1 defaults history to zero state; a settled
   DC-OP warm start is left to the caller. v1 provides the *mechanism* to express a caller
   initial condition (`seedHistory`, contract IC1/IC2) — required to demonstrate trapezoidal's
   2nd-order convergence (SC-002) from consistent initial data — but does **not** auto-compute
   the operating point. Whether a later convenience path that auto-computes the DC-OP belongs
   here or in a transient-assembly feature is captured, not v1.
