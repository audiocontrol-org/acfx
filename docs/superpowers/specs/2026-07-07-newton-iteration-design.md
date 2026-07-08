# Newton–Raphson iteration primitive — design

- Roadmap item: `design:primitive/newton-iteration`
- Part of: `multi:feature/phase-numerical-solvers`
- Siblings: `design:primitive/modified-nodal-analysis` (shipped), `design:primitive/implicit-integration` (planned)
- Date: 2026-07-07

## stack-control design house rules (stack-control-design-v1)

- **capture-over-yagni** (mechanical): capture everything known or knowably-implied; scoping is a separate, operator-driven pass AFTER capture.
- **solution-space-alternatives** (mechanical): the solution space enumerates ≥2 alternatives, including the rejected ones with reasons.
- **required-sections** (mechanical): problem-domain, solution-space, decisions, open-questions, provenance.
- **operator-approval** (operator): not done until the operator records the `design-approved:` marker on the roadmap node.
- **handoff-to-spec-kit** (soft): terminal handoff routes to `/stack-control:define`, never `writing-plans`.
- **installation-anchored-record** (mechanical): this record lives under `docs/superpowers/specs/`.

## Problem domain

MNA (`design:primitive/modified-nodal-analysis`, shipped) solves a **linear**
circuit: given every element's contribution as a conductance, a branch constraint,
or a `Companion{Geq, Ieq}`, it assembles the augmented system `A·x = z` and solves
for node voltages and branch currents. It is, by deliberate design, stateless and
purely linear — it **consumes** companions and never computes one.

A **diode is nonlinear**. Its constitutive law is Shockley,
`I(vAK) = Is·(exp(vAK/nVt) − 1)`, and MNA cannot solve a network containing one on
its own. The standard resolution is **Newton–Raphson**: repeatedly linearize each
diode about the current voltage iterate into a Norton `Companion`, re-solve the
now-linear MNA system, and iterate until the iterate stops moving. That outer
nonlinear loop is this primitive.

The seam is already cut on both sides and left empty for exactly this:

- **`Diode` (`core/primitives/circuit/models/diode.h`)** owns the physics only and
  says so: `evaluate(vAK) → {current, conductance}` returns the Shockley current and
  its analytic small-signal conductance `g = dI/dV` (the Jacobian Newton needs);
  `vCrit()` and `limitJunctionVoltage(vNew, vOld)` implement the SPICE `pnjlim`
  step limiter. Its header states: *"assembling g/I into a system and driving Newton
  is the solver's job (FR-006)."* The limiter lives on the diode because it is a
  property of that junction (it depends on `Is`, `n`, `Vt`), not of any solver.
- **MNA (`core/primitives/circuit/mna/mna-assembler.h`)** stamps every `Diode` from
  a **caller-supplied** Norton companion obtained through a `CompanionSupply` — any
  type exposing `Companion at(int componentIndex) const noexcept`. Its header names
  the supplier: *"Nonlinear element linearized (by newton-iteration, D6) into a
  caller-supplied Norton companion."* MNA never computes it.

What exists today instead of a primitive is **lab scaffolding**. `OpAmpClipperSolver`
(`core/labs/opamp-stages/solver/opamp-clipper-solver.h`) and `TransientClipper`
(`core/labs/diode-clippers/solver/transient-clipper.h`) already demonstrate the exact
loop shape — companions fixed once per timestep, an inner Newton loop that
re-linearizes only the diodes at the current guess, `pnjlim` damping, a bounded
iteration count, and an honest non-convergence report with **no fabricated output**.
But they are explicitly *"non-normative, Phase-5-superseded scaffolding"* that
*"must NEVER grow into general MNA / a multi-nonlinearity engine"*: they **refuse**
≥2 interacting nonlinearities at distinct node pairs with a descriptive throw
(tripwire ii), and they re-implement their own linear algebra. The production
primitive is where that bounded charter is lifted onto the shared MNA core.

Consequences that motivate the primitive:

- **The capability gap is the point.** The labs solve a single nonlinearity location.
  MNA's assembler already stamps *all* diodes generically. A production Newton driver
  over that assembler solves **coupled multi-diode** networks (antiparallel clipper
  strings, bridge rectifiers) that the labs refuse — the same way MNA lifted the
  labs' grounded-only voltage-source restriction.
- **Sibling boundaries (three coordinated primitives).** MNA is the linear engine;
  `implicit-integration` owns time-stepping and reactive companions;
  `newton-iteration` owns the nonlinear outer loop. Newton must **drive** MNA and
  **compose with** implicit-integration's companions, not absorb either.
