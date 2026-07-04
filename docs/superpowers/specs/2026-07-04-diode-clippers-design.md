> ============================================================================
> ‼ acfx COMMANDMENTS (non-negotiable) ‼
> 1. COMMIT AND PUSH EARLY AND OFTEN
> 2. NO GIT HOOKS, EVER
> 3. DESCRIPTIVE NAMES, NEVER NUMERIC PREFIXES
> (acfx Constitution, Principles I-III -- `.specify/memory/constitution.md`)
> ============================================================================

# diode-clippers — Design Record

**Roadmap item:** `design:feature/diode-clippers`
**Phase:** 4 — Circuit Modeling (third feature deliverable)
**Part-of:** `multi:feature/phase-circuit-modeling`
**Depends-on:** `multi:feature/phase-dynamic-systems` (shipped)
**Layer:** primitive builders (`core/primitives/circuit/diode-clipper/`, portable,
RT-safe, header-only) + host-only lab solver/harness
(`core/labs/diode-clippers/`, non-normative)

---

## Problem domain

Phase 4 (Circuit Modeling) proceeds: `component-abstractions` shipped the
solver-neutral circuit vocabulary (R, C, L, V/I sources, Shockley `Diode`,
`Netlist<MaxNodes,MaxComponents>`) plus a deliberately-naive lab solver;
`passive-tone-stacks` shipped the first *feature* built on that vocabulary — a
solver-neutral **builder** family (FMV, Baxandall) plus a host-only AC lab solver,
with realtime deferred. `diode-clippers` is the next feature and mirrors the
tone-stacks shape: **portable builders that assemble named clipping-stage
topologies into a `Netlist`, plus a host-only lab solver that validates the
assembled behavior.**

The one concept (Constitution XI): **the diode clipping stage — a passive network
whose nonlinearity is one or more junction diodes at a single port, shaping the
signal by clamping it against the diode forward drop, with the surrounding RC
network coloring that clamping in a frequency- and program-dependent way.**

### What this feature adds over what already shipped

`component-abstractions` already built *and validated* a single-diode and an
antiparallel-pair clipper — but only as a **static transfer curve** (its lab
solver `NewtonClipper` explicitly refuses reactive elements alongside the
nonlinearity, and caps the port at two diodes). The diode-clippers feature adds,
on top of that static proof, the two things that distinguish a real clipping
stage from a memoryless curve:

1. **A named builder family** — the topologies themselves, assembled from the
   frozen vocabulary and reusable by the later named-pedal features
   (`tube-screamer`, `rat-distortion`, `big-muff`), exactly as tone-stacks'
   FMV/Baxandall builders are reused by the later named-amp features.
2. **Reactive nonlinear behavior** — a bounded **transient** lab solver
   (backward-Euler companion reactances + per-timestep Newton) that exercises the
   RC network *around* the diodes. This is the faithful analogue of what the
   tone-stacks AC solver does: tone-stacks' solver exists to validate *reactive,
   frequency-dependent* behavior; the clipper's solver validates *reactive,
   frequency-dependent nonlinear* behavior — the attack softening and
   frequency-dependent clipping a single-valued curve cannot represent.

### Why not the existing algebraic waveshapers

The `nonlinear/` layer already ships `Shape::diodeCurve` — a memoryless,
stateless algebraic approximation of diode asymmetry. Its own doc comment marks it
as explicitly *not* a circuit-solved diode clipper and defers that to this
deliverable. The circuit clipper adds **physics** (the true Shockley I–V,
numerically solved) and **state/memory** (the reactive network), capturing the
tone interaction a `float → float` curve structurally cannot.

### Constraints (from the acfx constitution and repo conventions)

- **Solver-neutral builder in the primitive; solve in the lab.** The builder
  emits topology only (a `Netlist` + port node ids) — no solved response, no
  `process()`, no audio path. This mirrors tone-stacks (FR-001/FR-017) and keeps
  the primitive free of any solver worldview so Phase-5 MNA / Phase-6 WDF adapt
  the *same* builders unchanged.
- **Frozen vocabulary (no new circuit element).** The builders compose only the
  existing `component-abstractions` inhabitants (R, C, L, V/I sources, Diode).
  No new `Component` variant is introduced.
- **RT-safe primitive:** the builder is a pure function; fixed compile-time
  `Netlist` capacities; zero heap. A parameter change is a fresh builder call
  (control-rate rebuild), as in tone-stacks.
- **Platform-independent, C++17** primitive (Teensy-compatible); the lab may use
  C++20 and is host-only, non-normative.
- **No fallbacks / no mock data outside tests:** ill-posed BOM or topology raises
  a descriptive error at build/`prepare()`; Newton non-convergence yields a
  reported status + residual, never a silent fallback or fabricated output.
- **Small modules:** headers within ~300–500 lines; split by concern.

