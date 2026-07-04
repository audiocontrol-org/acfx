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
selected the reactive lab-solver charter; the solver's history-separation
structure was delegated to this design frontend; the exemplar count and the
solver's bound (templated `MaxDiodes`, Newton defaults, non-convergence contract)
were settled by the operator's design review (2026-07-04, recorded below).

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

### Chosen v1 sequencing — Two shunt exemplars first, series as the designated topology-axis closer

Diode clippers vary on *two* independent axes — transfer symmetry (odd vs even
harmonics + DC) and topology / reactance placement (shunt-to-ground vs
series-inline). The design captures three exemplars (symmetric shunt, asymmetric
shunt, series) but **ships two in v1 — symmetric + asymmetric shunt — and defers
series** (design review 2026-07-04). The reasoning is sequencing by risk: the new,
risky artifact is the **transient solver**, not the builder (the builder only
composes frozen primitives). The symmetric+asymmetric shunt pair stresses the
*solver* hardest — it forces the diode cap past the existing `kMaxDiodes = 2` and
exercises asymmetric / DC-offset convergence — so it is the correct pair to *prove
the solver*. The series exemplar is mostly a *builder-topology* change (low solver
risk); it is the designated closer of the topology/reactance-placement axis and
follows once the solver is proven.

**Honest limitation recorded:** because both v1 exemplars are shunt topologies,
v1 pins the *symmetry* axis but leaves the *topology* axis unexercised until series
lands — weaker anti-over-fit than tone-stacks' FMV+Baxandall (genuinely distinct
networks). This is an accepted, bounded trade because the abstraction at
over-fit risk (the solver) *is* exercised across the diode-count / asymmetry
dimension; the deferred series builder is tracked as the axis-closer, not dropped.

### Rejected exemplar set — A single topology (over-fit risk), or all-three-at-once

Ship only the symmetric shunt clipper (**rejected:** a single exemplar cannot
demonstrate generality across either variation axis; violates the ≥2-exemplar
anti-over-fit discipline). Or ship all three exemplars in one v1 (**rejected by
design review:** couples the unproven transient solver to the extra
builder-topology surface before the solver is validated; series is cleaner as a
fast-follow once the shunt family proves the solver).

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

- **D3 — Two v1 exemplar builders: symmetric shunt + asymmetric shunt; series
  captured and deferred as the topology-axis closer** (design review 2026-07-04,
  sequencing by solver risk). Symmetric shunt (matched antiparallel pair to ground
  after a series R, filter cap across the diodes → odd harmonics) is the baseline
  and the static cross-check anchor. Asymmetric shunt (unequal population, e.g.
  2-up / 1-down → even harmonics + DC offset) isolates the *symmetry* axis and
  forces the solver past the existing two-diode cap. This pair stresses the new
  transient solver hardest and is the right set to *prove* it. The series clipper
  (diodes inline, coupling cap in series) is captured in the design and deferred to
  the immediate fast-follow — it is the designated closer of the *topology /
  reactance-placement* axis, added once the solver is proven on the shunt family
  (not dropped; see Solution space and OQ4). v1 therefore pins the symmetry axis;
  the topology axis is exercised when series lands.

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
  no fabricated output). **The non-convergence status is a first-class,
  test-surfaced contract** (design review 2026-07-04): it must be threaded through
  to the harness/tests and observable, never swallowed — see D9. Newton defaults
  are held at the static solver's values initially (`maxIterations = 50`,
  `voltageTol = 1e-9`, `currentTol = 1e-12`); they are *not* pre-tuned for reactive
  stiffness — measure first, tune only if a real case fails to converge (OQ2).

- **D5 — The transient solver stays bounded and non-MNA.** Single clipper port; a
  single nonlinearity *location* (one node pair carrying the diode string). The
  diode-per-port cap is a **template parameter**
  `TransientClipper<MaxNodes, MaxComponents, MaxDiodes>` with **default
  `MaxDiodes = 4`** (design review 2026-07-04) — comfortably admits the 2-up /
  1-down asymmetric string with headroom; the internal augmented netlist is sized
  `MaxComponents + 2·MaxDiodes` for the per-iteration companion stamps (each diode
  linearizes to a `Resistor` + `CurrentSource`). It never becomes a general
  nonlinear engine; ≥2 *interacting* nonlinearities (distinct node pairs) remain a
  Phase-5 refusal, mirroring the existing lab's FR-016 line. This narrowness is
  load-bearing: it is what keeps the lab from becoming Phase-5 MNA by accident.

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
  clamps near the diode drop for large input; passivity / energy bound (a passive
  clipper dissipates — output energy ≤ input, no gain). The **reactive signature**
  is the pinned, load-bearing invariant (design review 2026-07-04): **for a fixed
  excitation and drive, increasing the filter cap `Cf` monotonically reduces the
  post-clip high-frequency output energy** (attack softening / frequency-dependent
  clipping). Measurement caveat to fix at spec time: because clipping itself
  generates HF harmonics, the assertion is over the *output* HF energy above a
  fixed band with excitation and drive held constant — the comparison is clean only
  when those are pinned (OQ3).

