> ============================================================================
> ‼ acfx COMMANDMENTS (non-negotiable) ‼
> 1. COMMIT AND PUSH EARLY AND OFTEN
> 2. NO GIT HOOKS, EVER
> 3. DESCRIPTIVE NAMES, NEVER NUMERIC PREFIXES
> (acfx Constitution, Principles I-III -- `.specify/memory/constitution.md`)
> ============================================================================

# opamp-stages — Design Record

**Roadmap item:** `design:primitive/opamp-stages`
**Phase:** 4 — Circuit Modeling (the active-element deliverable)
**Part-of:** `multi:feature/phase-circuit-modeling`
**Depends-on:** `multi:feature/phase-dynamic-systems` (shipped)
**Layer:** primitive builders (`core/primitives/circuit/opamp-stage/`, portable,
RT-safe, header-only) + host-only lab solver/harness
(`core/labs/opamp-stages/`, non-normative)

---

## Problem domain

Phase 4 (Circuit Modeling) continues. `component-abstractions` shipped the
solver-neutral circuit vocabulary (R, C, L, V/I sources, Shockley `Diode`,
`Netlist<MaxNodes,MaxComponents>`) plus a deliberately-naive lab solver;
`passive-tone-stacks` shipped the first *feature* built on that vocabulary
(FMV / Baxandall builders + a host-only AC lab solver); `diode-clippers` shipped
the passive nonlinear clipping stages (builders + a host-only transient nonlinear
lab solver). Each of those three deliverables made a discipline of introducing
**no new `Component` variant** — they composed the frozen vocabulary.

`opamp-stages` is the deliverable where that changes. It is **the first feature
to extend the frozen circuit vocabulary**, adding the one element the earlier
work deliberately deferred: the **ideal op-amp (nullor)** — the active gain
element. The shipped `diode-clippers` record names this dependency explicitly:
*"Op-amp-feedback clippers (Tube Screamer topology) are out of scope, blocked on
the deferred ideal-op-amp / nullor element (the separate `opamp-stages` Phase-4
deliverable)."* This deliverable removes that block.

The one concept (Constitution XI): **the op-amp gain stage — an active element
that drives its output until its two inputs sit at the same voltage (the virtual
short), so the surrounding feedback network — resistive, reactive, or
diode-loaded — sets the stage's gain and character.**

### What this feature adds over what already shipped

1. **A new vocabulary element — the ideal op-amp (nullor).** Every prior
   Phase-4 deliverable composed only R / C / L / sources / Diode. This one adds
   the active element. It is the first, and deliberately minimal, extension of
   the frozen vocabulary.
2. **A new, bounded solver capability — nullor MNA augmentation.** An ideal
   op-amp is not a conductance; it is a *constraint plus an unknown branch
   current* (see Solution space). Realizing it honestly requires one extra
   unknown and one extra constraint row — the first taste of Modified Nodal
   Analysis, added in bounded form to a new host-only lab solver.
3. **The active-stage builder vocabulary** the later named-pedal / named-amp
   features (`tube-screamer`, `rat-distortion`, `neve-preamp`) compose — exactly
   as tone-stacks' FMV/Baxandall and diode-clippers' shunt/series builders are
   composed downstream.

### Why an ideal op-amp is the right v1 model (and rails/GBW/slew are not)

An **ideal op-amp is a nullor**: virtual short (`V+ = V-`), infinite input
impedance (zero input current), zero output impedance, infinite open-loop gain.
Crucially it is **linear**. That linearity is load-bearing for three reasons,
each of which a more "capable" model (finite GBW pole, slew limiting, output
rails) actively breaks:

- **Phase-boundary integrity.** Output rails add a *second* nonlinearity; a
  finite GBW pole + slew add *dynamic nonlinear* behavior. Those are precisely
  what the still-blocked Phase-5 nodes exist to teach —
  `design:primitive/newton-iteration`, `design:primitive/implicit-integration`,
  `design:primitive/modified-nodal-analysis`. Building them inside a Phase-4
  deliverable pulls un-earned work forward and hollows those nodes out.