### Boundary — deferred to later phases / features (progressive-learning discipline)

- **No realtime `process()` / audio path.** As with tone-stacks, the realtime
  realization is deferred — here to a later effect feature and the Phase-5 real
  nonlinear solve (the lab's transient solver is validation scaffolding, not a
  shippable RT DSP path). Oversampling / ADAA anti-aliasing belongs to that
  realtime effect (and ADAA is inapplicable to a Newton solve anyway — no analytic
  antiderivative).
- **Op-amp-feedback clippers (Tube Screamer topology) are out of scope**, blocked
  on the deferred ideal-op-amp / nullor element (the separate `opamp-stages`
  Phase-4 deliverable).
- **Named-product BOMs / voicings** (Rat, DS-1, Big Muff, Tube Screamer) are their
  own downstream `design:feature/*` roadmap nodes; this feature ships the generic
  canonical topologies those features compose.
- **General multi-nonlinearity / MNA** stays Phase 5. The transient lab solver
  holds a bounded line: single clipper port, a single nonlinearity *location*
  (one node pair carrying the diode string), never a general nonlinear engine.

---

## Solution space

Four orthogonal choices, each with the chosen option and the rejected
alternatives (and why each was rejected). The operator's directive — *"look at the
tone stack implementation, mirror that"* — fixes the first choice; the operator
selected the reactive lab-solver charter; the exemplar count and the solver's
history-separation structure were delegated to this design frontend.

### Chosen — Solver-neutral builder family + host-only transient nonlinear lab solver (mirror tone-stacks)

The feature is a set of pure **builder functions** in
`core/primitives/circuit/diode-clipper/`, each taking a BOM value-struct plus a
diode-population config and returning `{ Netlist, inNode, outNode }` — the exact
shape of `toneStackFMV` / `toneStackBaxandall`. A host-only lab solver in
`core/labs/diode-clippers/solver/` consumes the emitted netlist and validates the
assembled behavior. No realtime effect. The seam is identical to tone-stacks:
**the builder owns topology; the solver owns the solve.**

### Rejected boundary — Shippable realtime `process()` effect now

Ship a real oversampled diode-clipper effect (quality tiers like the saturation
effect). **Rejected:** requires a real-time nonlinear solve path, pulling Phase-5
solver work forward and crossing the phase boundary tone-stacks deliberately
respected. The progressive-learning arc introduces the realtime nonlinear solve in
its own phase; a shippable effect is a later feature that composes it.

### Rejected boundary — Static builders only (reuse the existing `NewtonClipper`)

Add named clipper *builder* topologies validated against the existing static
`NewtonClipper` transfer curve; add no new solver, no reactive behavior.
**Rejected:** it validates *less* than tone-stacks' own solver does — tone-stacks
built a new AC solver precisely to exercise the reactive behavior that is the
network's whole point. A clipper whose surrounding caps are present in the builder
but never exercised by the solver would leave the feature's defining increment
(reactive nonlinear behavior) unvalidated. The operator selected the reactive
charter for exactly this reason.

### Rejected lab-solver structure — Reuse `LinearSolver::solve()` inside the Newton loop

The obvious extension of `NewtonClipper` — keep calling the history-advancing
`LinearSolver::solve()` once per Newton iteration, now with capacitor companions
in the netlist. **Rejected (and this is *why* `component-abstractions` refused
reactive+nonlinear):** `LinearSolver` advances its reactive history on *every*
`solve()`, so calling it N times per timestep would advance capacitor history N
times per sample and corrupt the transient. The chosen structure separates the
loops (below).

### Rejected exemplar set — A single topology (over-fit risk)

Ship only the symmetric shunt clipper. **Rejected:** the ≥2-exemplar discipline
(from tone-stacks) exists to keep the abstraction and solver from over-fitting one
circuit; a single exemplar cannot demonstrate generality across either variation
axis.

### Rejected exemplar set — Two exemplars pinning one axis

Two builders (e.g. symmetric + asymmetric shunt, *or* shunt + series).
**Rejected as insufficient (design-frontend judgment):** diode clippers vary on
*two* independent axes — transfer symmetry (odd vs even harmonics + DC) and
topology / reactance placement (shunt-to-ground vs series-inline). Any
two-exemplar pair pins only one axis and leaves the other unexercised. Three
exemplars pin both while keeping each exemplar's novelty isolable for debugging.
(Retained as the fallback if scoping trims v1 — dropping the series builder yields
the symmetric+asymmetric shunt pair.)

---

## Decisions

- **D1 — Mirror the tone-stacks deliverable shape.** Solver-neutral builders in
  `core/primitives/circuit/diode-clipper/`; host-only lab solver + harness in
  `core/labs/diode-clippers/`; two-tier tests in `tests/core/`; no realtime
  `process()` effect. The primitive does not depend on the lab. (Operator
  directive.)

