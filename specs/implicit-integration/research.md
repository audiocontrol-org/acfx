# Research — Implicit-integration primitive (Phase 0)

Implementation-shaping decisions. Each: Decision / Rationale / Alternatives considered.
No `NEEDS CLARIFICATION` remained after the approved, third-party-reviewed design record;
these resolve the implementation-level unknowns the plan surfaced.

## R1 — Companion formulas per element and rule, and the MNA stamp signs

**Decision.** Discretize each reactive element into a Norton `Companion{Geq, Ieq}` in the
MNA consumption convention `i(a,b) = Geq·(V(a)−V(b)) − Ieq` (the shipped `MnaAssembler`
stamps `stampConductance(a,b,Geq)` + `stampRhsCurrent(a,+Ieq)/(b,−Ieq)`):

| element / rule | `Geq` | `Ieq` |
|---|---|---|
| Capacitor, backward-Euler | `C/dt` | `Geq·vPrev` |
| Capacitor, trapezoidal | `2C/dt` | `Geq·vPrev + iPrev` |
| Inductor, backward-Euler | `dt/L` | `−iPrev` |
| Inductor, trapezoidal | `dt/(2L)` | `−(iPrev + Geq·vPrev)` |

**Rationale.** From the constitutive laws and each rule's discretization: capacitor
`i = C·dv/dt`; backward-Euler `i^n = (C/dt)(v^n − v^{n-1})` ⇒ in `i = Geq·v − Ieq`,
`Ieq = Geq·vPrev`. Trapezoidal `(i^n+i^{n-1})/2 = (C/dt)(v^n−v^{n-1})` ⇒
`i^n = (2C/dt)v^n − [(2C/dt)vPrev + iPrev]`, so `Geq = 2C/dt`, `Ieq = Geq·vPrev + iPrev`.
Inductor `v = L·di/dt`; backward-Euler `i^n = iPrev + (dt/L)v^n` ⇒ `Geq = dt/L`,
`Ieq = −iPrev`. Trapezoidal `i^n = iPrev + (dt/2L)(v^n + vPrev)` ⇒ `Geq = dt/(2L)`,
`Ieq = −(iPrev + Geq·vPrev)`. The BE rows match the existing hardcoded
`Capacitor::companion` / `Inductor::companion` exactly (verified against `capacitor.h`,
`inductor.h`), confirming the convention; trapezoidal is the new capability.

**Alternatives considered.** (a) A different sign convention (`i = Geq·v + Ieq`) —
rejected: MNA's convention is fixed by the shipped assembler; matching it is mandatory.
(b) Computing companions inside MNA — rejected: MNA is deliberately rule-agnostic and holds
no history (the whole reason this primitive exists).

## R2 — Rule selection as a template policy (branch-free hot path)

**Decision.** The integration rule is a **template parameter** on the integrator type
(`ReactiveIntegrator<Rule, ...>`), realized as a small policy with two static functions —
`capacitorCompanion(C, dt, vPrev, iPrev) → Companion` and `inductorCompanion(L, dt, vPrev,
iPrev) → Companion` — with `BackwardEuler` and `Trapezoidal` implementations from R1. The
rule is fixed per plan; no per-sample branch on rule.

**Rationale.** The design record (Decision 5) and the third-party review pinned this: the
rule is fixed per plan and a template policy keeps the per-sample companion path
branch-free, matching the siblings' `<MaxNodes, …>` template-sizing pattern. A runtime need
stays reachable by instantiating both policies and selecting the plan.

**Alternatives considered.** (a) Runtime enum + branch in the hot path — rejected
(needless per-sample branch; rule never changes within a plan). (b) Rule on the element
`companion()` method — rejected (design record Approach D: trapezoidal needs extra history
the stateless value-types don't hold).

## R3 — History state shape and the rule-agnostic advance contract

**Decision.** Hold `{vPrev, iPrev}` per reactive element (the superset both rules need). After
a converged timestep, advance **once** via `vPrev := v^n`, `iPrev := i^n = Geq·v^n − Ieq`,
using **that step's stamped companion** (`Geq`, `Ieq` computed from the prior history before
the solve — not recomputed from new history). `v^n = V(a) − V(b)` read from the converged node
voltages.

**Rationale.** `i^n = Geq·v^n − Ieq` is the companion's own defining relation, so the advance
is rule-agnostic: it reuses the companion already computed for the stamp rather than
re-deriving a parallel per-rule expression, structurally preventing companion/history drift
between rules (design record Decision 10; third-party review). Backward-Euler ignores the
stored `iPrev` when forming its next companion; trapezoidal consumes both — but both **store**
both, so switching rules across plans needs no state reshape.