- **D9 — Two test tiers + a mirroring harness** (tone-stacks convention).
  Tier-1 (`diode-clipper-builder-test.cpp`, no solver): builders produce
  `prepare()`-valid netlists at representative BOMs, contain only frozen-vocabulary
  elements, counts match BOM, no lab include. Tier-2
  (`diode-clipper-transient-test.cpp`): the solver sanity + assembled-clipper
  invariants above, **plus an explicit non-convergence test** (design review
  2026-07-04) — drive the solver into deliberate non-convergence (e.g. a starved
  iteration budget against a stiff edge) and assert the returned `NewtonStatus`
  reports `converged == false` with the residual, proving the status is surfaced
  and never swallowed. A host-only `int main()` harness
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

None blocks the design. OQ1 and OQ4 were resolved by the design review
(2026-07-04) and are recorded here as decided so `/stack-control:define` inherits
them; OQ2, OQ3, and OQ5 remain spec-time (`/speckit-clarify`) details.

- **OQ1 — Diode-per-port cap. → RESOLVED: templated `MaxDiodes`, default 4.** The
  solver is `TransientClipper<MaxNodes, MaxComponents, MaxDiodes>` with
  `MaxDiodes = 4` by default (fits 2-up / 1-down with headroom); the internal
  augmented netlist is sized `MaxComponents + 2·MaxDiodes`. No global ceiling — a
  builder needing more instantiates larger (D5).

- **OQ2 — Newton tolerance / max-iteration defaults for the reactive case. →
  DECIDED initially, measurement-gated.** Hold the static solver's defaults
  (`maxIterations = 50`, `voltageTol = 1e-9`, `currentTol = 1e-12`); do *not*
  pre-tune for reactive stiffness. Non-convergence is a test-surfaced contract
  (D4/D9). Open only as a *tuning* question if a real reactive case is measured to
  fail to converge; then expose/raise the budget rather than hide the failure.

- **OQ3 — Harness `dt` / sample rate, excitation, and HF measurement band.** The
  reactive-signature invariant (D8) is pinned in *shape* (larger `Cf` → less
  post-clip HF for fixed excitation + drive); the remaining numeric choices —
  excitation (edge vs swept tone), sample rate / `dt`, the HF band and the
  monotonic-decrease tolerance — are a spec-time decision (analogous to
  tone-stacks' 0.1 dB / 10-pts-per-decade clarification).

- **OQ4 — v1 exemplar set. → RESOLVED: ship two (symmetric + asymmetric shunt),
  defer series.** Series is captured and is the designated topology-axis closer,
  added as the immediate fast-follow once the transient solver is proven on the
  shunt family (D3, Solution space).

- **OQ5 — Series-clipper reactance placement (deferred with the series exemplar).**
  When series lands, its cap sits as an input coupling cap or across the series
  diodes; pick the placement that best isolates the topology axis without
  duplicating the shunt exemplar's cap-across-diodes reactance. Not a v1 concern.

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
  count and the solver's history-separation structure (D4) were delegated to
  the design frontend and resolved with the reasoning recorded above.
- **Design review (2026-07-04, operator):** narrowed the lab solver to keep it from
  becoming Phase-5 MNA by accident. Resolutions folded in: (1) diode cap is a
  templated `MaxDiodes` (default 4) rather than a fixed constant (D5, OQ1);
  (2) v1 ships two shunt exemplars, series deferred as the topology-axis closer
  (D3, OQ4) — sequencing by solver risk; (3) Newton defaults held at the static
  values initially with non-convergence a first-class *test-surfaced* contract
  (D4, D9, OQ2); (4) the reactive signature pinned as the load-bearing invariant —
  larger `Cf` reduces post-clip HF for fixed excitation + drive (D8, OQ3). The
  design frontend recorded one honest limitation in response: two shunt exemplars
  pin the symmetry axis only, leaving the topology axis unexercised until series
  lands — accepted because the at-risk artifact (the solver) is still fully
  exercised across the diode-count / asymmetry dimension.
- **Terminal handoff:** `/stack-control:define` to author the Spec Kit spec from
  this record (the `design-to-spec` transition), after the operator records the
  `design-approved:` marker on the roadmap node.
