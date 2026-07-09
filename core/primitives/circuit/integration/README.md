# Reactive-Element Integration — Production Primitive

The `core/primitives/circuit/integration/` primitive is the time-stepping engine for
reactive-element modeling. It implements a single `ReactiveIntegrator` type that owns
the per-element reactive history (`vPrev`, `iPrev`) and the time-stepping rule, then
composes the shipped MNA core (`core/primitives/circuit/mna/`) for linear reactive
netlists or the Newton solver (`core/primitives/circuit/newton/`) for reactive +
nonlinear netlists. It holds no linear algebra and invokes neither; the integration
rule is decoupled and selectable.

## Architecture — Reactive Companion Generation & History

### Single Type: `ReactiveIntegrator<Rule, MaxNodes, MaxComponents, MaxBranches>` (namespace `acfx::integration`)

The reactive time-stepper: manages per-element integration history and generates
`Companion{Geq, Ieq}` for capacitors and inductors at each sample by applying the
selected rule to prior element states:

- **Rule** (template parameter): integration algorithm—`BackwardEuler` (default,
  1st-order L-stable) or `Trapezoidal` (2nd-order A-stable). Selectable rule is the
  capability gain over the labs' hardcoded backward-Euler.
- **History** — per reactive element: `{vPrev, iPrev}` (voltage and current from prior
  sample), updated after each step.
- **Companion generation** — after history is frozen: stamps each reactive element as
  a Norton companion `{Geq, Ieq}` onto the supplied netlist, using the rule to
  linearize the element around the prior state.
- **Per-sample time-stepping** — no algebra on its own; calls `MnaAssembler` (for
  linear netlists) or `NewtonSolver` (for reactive + nonlinear), passing companions.

This is the seam between time and nonlinearity: integration holds reactive history;
MNA and Newton each handle their domain (linear algebra and nonlinearity).

## Real-Time Safety — Two-Phase Contract

The primitive is split into **two phases with a strict error contract**:

- **Plan phase** (`ReactiveIntegrator::plan()`) — runs once per netlist, off the hot
  path: allocates history storage, validates reactive-element topology, and prepares
  the companion structure. **May throw** descriptive errors for element-count overflow,
  invalid rule selection, or degenerate component values.

- **Per-step phase** (`ReactiveIntegrator::step()`) — runs hot, per sample: advances
  history, generates companions, and delegates to MNA or Newton for solving.
  **Throws-free and heap-allocation-free**. The per-element history is stateful
  across samples and updated each step; the rule is never re-selected.

## Charter — Selectable Higher-Order Integration

This primitive **lifts the labs' single hardcoded backward-Euler rule** by offering
selectable integration order. Backward-Euler is a robust default for stiff circuits
(1st-order, unconditionally L-stable), but Trapezoidal offers 2nd-order accuracy
for smooth transients (A-stable) and faster convergence in favorable topologies.
The rule is a template parameter, not a runtime switch, and is bound at compile
time.

## Contract — No Fallback, No Silent Rule Switch

Non-convergence and integration failures are **surfaced by value**. There is **no
silent bailout to coarser rules, no implicit step-size reduction, no fallback
companion substitution**. If the integration rule fails to converge or the network is
singular, the caller sees it and decides the response (retry with finer timestep,
clamp outputs, log and continue, etc.).

## Scope — What's Intentionally Not Here

- **Linear system solver** — owned by `MnaSystem`. Integration supplies companions,
  MNA solves.
- **Nonlinear outer loop** — owned by `NewtonSolver`. Integration supplies reactive
  companions; Newton linearizes nonlinearities.
- **Adaptive step size** — v1 is fixed timestep. Adaptive refinement and step-doubling
  heuristics are documented future directions.
- **Lab migration (TASK-14)** — retiring the 4x hand-rolled `advanceHistory` methods
  in the labs onto `ReactiveIntegrator` is captured follow-on scope, sequenced after
  this primitive lands. Not part of this deliverable.
- **Higher-order rules beyond Trapezoidal** — v1 supports backward-Euler and
  Trapezoidal. Runge-Kutta, BDF3+, or IMEX integration are documented future
  directions.

## Design Record & Spec

- **Spec** — [`specs/implicit-integration/spec.md`](../../../../specs/implicit-integration/spec.md)
  — functional requirements and test acceptance criteria.
- **API Contract** — [`specs/implicit-integration/contracts/reactive-integrator.md`](../../../../specs/implicit-integration/contracts/reactive-integrator.md)
  — detailed type and method signatures.
- **Design Record** — [`docs/superpowers/specs/2026-07-07-implicit-integration-design.md`](../../../../docs/superpowers/specs/2026-07-07-implicit-integration-design.md)
  — design-phase decisions and open questions.
- **Project Folder** — [`specs/implicit-integration/`](../../../../specs/implicit-integration/)
  — the plan, research decisions, and validation guide.

Host-side validation lives in `tests/core/integration-*-test.cpp` (integrator,
rule selection, history management, RT-safety, invariants, and lab-equivalence
oracle proving parity with lab reactive-element models).