**Alternatives considered.** (a) Store only the term each rule reads (`vPrev` for cap-BE,
`iPrev` for ind-BE) — rejected: cannot support trapezoidal or a rule switch without reshape.
(b) Re-derive `i^n` per rule with a separate formula — rejected: duplicates the companion
math and risks drift.

## R4 — Reconstructing the reactive element current post-solve

**Decision.** A reactive element's current `i^n` is **not** an MNA branch unknown
(capacitors/inductors are Norton-stamped as conductances, not branch constraints), so it is
reconstructed post-solve as `i^n = Geq·v^n − Ieq` from the converged terminal voltage and the
stamped companion. This value feeds R3's history advance.

**Rationale.** MNA solves for node voltages and *voltage-source* branch currents only; a
Norton-stamped element contributes a conductance + RHS current, not an augmented row. The
element current is therefore a derived quantity, and the only consistent derivation is its own
companion relation — which is exactly what makes R3's contract hold.

**Alternatives considered.** Add an explicit branch per reactive element to read its current —
rejected: needlessly grows the augmented system, contradicts MNA's Norton stamping, and the
companion relation already gives the exact current.

## R5 — Composition: reactive supply as MNA refresh input and Newton base

**Decision.** The integrator builds a fixed-capacity reactive-companion array (indexed by
component index) once per `step()`, and exposes a small `CompanionSupply` view with
`Companion at(int) const noexcept` returning that array's entry for reactive indices. For a
**linear** netlist (no diodes) the view is handed straight to `MnaAssembler::refresh`; for a
**nonlinear** netlist it is handed to `NewtonSolver::solve` as the `base` supply (Newton's
`ComposedCompanionSupply` overrides diode indices and delegates reactive indices to this
`base`). `at()` is `noexcept` (Newton `static_assert`s it).

**Rationale.** MNA consumes exactly one `CompanionSupply`; Newton composes over a `base`. This
primitive is the reactive `base`. Building the array once per step (topology fixed at `plan`)
keeps `at(i)` O(1) and allocation-free, and holds the reactive companions **fixed** across
Newton's iterations (only diodes vary) — dissolving TASK-13 by construction.

**Alternatives considered.** (a) Recompute companions inside the solve/Newton loop —
rejected: TASK-13 waste, and companions are fixed per timestep by design. (b) Grow MNA/Newton
to take a second supply — rejected: composition needs no change to either shipped sibling.

## R6 — Time-stepping ownership and the composed step

**Decision.** The integrator owns the per-sample loop and the warm-start node voltages. One
`step(nl, drivers…)`: (1) compute the fixed reactive companions from current history + `dt` +
rule; (2) drive the solve by composing the appropriate sibling — `NewtonSolver::solve(nl,
reactiveSupply, warmStart, assembler, sys)` when the netlist has diodes, else
`MnaAssembler::refresh(nl, reactiveSupply, sys)` + `MnaSystem::solve()`; (3) read the converged
node voltages, update the warm start, and advance reactive history (R3). Whether the netlist
has nonlinear elements is determined once at `plan()` (the `isNonlinear` scan), so `step()`
takes the right branch without re-scanning.

**Rationale.** "Owns time-stepping" is the charter. Composing Newton/MNA (rather than
reimplementing either) keeps the three-primitive boundary intact and keeps warm-start / time
state out of the stateless Newton. The linear-vs-nonlinear branch is a plan-time constant, not
a per-sample decision on the values.

**Alternatives considered.** (a) Always route through Newton (even for linear netlists) —
rejected: needless nonlinear loop over a purely linear system; a linear netlist has zero
diodes so Newton would do one MNA solve anyway, but the direct MNA path is clearer and avoids
Newton's composed-supply overhead. (b) Push the step loop onto the caller — rejected: the
per-sample advance and warm-start ARE this primitive's charter (design record Approach C
rejection).

## R7 — Two-phase structure and zero-heap hot path