- **D2 — Builders are pure functions over a BOM value-struct + diode-population
  config, returning `{ Netlist, inNode, outNode }`.** No retained state, no heap,
  fixed compile-time `Netlist` capacities. Diode string, resistors, and reactive
  elements are assembled from the frozen `component-abstractions` vocabulary; no
  new `Component` variant. Bad BOM / out-of-range config throws
  `std::invalid_argument` on the build thread (mirrors tone-stacks
  `requirePositive` / `validatePot`).

- **D3 — Three v1 exemplar builders: symmetric shunt, asymmetric shunt, series**
  (design-frontend judgment; operator delegated). Symmetric shunt (matched
  antiparallel pair to ground after a series R, filter cap across the diodes → odd
  harmonics) is the baseline and the static cross-check anchor. Asymmetric shunt
  (unequal population, e.g. 2-up / 1-down → even harmonics + DC offset) isolates
  the *symmetry* axis and forces the solver past the existing two-diode cap.
  Series clipper (diodes inline in the signal path, coupling cap in series)
  isolates the *topology / reactance-placement* axis. Trimmable to two
  (drop series) at operator direction.

- **D4 — New host-only transient nonlinear lab solver with separated timestep /
  Newton loops.** `core/labs/diode-clippers/solver/transient-clipper.h`:
  - **Outer, per-timestep:** compute reactive companion values *once* from held
    history (`Geq, Ieq` via the existing `capacitor.h` / `inductor.h`
    `companion()` hooks); after Newton converges for the timestep, **advance
    reactive history exactly once**.
  - **Inner, Newton (no history advance):** hold reactive companions fixed;
    companion-linearize the diode string into a Norton pair, append it as ordinary
    linear stamps, solve the now-purely-linear system, damp the new junction
    voltage through `Diode::limitJunctionVoltage` (pnjlim), test `|Δv| < tol`.
  Reports a per-sample `NewtonStatus { converged, iterations, voltageResidual,
  currentResidual }`; non-convergence is reported, never faked (no fallback,
  no fabricated output).

- **D5 — The transient solver stays bounded and non-MNA.** Single clipper port; a
  single nonlinearity *location* (one node pair carrying the diode string); the
  diode-per-port cap is raised beyond the existing `kMaxDiodes = 2` to admit the
  asymmetric string (exact cap → OQ1). It never becomes a general nonlinear
  engine; ≥2 *interacting* nonlinearities (distinct node pairs) remain a Phase-5
  refusal, mirroring the existing lab's FR-016 line.

- **D6 — Reactive discretization is backward Euler, non-normative.** Reusing the
  primitive's existing companion hooks; explicitly the lab's naive choice (as in
  `component-abstractions` OQ1). Trapezoidal / the general treatment is Phase-5
  implicit integration; the primitive never hard-codes one.

- **D7 — Validation: prove the transient solver exact first, then invariants**
  (mirrors tone-stacks). (a) Linear-only run of the transient solver matches the
  analytic backward-Euler RC step response (~1e-9); (b) each clipper's **DC
  steady-state limit** matches an **independent bisection root-find oracle** (e.g.
  `0 = v + R·2·Is·sinh(v/nVt) − Vin` for the symmetric shunt; its own equation per
  topology) to ~1e-6 *and* agrees with the existing static `NewtonClipper` curve —
  a genuine cross-check, not solver-vs-itself.

- **D8 — Assembled-clipper invariants** (the behavior the static curve cannot
  show): symmetry (`y(−x) = −y(x)` for the symmetric clipper; explicit
  DC-offset / even-harmonic presence for the asymmetric); forward saturation
  clamps near the diode drop for large input; the **reactive signature** —
  increasing the filter cap `Cf` monotonically attenuates the high-frequency
  content of the clipped signal (attack softening / frequency-dependent clipping);
  passivity / energy bound (a passive clipper dissipates — output energy ≤ input,
  no gain).

- **D9 — Two test tiers + a mirroring harness** (tone-stacks convention).
  Tier-1 (`diode-clipper-builder-test.cpp`, no solver): builders produce
  `prepare()`-valid netlists at representative BOMs, contain only frozen-vocabulary
  elements, counts match BOM, no lab include. Tier-2
  (`diode-clipper-transient-test.cpp`): the solver sanity + assembled-clipper
  invariants above. A host-only `int main()` harness
  (`diode-clippers-harness.cpp`) mirrors the Tier-2 assertions as PASS/FAIL prints
  with measured-vs-expected numbers and exits nonzero on any failure.

- **D10 — Templated fixed capacity, per-builder aliases.** Each builder returns a
  `Netlist<MaxNodes, MaxComponents>` sized for its topology (e.g.
  `SymmetricShuntClipper = ...`), and the transient solver is templated to match —
  heap-free by construction, no global ceiling (as in tone-stacks / OQ3 of
  component-abstractions).