- **The nonlinearity-count invariant.** A *linear* op-amp keeps an
  op-amp+feedback-diode clipper at **exactly one** nonlinearity (the diodes), so
  it composes directly with the bounded per-timestep Newton solver
  `diode-clippers` already shipped and validated. Add rails and that same
  flagship topology has **two** interacting nonlinearities — forcing the bounded
  solver either to explode into a general Newton/MNA engine (Phase 5) or to
  *refuse the exact topology this feature exists to enable*. The real Tube
  Screamer is designed so its soft feedback-diode clipping keeps the signal well
  inside the rails: the rails add near-zero fidelity for maximum solver cost.
- **Closed-form validation.** An ideal nullor validates against exact analytic
  gains (`1 + Rf/Rg`, `-Rf/Rin`) and analytic reactive responses to ~1e-9. A
  GBW+slew+rail macromodel has *no* clean closed form; it would have to be
  validated against transcribed datasheet behavior — exactly the
  transcribed-published-numbers trap the project's circuit-model-validation
  approach avoids. More capability here means *weaker* correctness confidence.

Deferring non-ideality is cheap and non-breaking because the vocabulary is
solver-neutral: the `OpAmp` element can gain `Vsat` / `GBW` / `slewRate` fields
later with ideal defaults, and a Phase-5 solver honors them while the Phase-4
lab solver continues to treat the op-amp as ideal. Deferral is *capture*, not
deletion (see Open questions).

### Constraints (from the acfx constitution and repo conventions)

- **Solver-neutral builder in the primitive; solve in the lab.** The builder
  emits topology only (a `Netlist` + port node ids) — no solved response, no
  `process()`, no audio path. Mirrors tone-stacks / diode-clippers and keeps the
  primitive free of any solver worldview so Phase-5 MNA / Phase-6 WDF adapt the
  *same* builders unchanged.
- **Minimal vocabulary extension.** Exactly one new `Component` inhabitant (the
  ideal `OpAmp`). No rails, no GBW, no slew, no finite-gain field in v1 — the
  element is the pure nullor.
- **RT-safe primitive:** the builder is a pure function; fixed compile-time
  `Netlist` capacities; zero heap. A parameter change is a fresh builder call
  (control-rate rebuild), as in the siblings.
- **Platform-independent, C++17** primitive (Teensy-compatible); the lab may use
  C++20 and is host-only, non-normative.
- **No fallbacks / no mock data outside tests:** an ill-posed BOM/topology
  (floating input, missing feedback path, no DC path to ground) raises a
  descriptive error at build/`prepare()`; a singular augmented system or Newton
  non-convergence yields a reported status + residual, never a silent fallback
  or fabricated output. In particular, the op-amp is **never** approximated as a
  large-but-finite-gain source — that is the gmin fallback `sources.h` forbids.
- **Small modules:** headers within ~300–500 lines; split by concern (the
  diode-clippers Tier-2 test was split for exactly this budget).

### Boundary — deferred to later phases / features (progressive-learning discipline)

- **No realtime `process()` / audio path.** As with the siblings, the realtime
  realization is deferred to a later effect feature and the Phase-5 real solve;
  the lab solver is validation scaffolding, not a shippable RT DSP path.
- **No op-amp non-ideality** (finite gain, output rails/saturation, finite GBW
  pole, slew rate, input offset/bias current). Captured as the non-ideality axis
  a nonideal follow-on / Phase-5 solver inherits (Open questions), not built now.
- **General MNA stays Phase 5.** The lab solver gains *only* the bounded
  nullor augmentation (one row / one column per op-amp), never a general MNA
  rewrite. It never grows into the Phase-5 engine — and it deliberately does not
  modify the existing `component-abstractions` `LinearSolver`, whose own charter
  forbids growing into MNA.
- **Named-product BOMs / voicings** (TS808, RAT, Neve) are their own downstream
  `design:feature/*` roadmap nodes; this feature ships the generic canonical
  op-amp-stage topologies those features compose.

---

## Solution space

Four orthogonal choices, each with the chosen option and the rejected
alternatives (and why each was rejected). The operator selected the ideality
level (ideal nullor), the exemplar set (four), and the solver technique (bounded
nullor MNA augmentation) during the in-session brainstorm, delegating the
internal technique details (validation ladder, layout, test tiers) to this design
frontend with the reasoning recorded below.