- **Duplication retirement (backlog TASK-14, follow-on).** Once the trio lands, the
  hand-rolled Newton+linear-algebra in `OpAmpClipperSolver` / `TransientClipper`
  graduates onto `NewtonSolver` + `MnaSystem`. Captured, not v1.

RT-safety and portability bar (Constitution Principle VI,
`core/primitives/README.md`, mirrored from the MNA primitive): header-only,
template-sized, **no heap allocation or locks in the solve path**, C++17, no platform
headers, files within ~300–500 lines.

## Solution space

The numerics are common to every alternative: Newton–Raphson on the MNA residual,
each diode linearized about its current bias into a Norton `Companion{Geq: g,
Ieq: I − g·vAK}` (from `Diode::evaluate`), `pnjlim` damping of each junction voltage
between iterations (`Diode::limitJunctionVoltage`), a **voltage-residual** convergence
gate `max|Δv| < voltageTol`, a bounded iteration count, and — on non-convergence or a
singular linearized system — an **honest failure report, never a fabricated output**
(no gmin, no source-stepping, no substituted value). The alternatives differ in
**how much of the transient/nonlinear problem the primitive owns** and **whether it
holds state**.

### Chosen — Stateless global-Newton driver over MNA, with companion composition (Approach A)

One header-only type under `core/primitives/circuit/newton/`, namespace
`acfx::newton` (sibling to `acfx::mna`):

- **`NewtonSolver<int MaxNodes, int MaxComponents, int MaxBranches>`** drives the
  nonlinear loop over an **already-planned** `MnaAssembler` + `MnaSystem`. Like MNA,
  it is **two-phase**: `plan()`/`prepare()` runs once per netlist off the hot path
  (delegating to `MnaAssembler::plan`, which allocates branches and validates,
  throw-permitted); `solve()` runs on the hot path, allocation-free, and reports
  failure by value rather than throwing.

- **Global multi-diode Newton.** `solve()` scans the netlist's diodes once, then
  iterates: from the current node-voltage iterate it reads each diode's
  `vAK = V(anode) − V(cathode)`, calls `Diode::evaluate(vAK) → {I, g}`, forms
  `Companion{Geq: g, Ieq: I − g·vAK}` for every diode, refreshes the assembler with
  the **composed** supply (below), solves the linear system, reads the new voltages,
  `pnjlim`-damps each diode's `vAK`, and tests `max|Δv| < voltageTol`. All diodes are
  linearized and updated together — one global Newton step over the whole system, so
  coupled nonlinearities converge jointly.

- **Companion composition (the load-bearing structural decision).** MNA's `refresh`
  takes **one** `CompanionSupply`, but two siblings feed it: `implicit-integration`
  supplies **reactive** companions (capacitor/inductor, held **fixed** for the
  timestep) and Newton supplies **diode** companions (changing every iteration).
  Newton therefore accepts a caller-supplied **base** `CompanionSupply` (the fixed
  reactive companions for this solve) and **wraps** it: `at(i)` returns Newton's
  per-iteration diode linearization when component `i` is a diode, and delegates to
  the base supply otherwise. This preserves MNA's single-supply contract exactly and
  keeps the sibling boundary crisp. For v1 DC / resistive-diode validation the base
  is a trivial empty supply (no reactive elements).

- **Stateless per solve (mirrors MNA).** `NewtonSolver` owns **no** history and **no**
  warm-start across samples. `solve()` takes an **initial node-voltage guess** (the
  caller's warm start — typically the previous converged sample, owned by the transient
  driver / `implicit-integration`) and returns the converged voltages plus a status.
  This mirrors MNA's statelessness and keeps time-stepping ownership entirely with
  `implicit-integration`. `pnjlim`'s `vOld` is the *previous inner iterate*, internal
  to one `solve()` call — not cross-sample state.

- **Honest result contract (no fallback, FR-014-style).** `solve()` returns a
  `NewtonStatus { bool converged; int iterations; double voltageResidual; double
  currentResidual; }`. `converged == false` is a legitimate, surfaced outcome (the
  returned voltages are then the last iterate and must not be trusted as physical). A
  singular linearized system mid-loop is reported the same way (via `MnaSystem::solve`
  returning `false`), **not** a throw on the hot path and **never** a silent gmin
  fallback. The current residual is **reported, not gated** — a diode's reverse
  saturation current can sit above any fixed `currentTol` even at a settled voltage,
  so gating on it would spuriously reject a converged solve (the lab established this).