- **D11 — Naming (descriptive, no numeric prefixes).** Spec dir `diode-clippers`;
  primitive subfolder `circuit/diode-clipper/` with `diode-clipper.h` +
  `clipper-config.h`; lab under `labs/diode-clippers/` with
  `solver/transient-clipper.h` and `harness/diode-clippers-harness.cpp`; lab
  namespace `acfx::labs::diode_clippers`; harness target
  `acfx_lab_diode_clippers_harness`; tests `diode-clipper-<aspect>-test.cpp`.

---

## Open questions

None blocks the design; each is a spec-time (`/speckit-clarify`) detail.

- **OQ1 — Exact diode-per-port cap.** The asymmetric exemplar (2-up / 1-down)
  needs 3 diodes at the port; a general N-up / M-down population needs more. Pick a
  sane fixed cap (candidate: a small templated `MaxDiodes`, default covering the
  2:1 case) and watch the augmented-netlist / solver matrix size. Decide the
  default and whether it is templated or fixed.

- **OQ2 — Newton tolerance / max-iteration defaults for the reactive case.** The
  static `NewtonClipper` uses N=50, `voltageTol=1e-9`. A reactive network can be
  stiffer (fast edges into a small cap); confirm the defaults hold or expose them,
  and decide the non-convergence reporting contract at the harness level.

- **OQ3 — Harness `dt` / sample rate and THD / spectral-tilt measurement window.**
  The reactive-signature invariant (D8) needs a chosen excitation (swept tone or
  edge), sample rate, and measurement band. A spec-time numeric-tolerance decision
  (analogous to tone-stacks' 0.1 dB / 10-pts-per-decade clarification).

- **OQ4 — v1 exemplar count (3 vs 2).** Design-frontend call is three (D3);
  recorded as an open scoping knob so the operator / `/speckit-clarify` can trim to
  the symmetric+asymmetric shunt pair if v1 surface is to be minimized.

- **OQ5 — Series-clipper reactance placement.** The series exemplar's cap can sit
  as an input coupling cap or across the series diodes; pick the placement that
  best isolates the topology axis without duplicating the shunt exemplar's
  cap-across-diodes reactance. Spec-time detail.

---

## Provenance

- **Design frontend:** `/stack-control:design` driving `superpowers:brainstorming`
  in-session, under the `stack-control-design-v1` house rules
  (capture-over-YAGNI, ≥2 solution-space alternatives, required sections,
  operator-approval marker, installation-anchored record, handoff to
  `/stack-control:define`).
- **Compass verdict at open:** `on-course` — `designing` is the legitimate next
  move from `planned` for `design:feature/diode-clippers`.
- **Roadmap context:** third feature deliverable of Phase 4 (Circuit Modeling) per
  `docs/superpowers/specs/2026-06-29-acfx-progressive-dsp-prospectus.md` §"Phase 4
  — Circuit Modeling"; part-of `multi:feature/phase-circuit-modeling`; sibling of
  the shipped `component-abstractions` and `passive-tone-stacks`, and the vocabulary
  the later `tube-screamer` / `rat-distortion` / `big-muff` `design:feature/*`
  nodes compose.
- **Repo conventions consulted:** the shipped `passive-tone-stacks` feature as the
  structural template (builder in `core/primitives/circuit/tone-stack/` + host-only
  AC lab solver + two-tier tests + harness;
  `docs/superpowers/specs/2026-07-04-passive-tone-stacks-design.md`); the
  `component-abstractions` vocabulary and its lab
  (`core/primitives/circuit/models/diode.h` Shockley `evaluate`/`vCrit`/`pnjlim`;
  `core/labs/component-abstractions/solver/newton-clipper.h` bounded static Newton,
  `kMaxDiodes = 2`, reactive+nonlinear refusal); the `nonlinear/` waveshaper layer
  boundary (`Shape::diodeCurve` marked explicitly not a circuit-solved clipper);
  the `Oversampler` reuse note (deferred to the realtime effect); the three-layer
  lab→primitive→effect structure; and the acfx constitution / CLAUDE.md standards.
- **Operator decisions during the in-session brainstorm:** (1) deliverable
  boundary — *"look at the tone stack implementation, mirror that"* → the
  builder + host-only lab shape, no realtime effect (D1); (2) lab-solver charter —
  selected the **transient nonlinear (reactive)** solver over static-only, making
  the reactive dimension the feature's validated increment (D4/D6/D8). The exemplar
  count (D3) and the solver's history-separation structure (D4) were delegated to
  the design frontend and resolved with the reasoning recorded above.
- **Terminal handoff:** `/stack-control:define` to author the Spec Kit spec from
  this record (the `design-to-spec` transition), after the operator records the
  `design-approved:` marker on the roadmap node.