### Chosen — Op-amp ideality: ideal op-amp (nullor), linear

The v1 element is the pure nullor — virtual short, infinite input impedance,
zero output impedance, infinite gain, **linear**. Non-ideality is captured for a
later deliverable, not built now (rationale above and in Decisions D1).

### Rejected ideality — Ideal op-amp + output rail saturation

Add hard clipping against ±Vsat rails. **Rejected:** rails introduce a *second*
nonlinearity, so an op-amp+feedback-diode clipper acquires two interacting
nonlinearities and crosses the diode-clippers ≥2-nonlinearity refusal line into
Phase-5 — for a non-ideality the flagship Tube Screamer topology is specifically
designed never to reach (soft feedback clipping keeps the signal inside the
rails). Worst-of-both: full second-nonlinearity solver cost, near-zero fidelity
gain.

### Rejected ideality — Full nonideal macromodel (finite gain + GBW pole + slew + rails)

Model a real TL072/4558: finite open-loop gain, single-pole GBW roll-off, slew
limiting, output rails. **Rejected:** pulls substantial Phase-5 dynamic-systems
and general-nonlinear-solve work forward, hollowing out the blocked
`newton-iteration` / `implicit-integration` / `modified-nodal-analysis` nodes;
and it has no clean closed-form validation, forcing transcribed-datasheet checks
the project's validation approach rejects. (A *finite-gain-only* linear VCVS was
weighed as a middle ground — still linear, no extra nonlinearity — but costs
validation cleanliness, `A/(1+Aβ)` instead of exact `1/β`, for fidelity the
immediate consumers do not exercise; deferred with the rest of the non-ideality
axis.)

### Chosen — Four exemplar builders (a validation ladder across all axes)

The op-amp-stage variation axes are **feedback sign** (inverting vs
non-inverting), **feedback content** (resistive→linear vs diode→nonlinear), and
**reactive feedback** (a cap in the loop → active filter). v1 ships four
exemplars structured as a validation ladder:

1. **Non-inverting gain** (linear, resistive) — closed form `1 + Rf/Rg`; the
   Tube Screamer input stage.
2. **Inverting gain** (linear, resistive) — closed form `-Rf/Rin`; isolates the
   feedback-sign axis.
3. **Active first-order stage** (linear, reactive — inverting integrator /
   op-amp low-pass) — nullor **+ reactive companions**, validated against its
   analytic step / frequency response. Isolates the reactive-feedback axis and,
   critically, validates nullor+reactive *before* any nonlinearity enters.
4. **Op-amp + feedback-diode clipper with feedback cap** (nullor + reactive +
   **one** nonlinearity) — the flagship TS808 clipping core, the topology this
   feature exists to unblock.

Exemplar 3 earns its place on **validation hygiene**, not merely coverage:
proving nullor+reactive exact against a closed form before #4 adds the diode
means a fault in #4 is isolated to the Newton coupling, not the nullor or the
reactive layer. Its marginal cost is near zero — #4 already forces the solver to
carry nullor + reactive companions, so #3 exercises the linear subset of
machinery built anyway. This honors capture-over-YAGNI: the concept's full
canonical variation is built now, and the flagship exemplar *is* the real TS
network rather than a stripped one.

### Rejected exemplar set — Two exemplars (one linear gain + the clipper)

Ship only a single linear gain stage plus the op-amp+diode clipper. **Rejected:**
pins the feedback-sign axis, omits the reactive-feedback validation stepping
stone, and under-delivers versus the ≥2-distinct-axes anti-over-fit bar
tone-stacks and diode-clippers set. The reactive+nullor combination would ship
validated *only* through the nonlinear exemplar, muddying the proof.

### Rejected exemplar set — Three exemplars (two linear gains + the clipper, no reactive stepping stone)