**Decision.** `plan()` (off hot path, throw-permitted) delegates topology + branch allocation
to `MnaAssembler::plan`, scans the netlist once to record the reactive component indices (and
whether any nonlinear element is present, for R6's branch), and sizes fixed-capacity history +
companion arrays. `step()` (hot path) does only: compute companions, drive the composed solve,
read voltages, advance history — all over fixed-capacity `std::array` sized by the template
parameters; no allocation, no locks, no re-planning.

**Rationale.** Mirrors the MNA/Newton proven split and sidesteps the lab's per-iteration
recompute (TASK-13) by construction. `AllocationSentinel` asserts zero heap across many steps
(FR-025/SC-004).

**Alternatives considered.** Single-phase `step()` that re-scans topology each sample —
rejected (re-derives topology every sample; heap/throw risk on the hot path).

## R8 — No-fallback failure semantics and the rule contract

**Decision.** For a composed nonlinear step, the `NewtonStatus` is propagated **by value**;
on `converged == false`, the primitive surfaces the failure and does not advance reactive
history from the untrustworthy iterate as if physical. The integration rule is used verbatim
and is **never** silently switched or adapted (no trapezoidal→BE rescue). Trapezoidal ringing
on a stiff node is produced faithfully (A-stable, not L-stable) and documented, not damped.

**Rationale.** Constitution Principle V and the sibling precedent (MNA no silent gmin; Newton
no fabricated output). A masked non-convergence or a silent rule switch is a bug factory; a
surfaced one is actionable. The hot path stays throw-free; construction-time validation is
where throws live.

**Alternatives considered.** (a) Auto-fall-back to backward-Euler on ringing / adapt order —
rejected (silent substitution; Principle V). A future *explicitly-selected, reported* adaptive
integrator remains possible (spec Open Question 3), never a silent fallback. (b) Advance
history from a non-converged iterate to "keep going" — rejected (propagates an unphysical state
silently).

## R9 — Backward-Euler physics single-sourcing (spec Open Question 1)

**Decision.** For the backward-Euler policy, reuse the existing element `companion(dt, ·)`
methods (`Capacitor::companion(dt, vPrev)`, `Inductor::companion(dt, iPrev)`) so the `C/dt` /
`dt/L` constants live in exactly one place; the trapezoidal policy computes its `{Geq, Ieq}`
in the integrator (there is no element method for it, and adding one would push history-shaped
concerns onto the stateless value-types — design record Approach D rejection). The element
parameter accessors (`C()`, `L()`) provide what trapezoidal needs.

**Rationale.** Avoids a third copy of the BE physics constants while keeping the stateless
elements free of the extra history trapezoidal requires. Resolves spec Open Question 1 toward
"reuse the element method for BE."

**Alternatives considered.** Centralize both rules entirely in the integrator and demote the
element `companion()` to a documented reference — viable and equivalent on single-sourcing, but
rejected for v1 because it leaves the shipped element method unused/whistling; reusing it keeps
one obvious BE source. (Revisitable if the element method later becomes awkward to call from the
policy.)

## R10 — Validation strategy and default tolerances (deferred detail, now pinned)

**Decision.** Per the recorded `circuit-model-validation-approach`: (a) **exact closed forms**
— in-test analytic discrete responses for RC and RL step (both rules; e.g. BE RC
`v[n] = v[n−1] + (dt/(RC+dt))(Vin − v[n−1])`, trapezoidal RC its own closed form) and
series/parallel LC; (b) **convergence order** — regress global error vs `dt` over a refinement
sweep and assert slope ≈1 (BE) / ≈2 (trap); (c) **physical invariants** — DC steady state
(cap → open `i→0`, inductor → short `v→0`), passivity/no-energy-gain for passive networks;
(d) **equivalence oracle** — vs `LinearSolver` / `TransientClipper` / `OpAmpClipperSolver`
with rule = backward-Euler on shared topologies across a transient; (e) **`AllocationSentinel`**
zero-heap on `step()`. Test tolerances start at `1e-9`–`1e-12` (tightened where the discrete
math is exact); the convergence-order slope is asserted within a tolerance band (e.g. BE in
`[0.9, 1.2]`, trap in `[1.8, 2.2]`). Reuse Newton's constructor defaults (`maxIterations = 50`,
`voltageTol = 1e-9`) for the composed nonlinear path. Not transcribed published values.

**Rationale.** Proves the integrator exact where the discrete math is closed-form, proves the
rule is genuinely selectable (order test), matches the trusted lab reference on real
topologies, and enforces the RT + no-fallback contracts — without transcribing published
values (the memory explicitly avoids this).

**Alternatives considered.** Transcribed SPICE transient traces — rejected (brittle, not
first-principles). A single fixed-`dt` accuracy check without a refinement sweep — rejected: it
cannot distinguish 1st- from 2nd-order and so cannot prove the rule is selectable (SC-002).