Chosen because it (a) lifts the labs' single-nonlinearity charter onto the shared MNA
core — the actual capability gain and the reason it is a production primitive; (b)
preserves the three-primitive boundary exactly by *composing* companions rather than
absorbing reactive/time-stepping concerns; and (c) inherits MNA's proven shape
(two-phase, stateless, no-fallback, RT-safe). Cost: the caller must supply the base
reactive companions and the initial guess — deliberately, since those are
`implicit-integration`'s to own.

### Rejected — Transient nonlinear monolith (Approach B)

Newton owns the whole transient loop: time-stepping, reactive companion integration,
**and** the nonlinear iteration; MNA and `implicit-integration` become thin config.
Rejected for the identical reason the MNA design rejected its own monolith: it
collapses the deliberate three-primitive decomposition the roadmap drew and recreates
the tangled, hard-to-test structure the labs already demonstrate. It would also make
Newton own reactive history — precisely `implicit-integration`'s charter.

### Rejected — Single-nonlinearity, lab-parity charter (Approach C)

Match the lab exactly: exactly one nonlinearity location, refuse ≥2 interacting
nonlinearities with a descriptive throw. Rejected because it delivers **no capability
gain** over the existing `OpAmpClipperSolver` / `TransientClipper` — a new production
primitive that cannot solve anything the lab can't is not worth its surface. The MNA
sibling set the pattern by generalizing past the labs (floating sources, all
components), and MNA's assembler already stamps all diodes generically, so the global
multi-diode charter costs little beyond broader validation.

### Rejected — Stateful Newton owning warm-start history (Approach D)

Newton holds the warm-start voltage (and possibly reactive history) between samples,
like the lab's `warmStart_` / `prevNodeVoltage_`. Rejected because it breaks MNA's
statelessness pattern and entangles Newton with time-stepping state that
`implicit-integration` owns. A stateless `solve(initialGuess) → status` keeps warm-start
a *caller* concern, leaves the primitive a pure function of (netlist, base companions,
guess), and makes it independently testable — the same reasoning MNA used to stay
stateless.

### Rejected (numerics) — gmin homotopy / source-stepping fallback

Add SPICE-style gmin stepping or source stepping so a hard case "always returns
something." Rejected on the repo's no-fallback standard, consistent with the lab
(`no hidden fallback, FR-014`) and MNA (`no silent gmin fallback`). Non-convergence is
surfaced honestly as `converged == false`, not masked. (Homotopy could be revisited as
a *future, explicitly-reported* convergence aid — never a silent substitution — if
bounded damped Newton proves insufficient for stiff strings; see open questions.)

## Decisions

1. **Scope.** Newton owns the **nonlinear outer loop only**: linearize each diode into
   a `Companion`, drive `MnaAssembler::refresh` + `MnaSystem::solve`, `pnjlim`-damp,
   test convergence. It does **not** own the integration rule, reactive history, or
   time-stepping (those are `implicit-integration`), nor the linear algebra (MNA).
2. **Charter.** **General multi-diode global Newton** — any number of coupled `Diode`
   nonlinearities at arbitrary node pairs, linearized and updated together each step.
   The lab's single-nonlinearity refusal is exactly the limitation this primitive lifts.
3. **Nonlinear vocabulary (v1).** `Diode` only — the sole nonlinear element in the
   frozen vocabulary. The interface stays generic (per-element linearize + limit) so a
   future nonlinear element slots in without reshaping Newton.
4. **Structure.** A single `NewtonSolver` type driving a **planned** `MnaAssembler` +
   `MnaSystem`; it does not re-plan. Two-phase like MNA: `plan()` once off the hot path
   (throw-permitted, delegating to `MnaAssembler::plan`); `solve()` on the hot path
   (allocation-free, failure-by-value).
5. **Companion composition.** Newton wraps a caller-supplied **base** `CompanionSupply`
   (fixed reactive companions from `implicit-integration`), overriding diode component
   indices with its per-iteration linearization and delegating all others. MNA's
   single-supply contract is preserved. v1 base supply is the trivial empty supply.
6. **State.** **Stateless per solve** (mirrors MNA). `solve()` takes an initial
   node-voltage guess (caller-owned warm start) and returns `NewtonStatus`; it holds no
   cross-sample history.