Non-inverting gain, inverting gain, and the op-amp+diode+cap clipper.
**Rejected:** the clipper still forces nullor+reactive machinery, but without the
linear reactive exemplar (#3) that combination is never validated against a clean
closed form before the nonlinearity enters — losing the validation-ladder
isolation for essentially no cost saving. Folding the reactive validation into
the nonlinear exemplar is exactly the muddying the ladder is designed to avoid.

### Chosen — Bounded nullor MNA augmentation in a new host-only lab solver

The lab solver realizes the nullor honestly: **one extra unknown** (the norator
output branch current) and **one extra constraint row** (`V(inPlus) -
V(inMinus) = 0`) per op-amp, appended to the reduced nodal system. For the
nonlinear exemplar this is wrapped in the same separated timestep/Newton
structure `diode-clippers` established (reactive companions once per step; Newton
linearizes the diode inside; history advanced once after convergence). Because
the op-amp is linear, the clipper carries one nonlinearity and reuses the proven
bounded-Newton pattern. This is a **new** solver in `core/labs/opamp-stages/`; it
does not modify the existing `LinearSolver` (whose charter forbids MNA growth).

### Rejected solver — Finite-gain VCVS with a huge `A` (avoid the augmentation)

Approximate the op-amp as a voltage-controlled voltage source with a
large-but-finite gain and stamp it as an ordinary controlled source.
**Rejected on the no-fallback rule:** this is the large-but-finite-conductance
gmin approximation `sources.h` explicitly forbids ("never by substituting a
large-but-finite conductance"). It is numerically fragile (ill-conditioning as
`A → ∞`) and hides, rather than honestly imposes, the virtual-short constraint.

### Rejected solver — Grow the existing `component-abstractions` LinearSolver into MNA

Extend the shipped `LinearSolver` to carry the augmentation in place.
**Rejected:** that solver's own documented charter is that it "must never grow
into MNA"; it is deliberately-naive fixed-node-reduction scaffolding. The bounded
augmentation belongs in a new opamp-stages lab solver, mirroring how
diode-clippers added `transient-clipper.h` rather than mutating `LinearSolver`.

---

## Decisions

- **D1 — Op-amp ideality: ideal nullor, linear.** The v1 `OpAmp` element is the
  pure nullor (virtual short, infinite input impedance, zero output impedance,
  infinite linear gain). No rails, GBW, slew, finite gain, or offset. This keeps
  the feature inside the Phase-4 boundary, keeps an op-amp+feedback-diode clipper
  at exactly one nonlinearity (composes with the shipped bounded-Newton solver),
  and preserves exact closed-form gain validation. (Operator decision, in-session
  brainstorm.)

- **D2 — New vocabulary element `OpAmp { NodeId inPlus, inMinus, out }`.** Added
  to the `Component` variant in `components.h` — the first and deliberately
  minimal extension of the frozen vocabulary. Like `VoltageSource` it is a
  *constraint*, not a conductance: it exposes no `admittance()` and no
  `companion()`. The classifiers are extended so `isNonlinear` / `isReactive` are
  both false for `OpAmp` and `isLinear` is true (the same bucket sources land in,
  for solver-routing purposes). `terminalsOf` / `contributesConductivePath` in
  `netlist.h` are extended to recognize it (an op-amp output does not by itself
  guarantee a DC path, mirroring the current-source / diode exclusion — the
  feedback network provides the path).

- **D3 — Builders are pure functions over a BOM value-struct, returning
  `{ Netlist, inNode, outNode }`.** No retained state, no heap, fixed
  compile-time `Netlist` capacities. Resistors, caps, diodes, and the op-amp are
  assembled from the (now-extended) vocabulary. Bad BOM / floating input /
  missing feedback path throws `std::invalid_argument` on the build thread
  (mirrors tone-stacks `requirePositive` / diode-clippers config validation).

- **D4 — Four v1 exemplar builders: non-inverting gain, inverting gain, active
  first-order (reactive), op-amp+diode+cap clipper.** The validation ladder of
  the Solution space. All four ship in v1 so every variation axis is exercised
  and nullor+reactive is proven exact before the nonlinearity enters. The generic
  op-amp-stage vocabulary the named features compose carries its full canonical
  variation (capture-over-YAGNI).

- **D5 — New host-only lab solver with bounded nullor MNA augmentation.**
  `core/labs/opamp-stages/solver/`:
  - **Nullor augmentation:** append one unknown (norator output current) and one
    constraint row (`V+ - V- = 0`) per op-amp to the reduced nodal system; solve
    the augmented system by the same fixed-size Gaussian elimination with partial
    pivoting the lab already uses. A singular augmented system (ill-posed
    feedback) is a descriptive throw, never a silent wrong answer.
  - **Linear stages (exemplars 1–3):** a single augmented solve per step;
    exemplar 3 adds reactive companions (backward Euler, reusing the primitive's
    `capacitor.h` / `inductor.h` `companion()` hooks) with history advanced once
    per step.
  - **Nonlinear stage (exemplar 4):** the separated timestep/Newton structure
    from `diode-clippers` — reactive companions computed once per step from held
    history; inner Newton holds companions fixed, linearizes the diode string
    into a Norton pair, solves the augmented linear system (now carrying the
    nullor rows too), damps the junction voltage via `Diode::limitJunctionVoltage`
    (pnjlim), tests `|Δv| < tol`; after convergence, advance reactive history
    once. Reports `NewtonStatus { converged, iterations, voltageResidual,
    currentResidual }`; non-convergence is a first-class, test-surfaced contract,
    never swallowed.
  - **Bounded, not general MNA — three enforceable tripwires** (making "nullor
    augmentation only" a *checkable* property, as diode-clippers encoded its
    bound rather than merely intending it): (i) the branch-current augmentation
    code path is **`OpAmp`-specific** — `VoltageSource` stays on fixed-node
    reduction, R/C/L stay nodal/companion; if the solver ever needs to
    branch-augment anything *other* than an `OpAmp`, that is the
    becoming-general-MNA signal and it stops; (ii) diode-clippers'
    **single-nonlinearity-location refusal carries forward unchanged** — ≥2
    interacting nonlinearities is still a descriptive throw, never a feature;
    (iii) **one row / one column per op-amp, sized at template instantiation** —
    no dynamic growth. Templated capacities; the augmentation is *only* the
    nullor stamp. It never becomes the Phase-5 engine and never modifies
    `LinearSolver`.

- **D6 — Reactive discretization is backward Euler, non-normative.** Reusing the
  primitive's existing companion hooks; explicitly the lab's naive choice (as in
  the siblings). Trapezoidal / the general treatment is Phase-5 implicit
  integration; the primitive never hard-codes one.

- **D7 — Validation ladder: prove exact first, then invariants.**
  (a) Linear resistive stages match the analytic gains (`1 + Rf/Rg`,
  `-Rf/Rin`) to ~1e-9 — the nullor stamp is exact.
  (b) The active first-order stage matches its analytic backward-Euler RC step
  response and/or first-order frequency response to ~1e-9 — nullor+reactive exact
  before any nonlinearity.
  (c) The clipper's DC steady-state limit matches an independent bisection
  root-find oracle (its own KCL equation at the virtual-short node) to ~1e-6 — a
  genuine cross-check, not solver-vs-itself.

- **D8 — Assembled-stage invariants** (the behavior the closed forms cannot
  show for the nonlinear stage): the clipper's forward saturation clamps near the
  feedback-diode drop for large input; symmetry / asymmetry per the diode
  population; passivity of the passive sub-network; and the reactive signature —
  for fixed excitation and drive, increasing the feedback cap `Cf` monotonically
  reduces post-clip high-frequency output energy (the TS "soft" clipping band).
  The measurement caveat (compare output HF energy above a fixed band with
  excitation and drive held constant) is a spec-time detail (OQ3).

- **D9 — Two test tiers + a mirroring harness** (sibling convention). Tier-1
  (`opamp-stage-builder-test.cpp`, no solver): builders produce `prepare()`-valid
  netlists at representative BOMs, contain only vocabulary elements (now including
  `OpAmp`), counts match BOM, no lab include. Tier-2
  (`opamp-stage-solve-test.cpp`, possibly split for the ~500-line budget): the
  validation ladder (D7) + the assembled-stage invariants (D8) + an explicit
  **non-convergence test** for the nonlinear exemplar (starve the iteration budget
  against a stiff edge, assert the returned `NewtonStatus` reports
  `converged == false` with its residual). A host-only `int main()` harness
  (`opamp-stages-harness.cpp`) mirrors the Tier-2 assertions as PASS/FAIL prints
  with measured-vs-expected numbers and exits nonzero on any failure.

- **D10 — Templated fixed capacity, per-builder aliases.** Each builder returns a
  `Netlist<MaxNodes, MaxComponents>` sized for its topology; the augmented system
  is sized `nodes + (nonground reductions) + numOpAmps` and, for the nonlinear
  exemplar, `+ 2·MaxDiodes` for the per-iteration diode companion stamps —
  heap-free by construction, no global ceiling (sibling convention).

- **D11 — Naming (descriptive, no numeric prefixes).** Spec dir `opamp-stages`;
  primitive subfolder `circuit/opamp-stage/` with `opamp-stage.h` +
  `opamp-config.h`; lab under `labs/opamp-stages/` with
  `solver/opamp-stage-solver.h` (augmentation) and, for exemplar 4, the transient
  Newton coupling; harness `harness/opamp-stages-harness.cpp`; lab namespace
  `acfx::labs::opamp_stages`; harness target `acfx_lab_opamp_stages_harness`;
  tests `opamp-stage-<aspect>-test.cpp`.

---

## Open questions

None blocks the design. The ideality level (D1), exemplar set (D4), and solver
technique (D5) were decided in-session and are recorded above so
`/stack-control:define` inherits them. The remainder are spec-time
(`/speckit-clarify`) details or the explicitly-captured non-ideality axis.

- **OQ1 — Non-ideality axis (CAPTURED, deferred, not dropped).** Finite
  open-loop gain, output rails/saturation, finite GBW single-pole roll-off, slew
  rate, input offset / bias current. The `OpAmp` element is shaped so it can gain
  `Vsat` / `GBW` / `slewRate` fields later with ideal defaults (non-breaking),
  and a Phase-5 solver can honor them while the Phase-4 lab solver keeps treating
  the op-amp as ideal. Rails specifically stay out of v1 because they add the
  second nonlinearity that pushes the flagship topology across the Phase-5 line
  (Solution space). This is the natural charter of a future
  `design:feature/opamp-nonideal` (or the named-preamp features that need it,
  e.g. `neve-preamp`). Recorded here so the boundary is a captured decision, not
  an omission.

- **OQ2 — Newton tolerance / max-iteration defaults for the nonlinear exemplar.**
  Hold the diode-clippers defaults initially (`maxIterations = 50`,
  `voltageTol = 1e-9`, `currentTol = 1e-12`); do not pre-tune for the op-amp
  augmentation. Non-convergence is a test-surfaced contract (D5/D9). Open only as
  a tuning question if a real reactive+nullor case is measured to fail to
  converge; then expose/raise the budget rather than hide the failure. A
  `/speckit-clarify` detail.

- **OQ3 — Harness `dt` / sample rate, excitation, and HF measurement band.** The
  reactive-signature invariant (D8) is pinned in shape (larger `Cf` → less
  post-clip HF for fixed excitation + drive); the numeric choices — excitation
  (edge vs swept tone), sample rate / `dt`, the HF band and the monotonic-decrease
  tolerance — are a spec-time decision (analogous to tone-stacks' 0.1 dB /
  10-pts-per-decade and diode-clippers' OQ3).

- **OQ4 — Active first-order exemplar exact form (v1 spec-time detail).** The
  reactive linear stage is an inverting integrator vs an inverting first-order
  low-pass (integrator with a parallel feedback R for finite DC gain). Pick the
  form whose closed-form oracle is cleanest and that best isolates the
  reactive-feedback axis without a trivially-unstable pure integrator. A
  `/speckit-clarify` numeric/topology detail, not a blocker.

- **OQ5 — Op-amp well-posedness: augmented-singularity is the authority; the
  connectivity scan is a conservative pre-filter.** Once the system is augmented
  with nullor rows, `contributesConductivePath` is **no longer the authority on
  well-posedness in either direction**: a node held only by the virtual short can
  be perfectly determined yet look "floating" to a nodal scan (false-positive
  rejection of a *valid* circuit), and conversely the scan can pass a circuit
  whose augmented matrix is singular. Therefore the spec fixes the authoritative
  gate as **the non-singularity of the augmented system at solve time (a
  descriptive throw)**, with `contributesConductivePath` kept as a *fast,
  conservative, nodal-only pre-filter* to which the op-amp contributes nothing
  (neither the output branch nor the virtual short — mirrors current-source /
  diode). This is safe for v1: verified that **all four exemplars avoid the
  false-positive case** — every interior node (both amps' inverting inputs, the
  active-stage summing node) has a real resistor or capacitor-companion path to a
  determined node, so the nodal-only pre-filter is sound *as shipped*. The spec
  records that this soundness is a property of these four topologies, **not** a
  general law of op-amp circuits — "the feedback network proves well-posedness"
  holds here but is not stated as universal. Confirm the pre-filter against each
  exemplar at `/speckit-clarify` time.

---

## Provenance

- **Design frontend:** `/stack-control:design` driving `superpowers:brainstorming`
  in-session, under the `stack-control-design-v1` house rules
  (capture-over-YAGNI, ≥2 solution-space alternatives, required sections,
  operator-approval marker, installation-anchored record, handoff to
  `/stack-control:define`).
- **Compass verdict at open:** `on-course` — `designing` is the legitimate next
  move from `planned` for `design:primitive/opamp-stages`.
- **Roadmap context:** the active-element deliverable of Phase 4 (Circuit
  Modeling) per
  `docs/superpowers/specs/2026-06-29-acfx-progressive-dsp-prospectus.md`;
  part-of `multi:feature/phase-circuit-modeling`; depends-on the shipped
  `multi:feature/phase-dynamic-systems`; sibling of the shipped
  `component-abstractions`, `passive-tone-stacks`, and `diode-clippers`; the
  vocabulary the later `tube-screamer` / `rat-distortion` / `neve-preamp`
  `design:feature/*` nodes compose. It removes the block the shipped
  `diode-clippers` record recorded against op-amp-feedback clippers.
- **Repo conventions consulted:** the shipped `diode-clippers` feature as the
  structural template (builder in `core/primitives/circuit/diode-clipper/` +
  host-only transient nonlinear lab solver + two-tier tests + harness;
  `docs/superpowers/specs/2026-07-04-diode-clippers-design.md`); the
  `passive-tone-stacks` builder + AC-solver shape; the `component-abstractions`
  vocabulary (`core/primitives/circuit/models/`, `components.h` classifiers,
  `netlist.h` `terminalsOf` / `contributesConductivePath`, `sources.h`
  VoltageSource-as-constraint and the explicit gmin/large-conductance-fallback
  prohibition) and its lab `LinearSolver`
  (`core/labs/component-abstractions/solver/linear-solver.h`, fixed-node
  reduction, the documented "must never grow into MNA" charter) and
  `NewtonClipper`; the three-layer lab→primitive→effect structure; and the acfx
  constitution / CLAUDE.md standards.
- **Operator decisions during the in-session brainstorm:** (1) ideality level —
  after weighing whether more capability carried a downside, selected the **ideal
  nullor (linear)** v1 element over rail-saturation and full-macromodel
  alternatives, on the phase-boundary / one-nonlinearity / closed-form-validation
  reasoning recorded in D1 and the Solution space; (2) exemplar set — the
  **four-exemplar validation ladder** (two linear gains, one linear reactive
  stepping stone, the flagship op-amp+diode+cap clipper); (3) solver technique —
  the **bounded nullor MNA augmentation** in a new host-only lab solver, after
  the concept was taught in-session (the operator's stated goal is to *learn*
  these concepts, so the design frontend taught the nullor / MNA-augmentation
  concept and made the technique call with the reasoning visible, rather than
  asking the operator to adjudicate solver internals). The validation ladder,
  file layout, and test tiers were delegated to the design frontend and resolved
  with the reasoning recorded above.
- **Terminal handoff:** `/stack-control:define` to author the Spec Kit spec from
  this record (the `design-to-spec` transition), after the operator records the
  `design-approved:` marker on the roadmap node.
