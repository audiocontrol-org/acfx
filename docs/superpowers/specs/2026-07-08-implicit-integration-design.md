# Implicit-integration primitive — design

- Roadmap item: `design:primitive/implicit-integration`
- Part of: `multi:feature/phase-numerical-solvers`
- Depends-on: `multi:feature/phase-circuit-modeling`
- Siblings: `design:primitive/modified-nodal-analysis` (shipped), `design:primitive/newton-iteration` (shipped)
- Date: 2026-07-08

## stack-control design house rules (stack-control-design-v1)

- **capture-over-yagni** (mechanical): capture everything known or knowably-implied; scoping is a separate, operator-driven pass AFTER capture.
- **solution-space-alternatives** (mechanical): the solution space enumerates ≥2 alternatives, including the rejected ones with reasons.
- **required-sections** (mechanical): problem-domain, solution-space, decisions, open-questions, provenance.
- **operator-approval** (operator): not done until the operator records the `design-approved:` marker on the roadmap node.
- **handoff-to-spec-kit** (soft): terminal handoff routes to `/stack-control:define`, never `writing-plans`.
- **installation-anchored-record** (mechanical): this record lives under `docs/superpowers/specs/`.

## Problem domain

The circuit-modeling solver is deliberately decomposed into **three coordinated
primitives**, and this one is the last of the trio:

- **MNA** (`core/primitives/circuit/mna/`, shipped) — the **linear** engine. Given
  every element's contribution as a conductance, a branch constraint, or a
  `Companion{Geq, Ieq}`, it assembles the augmented system `A·x = z` and solves for
  node voltages and branch currents. It is stateless and purely linear: it
  **consumes** companions and never computes one, and it holds **no reactive history
  and no integration rule** (`mna/README.md:76-77`: *"Integration rule and reactive
  history — owned by `implicit-integration`"*).
- **newton-iteration** (`core/primitives/circuit/newton/`, shipped) — the
  **nonlinear** outer loop. It re-linearizes each diode about the current iterate,
  re-solves the MNA system, and iterates to convergence. It is stateless per solve
  and holds no `dt`. Critically, it composes over a caller-supplied **base**
  `CompanionSupply` via `ComposedCompanionSupply` (`newton-solver.h:83-94`): `at(i)`
  returns Newton's per-iteration diode companion for diode indices and **delegates to
  the base** for every other index — i.e. for the reactive elements. Its `solve()`
  `static_assert`s that `base.at()` is `noexcept` (`newton-solver.h:206-209`).

A **capacitor and an inductor are reactive** — their constitutive laws are
differential (`i = C·dv/dt`, `v = L·di/dt`), so they have no single-timestep
conductance until the time derivative is **discretized** by an implicit integration
rule. The standard resolution is a **companion model**: each timestep, replace the
reactive element with a Norton `Companion{Geq, Ieq}` — an equivalent
resistor-plus-source computed from the element's parameter, `dt`, and its **history**
(the previous timestep's voltage/current). MNA then stamps that companion like any
other conductance; Newton fixes it across its iterations and varies only the diodes.
Owning that discretization — the integration rule, the reactive history, and the
per-sample time advance — is this primitive.

**The seams are already cut and named for this primitive:**

- **`Companion{Geq, Ieq}`** (`models/companion.h:22-25`) is already the shared,
  **integration-rule-agnostic** output type. Trapezoidal produces the same
  `{Geq, Ieq}` shape as backward-Euler; only the *formula* differs.
- **The reactive elements are stateless physics value types.** `Capacitor::companion(dt, vPrev)`
  (`capacitor.h:43-46`) returns `Geq = C/dt, Ieq = Geq·vPrev`; `Inductor::companion(dt, iPrev)`
  (`inductor.h:50-52`) returns `Geq = dt/L, Ieq = −iPrev`. **This is backward-Euler,
  hardcoded**, and the elements hold **no state** — `vPrev`/`iPrev` are supplied by
  whoever drives them. Both headers carry the anchor note: *"Backward Euler is
  deliberately non-normative… a better integrator (trapezoidal, etc.) would change
  `companion()`."*
- **`CompanionSupply`** (`mna-assembler.h:59-69`) — any type exposing
  `Companion at(int componentIndex) const noexcept` — is the consumption seam MNA's
  `refresh()` is templated on, and the header names the supplier:
  *"newton-iteration / implicit-integration supply them."*
- **Newton's `base` supply** (`newton-solver.h:83-94`) is the composition slot: this
  primitive is exactly the `base` Newton composes over.

**What exists today instead is lab scaffolding, and it is duplicated four times.**
The reactive-history + backward-Euler `advanceHistory` logic is hand-rolled in
`LinearSolver` (`component-abstractions`), `NullorSolver` + `OpAmpClipperSolver`
(`opamp-stages`), and `TransientClipper` (`diode-clippers`) — each owning its own
`prevNodeVoltage_` / `inductorCurrent_` arrays and each hardcoding the first-order
backward-Euler update `i^n = Geq·v^n + iPrev`. These are explicitly *non-normative,
Phase-5-superseded scaffolding*. Two consequences motivate the primitive:

- **The capability gap is the point (the pattern the siblings set).** MNA
  generalized past the labs' grounded-source-only restriction; Newton generalized
  past the labs' single-nonlinearity refusal. Each production primitive earns its
  surface by a **capability gain** over the lab it replaces. The labs do hardcoded
  first-order backward-Euler; this primitive's gain is a **selectable rule** —
  backward-Euler *and* trapezoidal (2nd-order, A-stable) — with the interface left
  open for further rules (BDF2/Gear). A backward-Euler-only primitive would deliver
  no gain and is rejected for the same reason Newton rejected its lab-parity charter.
- **Duplication retirement (backlog TASK-14, follow-on).** Once this lands, the four
  hand-rolled `advanceHistory` routines and the lab outer loops graduate onto
  MNA + Newton + implicit-integration. Captured, not v1. This primitive's two-phase
  shape also dissolves **TASK-13** (fixed reactive companions recomputed inside the
  Newton loop) *by construction* — companions are computed once per timestep, not per
  iteration.

RT-safety and portability bar (Constitution Principle VI, mirrored from the MNA and
Newton siblings): header-only, template-sized, **no heap allocation or locks in the
per-sample / companion path**, C++17, no platform headers, files within ~300–500
lines.

## Solution space

Common to every alternative: reactive elements are discretized into
`Companion{Geq, Ieq}` Norton stamps per timestep; the companions are **fixed for the
duration of one timestep** (Newton varies only diodes over its iterations); reactive
**history** (`vPrev`/`iPrev`) is advanced **once** after the timestep converges; the
per-sample and companion paths are RT-safe (zero heap, no locks); and non-physical
states are surfaced, never masked (no silent rule-switching, no fabricated stamp).
The alternatives differ in **where the state and the integration rule live** and in
**how much of the transient problem the primitive owns**.

### Chosen — Stateful reactive-companion integrator with a selectable rule, composing Newton/MNA (Approach A)

One header-only primitive under `core/primitives/circuit/integration/`, namespace
`acfx::integration` (sibling to `acfx::mna` / `acfx::newton`):

- **The one stateful sibling.** MNA and Newton are stateless; this primitive
  deliberately **owns the cross-sample state**: per-reactive-element history
  `{vPrev, iPrev}`, the timestep `dt`, and the selected integration rule. It also
  owns the warm-start node voltages (the previous converged sample), since those are
  time-stepping state — keeping warm-start ownership out of the stateless Newton.

- **Selectable integration rule (the capability gain).** Backward-Euler (1st-order,
  L-stable, numerically damped — the safe **default**) and trapezoidal (2nd-order,
  A-stable, more physically accurate). The rule is **fixed per plan** (chosen once,
  off the hot path — not switched per sample). Per-element companion formulas:
  - Capacitor, backward-Euler: `Geq = C/dt`, `Ieq = Geq·vPrev`.
  - Capacitor, trapezoidal: `Geq = 2C/dt`, `Ieq = Geq·vPrev + iPrev`.
  - Inductor, backward-Euler: `Geq = dt/L`, `Ieq = −iPrev`.
  - Inductor, trapezoidal: `Geq = dt/(2L)`, `Ieq = −(iPrev + Geq·vPrev)`.
  History state is `{vPrev, iPrev}` per reactive element — the superset both rules
  need (backward-Euler reads only one term; trapezoidal reads both).

- **Two-phase, mirroring the siblings.** `plan()` runs once per topology off the hot
  path (throw-permitted): it scans the netlist, records the reactive component
  indices, and sizes fixed-capacity history storage. The per-sample path is
  **`noexcept` and heap-free**: it computes each reactive element's `Companion` from
  current history + `dt` + rule, and exposes them.

- **Exposes reactive companions as a `CompanionSupply`.** A view type with
  `Companion at(int componentIndex) const noexcept` (satisfying Newton's
  `static_assert`) that returns the fixed reactive companion for reactive indices.
  This is handed to `NewtonSolver::solve(...)` as the **`base`** when the netlist has
  nonlinear elements, or straight to `MnaAssembler::refresh(...)` when the reactive
  circuit is purely **linear** (no diodes → no Newton needed).

- **Owns the per-sample time advance (`step`), composing not absorbing.** The
  primitive owns *time-stepping*: per sample it (1) computes the fixed reactive
  companions from history, (2) drives the solve by **composing** the appropriate
  sibling — Newton for nonlinear netlists, MNA directly for linear ones — passing its
  companion supply and warm-start guess, and (3) **advances** reactive history from
  the converged node voltages / branch currents, once. It reimplements neither the
  linear algebra (MNA's) nor the nonlinear loop (Newton's).

- **Honest, no-fallback contract.** A non-converged Newton solve is surfaced by value
  (the `NewtonStatus` is propagated, not swallowed); history is **not** advanced from
  an untrustworthy iterate without the failure being visible. The integration rule is
  **never silently switched** to rescue a stiff/ringing case (that would be a masked
  fallback); trapezoidal ringing on a stiff node is a documented property, and
  backward-Euler is the damped default the caller selects deliberately.

Chosen because it (a) delivers the capability gain — a selectable, higher-order
integrator — that justifies a production primitive over the labs' hardcoded
backward-Euler, exactly as MNA and Newton generalized past their labs; (b) preserves
the three-primitive boundary by *composing* Newton/MNA and owning only what the
charter assigns it (time-stepping + reactive companions + history); (c) consolidates
the 4× duplicated hand-rolled `advanceHistory` and dissolves TASK-13 by construction;
and (d) inherits the siblings' proven shape (two-phase, template-sized, RT-safe,
no-fallback). Cost: it is stateful (unlike its siblings), so it carries an explicit
state-lifecycle contract (`plan`/`reset`/`step`).

### Rejected — Backward-Euler only, lab parity (Approach B)

Ship only the first-order backward-Euler the labs already hardcode. Rejected because
it delivers **no capability gain** over the existing lab solvers — a new production
primitive that integrates no more accurately than the scaffolding it replaces is not
worth its surface (the exact reasoning that rejected Newton's lab-parity Approach C).
It also re-opens the design the moment trapezoidal is needed for the downstream
physical-modeling phases, and it violates the **capture-over-yagni** house rule when
trapezoidal is knowably implied by the anchor notes already in `capacitor.h` /
`inductor.h`.

### Rejected — Stateless integrator, caller owns all reactive history (Approach C)

Keep the integrator a pure function and push `vPrev`/`iPrev` ownership back onto the
caller (as the stateless elements do today). Rejected because it directly contradicts
the charter — this primitive's defining job is to **own time-stepping and reactive
history** — and it recreates precisely the 4× hand-rolled, backward-Euler-only
`advanceHistory` duplication across the labs that the primitive exists to retire.
Statelessness is right for MNA and Newton (pure linear/nonlinear solves); it is wrong
for the sibling whose entire charter is holding time.

### Rejected — Push the rule into the element `companion()` methods (Approach D)

Make the reactive element structs own the integration rule by extending their
`companion()` methods. Rejected because trapezoidal requires **extra cross-sample
history** (`iPrev` for the capacitor, `vPrev` for the inductor) beyond what
backward-Euler needs, and the reactive elements are **deliberately stateless value
types** that hold no history. Putting rule + the history it needs on the element
would either force state into the value types (breaking their design) or split the
companion computation awkwardly across element and integrator. The rule and the
history it consumes belong together, in the integrator.

### Rejected — Transient monolith absorbing Newton + MNA (Approach E)

One transient engine that owns time-stepping, the linear solve, *and* the nonlinear
loop, with MNA and Newton demoted to internals. Rejected for the identical reason MNA
and Newton rejected their own monolith alternatives: it collapses the deliberate
three-primitive decomposition the roadmap drew, recreates the tangled, hard-to-test
structure the labs demonstrate, and duplicates the linear algebra and Newton loop the
siblings already own.

### Rejected (numerics) — silent rule-switching / adaptive-order fallback

Automatically fall back from trapezoidal to backward-Euler (or adapt the order) when
a node rings or a solve struggles, so a hard case "always looks stable." Rejected on
the repo's **no-fallback** standard (Constitution Principle V), consistent with MNA
(no silent gmin) and Newton (no fabricated output). The rule is a deliberate,
operator/caller-selected property; trapezoidal ringing on a stiff node is surfaced as
a documented characteristic, and backward-Euler is the damped default one chooses on
purpose. (An *explicitly-selected*, reported adaptive integrator could be revisited as
a future capability — never a silent substitution; see open questions.)

## Decisions

1. **Scope.** The primitive owns the **integration rule, reactive-element history,
   and the per-sample time advance** — discretize each `Capacitor`/`Inductor` into a
   `Companion{Geq, Ieq}` per the selected rule, expose them as a `CompanionSupply`,
   drive the timestep by composing Newton/MNA, and advance history once on
   convergence. It does **not** own the linear algebra (MNA) or the nonlinear loop
   (Newton).
2. **Charter — selectable rule.** Backward-Euler (default, L-stable, damped) **and**
   trapezoidal (2nd-order, A-stable). This is the capability gain over the labs'
   hardcoded first-order backward-Euler. The interface stays generic so a further
   rule (BDF2/Gear) slots in without reshaping the primitive.
3. **Reactive vocabulary (v1).** `Capacitor` and `Inductor` — the two reactive
   elements in the frozen vocabulary (`components.h` `isReactive`). No new component
   types are added.
4. **History state shape.** `{vPrev, iPrev}` per reactive element — the superset both
   supported rules need. Backward-Euler reads one term; trapezoidal reads both.
5. **Rule is fixed per plan.** Chosen once, off the hot path — not switched per
   sample. (Template parameter vs. runtime enum is an open question; see below.)
6. **Structure — two-phase, mirroring the siblings.** `plan()` once per topology
   (throw-permitted: scan netlist, record reactive indices, size history storage);
   the per-sample path (`step` + companion computation) is `noexcept` and heap-free
   and does not re-plan.
7. **`CompanionSupply` seam.** The primitive exposes a view with
   `Companion at(int) const noexcept` returning the fixed reactive companion for
   reactive indices — satisfying MNA's `CompanionSupply` concept and Newton's `base`
   `noexcept` `static_assert`. Handed to `NewtonSolver::solve` as `base` for
   nonlinear netlists, or to `MnaAssembler::refresh` directly for purely linear ones.
8. **Stateful — the one stateful sibling.** The primitive owns cross-sample state
   (reactive history + `dt` + warm-start node voltages). It carries an explicit
   state-lifecycle contract: `plan()` sizes it, `reset()` returns it to a known
   initial condition, `step()` advances it. Given identical (state, inputs), `step()`
   yields identical output.
9. **Time-stepping ownership.** The primitive owns the per-sample loop and the
   warm-start node voltages (previous converged sample), keeping time-stepping state
   out of the stateless Newton. It **composes** Newton (nonlinear) / MNA (linear),
   absorbing neither.
10. **History advance.** Advanced **once** per timestep, after the solve converges,
    from the converged node voltages / branch currents per the selected rule. TASK-13
    (fixed companions recomputed inside the Newton loop) is dissolved by
    construction: companions are computed once per `step`, before the solve.
11. **No fallback.** A non-converged solve is surfaced by value (the `NewtonStatus` is
    propagated); history is not silently advanced from an untrustworthy iterate. The
    integration rule is **never silently switched** to rescue a stiff/ringing case —
    that would be a masked fallback (Principle V).
12. **Initial conditions.** History defaults to **zero state** on `plan`/`reset`. A
    DC-operating-point warm start (a first solve to settle initial history) is a
    composition concern for the caller, not baked into v1.
13. **Placement.** A **production primitive** under
    `core/primitives/circuit/integration/`, namespace `acfx::integration`, sibling to
    `acfx::mna` / `acfx::newton`. Header-only, template-sized
    (`<MaxNodes, MaxComponents, MaxBranches>` to match the siblings), zero heap in the
    per-sample / companion path, C++17, no platform headers, ≤~300–500 lines/file.
    Folder created in the same commit as the primitive ("inhabit before creating").
14. **Single-source the backward-Euler physics.** To avoid a third copy of the
    `C/dt` / `dt/L` constants, the backward-Euler companion should reuse the existing
    element `companion(dt, ·)` methods where practical; trapezoidal is computed in the
    integrator. The exact mechanism (reuse the element method for BE vs. centralize
    both rules in the integrator and demote the element method to a reference) is a
    resolvable detail; see open questions.
15. **Validation** (per `circuit-model-validation-approach`): **exact closed forms**
    where a reactive network has an analytic discrete response (RC / RL step response
    — both rules have known closed-form discrete solutions; series/parallel LC);
    **convergence-order** verification by timestep refinement (trapezoidal 2nd-order
    vs. backward-Euler 1st-order — a distinguishing test that proves the rule is
    actually selectable, not aliased); **physical invariants** (passivity / energy
    behavior, DC steady state where a cap → open and an inductor → short); an
    **equivalence oracle** against the lab backward-Euler solvers
    (`LinearSolver` / `TransientClipper` / `OpAmpClipperSolver`) on shared topologies
    with the rule set to backward-Euler; and `AllocationSentinel` asserting zero heap
    in the per-sample path. **Not** transcribed published values.
16. **Lab migration (TASK-14) is follow-on, not v1.** Retiring the 4× duplicated
    `advanceHistory` and the lab outer loops onto MNA + Newton + implicit-integration
    is captured scope, sequenced once the trio is complete so the labs migrate to the
    full stack at once.

## Open questions

1. **Rule selection mechanism — template parameter vs. runtime enum.** A template
   parameter keeps the companion computation branch-free on the hot path and matches
   the siblings' `<MaxNodes, …>` template-sizing pattern; a runtime enum is friendlier
   for a host that switches oversampling/rate. Since the rule is fixed per plan
   (Decision 5), a plan-time template parameter is the leaning; to be resolved in the
   spec/plan.
2. **Exact backward-Euler single-sourcing mechanism (Decision 14).** Whether to reuse
   the element `companion(dt, ·)` for the BE case or centralize both rules in the
   integrator (demoting the element method to a documented reference). Both keep the
   physics constant single-sourced; a code-shape call for the plan.
3. **Variable timestep.** v1 targets a fixed audio sample rate → fixed `dt` per plan.
   A `dt` change requires recomputing companions (they depend on `dt`), which is fine
   off the hot path. Whether to expose a supported `dt`-change path (re-plan) or hold
   `dt` immutable per plan is deferred; audio rate is effectively fixed within a
   render block.
4. **Trapezoidal ringing on stiff nodes.** Trapezoidal is A-stable but not L-stable
   and can ring on stiff reactive nodes. v1 surfaces this as a documented property
   with backward-Euler as the damped default; an *explicitly-selected, reported*
   adaptive/higher-order integrator (BDF2/Gear) is a future capability — never a
   silent switch (Decision 11).
5. **DC-operating-point initialization.** v1 defaults history to zero state
   (Decision 12); a settled DC-OP warm start is left to the caller. Whether a later
   convenience path belongs in this primitive or in a transient-assembly feature is
   captured, not v1.
6. **Complex/AC.** The integrator is a transient (real, `double`) concern, consistent
   with the real-valued siblings. If MNA later generalizes to a complex scalar for AC
   analysis, the integrator stays real. Noted, not v1.

## Provenance

- Design conducted in-session via `superpowers:brainstorming`, driven by the
  `/stack-control:design` frontend (house rules `stack-control-design-v1` injected
  above).
- Compass verdict `on-course` for intent `design` from phase `planned`; `design:`
  pointer set on the roadmap node
  (`docs/superpowers/specs/2026-07-08-implicit-integration-design.md`) before
  authoring.
- Operator (non-domain-expert) explicitly delegated the scope call, directing the
  decision to project goals, guidelines, and existing patterns. The selectable-rule
  charter (backward-Euler + trapezoidal) follows directly from the pattern the two
  shipped siblings set: each production primitive earns its surface by a **capability
  gain** over the lab scaffolding it replaces (MNA lifted grounded-source-only;
  Newton lifted single-nonlinearity; implicit-integration lifts hardcoded first-order
  backward-Euler to a selectable, higher-order rule). Operator approved the scope call
  and the chosen shape before this record was written.
- Existing-code references (from a thorough code-map exploration of the trunk):
  `core/primitives/circuit/models/companion.h:22-25` (rule-agnostic `Companion`),
  `core/primitives/circuit/models/capacitor.h:20-22,43-46` and
  `core/primitives/circuit/models/inductor.h:26-28,50-52` (hardcoded backward-Euler +
  the "a better integrator would change companion()" anchor),
  `core/primitives/circuit/components.h:53-56` (`isReactive` routing),
  `core/primitives/circuit/mna/mna-assembler.h:59-69,190-279` (the `CompanionSupply`
  seam + reactive stamping), `core/primitives/circuit/mna/README.md:76-77`
  (integration rule scoped OUT of MNA and INTO this primitive),
  `core/primitives/circuit/newton/newton-solver.h:83-94,206-209` (the `base`
  composition slot + the `noexcept` static_assert),
  and the four hand-rolled backward-Euler `advanceHistory` sites
  (`core/labs/component-abstractions/solver/linear-solver.h:174-209,377-397`,
  `core/labs/opamp-stages/solver/opamp-stage-solver.h:197-227,273-290`,
  `core/labs/opamp-stages/solver/opamp-clipper-solver.h:307-345,376-391`,
  `core/labs/diode-clippers/solver/transient-clipper.h:311-354,385-401`).
- Sibling design records: `docs/superpowers/specs/2026-07-07-newton-iteration-design.md`
  and `docs/superpowers/specs/2026-07-07-modified-nodal-analysis-design.md`.
- Backlog inputs: TASK-13 (fixed companions recomputed inside the Newton loop —
  dissolved by this primitive's two-phase design), TASK-14 (retire the four lab
  solvers onto MNA + Newton + implicit-integration; follow-on), and the MNA-residual
  items TASK-15/16/17 the shared core carries.
- Recorded memory: `circuit-model-validation-approach` (prove exact on closed forms;
  validate assembled circuits via exact limits + monotonic/passivity invariants; not
  transcribed published values).