7. **Convergence & damping.** Gate on the **voltage residual** `max|Δv| < voltageTol`
   only; report (don't gate) the current residual. Damp each junction with
   `Diode::limitJunctionVoltage` (`pnjlim`). Bounded `maxIterations`. Configurable
   `maxIterations`/`voltageTol` validated `> 0` at construction; never silently
   retuned to hide a non-converging case.
8. **No fallback.** Non-convergence and singular linearized systems are surfaced via
   `NewtonStatus.converged == false` (singularity through `MnaSystem::solve() == false`),
   never a throw on the hot path and never a gmin/source-step/substituted output.
9. **Placement.** A **production primitive** under `core/primitives/circuit/newton/`,
   namespace `acfx::newton`, sibling to `acfx::mna`. Header-only, template-sized, zero
   heap in `solve()`, C++17, no platform headers, ≤~300–500 lines/file. Folder created
   in the same commit as the primitive ("inhabit before creating").
10. **Validation** (per `circuit-model-validation-approach`): exact closed forms where a
    diode network has an analytic operating point (single diode + series resistor via the
    Lambert-W / iterated fixed point; symmetric antiparallel pair → zero at zero drive);
    an **equivalence oracle** against `OpAmpClipperSolver` / `TransientClipper` on the
    TS808 diode-clipper core; physical invariants (monotonic transfer, passivity,
    antiparallel symmetry, `I(0)=0`); and `AllocationSentinel` asserting zero heap in the
    solve path. Not transcribed published values.
11. **Lab migration (TASK-14) is follow-on, not v1.** Retiring the hand-rolled
    Newton+linear-algebra in the labs onto `NewtonSolver` + `MnaSystem` is captured scope,
    likely sequenced once all three sibling primitives land so the labs migrate to the
    full trio at once.

## Open questions

1. **Damping generality beyond the diode.** `pnjlim` is diode-specific and lives on
   `Diode`. A future non-diode nonlinearity would need its own limiter; the generic
   per-element "linearize + limit" interface must obtain it from the element. v1 is
   diode-only, so Newton calls `Diode::limitJunctionVoltage` directly — the generalization
   is a design note, not v1 surface.
2. **Initial-guess API shape.** Whether `solve()` accepts a full node-voltage array or a
   per-diode `vAK` warm start. Leaning node-voltage array (matches MNA's `nodeVoltage`
   read surface and lets a caller warm-start the whole solution), resolved at plan/tasks time.
3. **Sequencing vs. `implicit-integration`.** Whether Newton lands first — validated with a
   hand-supplied fixed reactive base companion (DC / resistive-diode networks) and the lab
   oracle — or in lockstep with `implicit-integration`. Leaning land-first with a
   hand-written base supply, mirroring how MNA landed ahead of its consumers.
4. **Multi-diode convergence robustness without gmin.** Whether bounded `pnjlim`-damped
   Newton converges for stiff antiparallel strings at large drive. If not, the answer is an
   *explicitly-reported* convergence aid (still no silent fallback), captured for a later
   pass — not a v1 gmin path.
5. **Complex/AC.** Newton is a DC/transient (real) concern; if MNA later generalizes to a
   complex scalar, Newton stays real. Noted, not v1.

## Provenance

- Design conducted in-session via `superpowers:brainstorming`, driven by the
  `/stack-control:design` frontend (house rules `stack-control-design-v1` injected above).
- Compass verdict `on-course` for intent `design` from phase `planned`; `design:` pointer
  set on the roadmap node (`docs/superpowers/specs/2026-07-07-newton-iteration-design.md`)
  before authoring.
- Operator (non-domain-expert) directed the charter decision to "follow the pattern of the
  rest of the project"; the general multi-diode charter and stateless/no-fallback contract
  follow directly from the just-shipped MNA sibling design.
- Existing-code references: `core/primitives/circuit/models/diode.h` (physics + `pnjlim`),
  `core/primitives/circuit/models/companion.h` (`Companion{Geq, Ieq}`),
  `core/primitives/circuit/mna/{mna-assembler.h, mna-system.h, README.md}` (the seam:
  `CompanionSupply`, `refresh`, two-phase, no-fallback),
  `core/labs/opamp-stages/solver/opamp-clipper-solver.h` and
  `core/labs/diode-clippers/solver/transient-clipper.h` (the loop shape + oracle).
- Sibling design record: `docs/superpowers/specs/2026-07-07-modified-nodal-analysis-design.md`.
- Backlog inputs: TASK-14 (unify duplicated linear algebra; lab migration target), and the
  MNA-residual items TASK-15/16/17 the shared core carries.
- Recorded memory: `circuit-model-validation-approach` (prove exact on closed forms;
  validate assembled circuits via exact limits + monotonic invariants + passivity).
