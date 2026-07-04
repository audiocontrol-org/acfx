> ============================================================================
> ‼ acfx COMMANDMENTS (non-negotiable) ‼
> 1. COMMIT AND PUSH EARLY AND OFTEN
> 2. NO GIT HOOKS, EVER
> 3. DESCRIPTIVE NAMES, NEVER NUMERIC PREFIXES
> (acfx Constitution, Principles I-III -- `.specify/memory/constitution.md`)
> ============================================================================

# passive-tone-stacks — Design Record

**Roadmap item:** `design:primitive/passive-tone-stacks`
(part-of `multi:feature/phase-circuit-modeling`; depends-on `multi:feature/phase-dynamic-systems`, shipped).
**Second deliverable of Phase 4 (Circuit Modeling)**, following `component-abstractions`.

**Date:** 2026-07-04
**Design backend:** `superpowers:brainstorming`, driven in-session under `/stack-control:design`.
**Status:** design complete, pending operator approval → `/stack-control:define`.

One concept (Constitution XI): **the pot-driven passive frequency-shaping
sub-circuit** — assembling the frozen component-abstractions vocabulary into a
*parameterized, control-driven, reusable* network, and reading its **frequency
response**. This is the first Phase-4 deliverable that composes the vocabulary
into something musically recognizable (a Fender/Baxandall tone control).

## Problem domain

`component-abstractions` (Phase 4, first deliverable) gave the codebase a
**solver-neutral typed vocabulary** — `Resistor`, `Capacitor`, `Inductor`,
independent `VoltageSource`/`CurrentSource`, `Diode` — each a pure value that
knows only its own physics (`admittance()`, `companion(dt, state)`,
`evaluate(v)`), plus a heap-free `Netlist<MaxNodes, MaxComponents>` topology
container and a **non-normative lab reference solver** (`LinearSolver` — Gaussian
elimination + fixed-node reduction + backward-Euler reactive companions;
`NewtonClipper` — one bounded diode nonlinearity). A vocabulary of parts is not,
by itself, a *musical circuit*.

A **passive tone stack** is the classic guitar/hi-fi tone control network — the
Fender/Marshall/Vox (FMV) bass-mid-treble stack, the Baxandall/James
shelving control — built entirely from resistors, capacitors, and
**potentiometers**. It is a purely **passive, linear, reactive** network: no
diodes, no active elements. That places it squarely in the class the existing
`LinearSolver` already handles, and makes it the natural next rung on the
progressive-learning ladder.

Two things a tone stack needs that `component-abstractions` does not yet supply:

1. **The potentiometer** — the tone stack's defining control. `component-abstractions`
   froze its v1 vocabulary at *fixed* two-terminal elements (OQ4 deferred
   controlled sources / op-amp / nullor). A pot is a *variable* resistance whose
   split depends on a mechanical position and a **taper law** (linear vs.
   audio/log). This is the one genuinely new piece of physics content.
2. **Frequency-response reading** — a tone stack's identity *is* its magnitude
   curve (the recognizable FMV mid-scoop). The lab today validates only
   time-domain (backward-Euler) step responses; there is no way to read a
   network's `H(f)`.

**The three audiences** (matching the platform's framing, as in
`component-abstractions` / `svf-vertical-slice`):

- the **circuit-model author** — assembles a named tone stack from a bill of
  materials and pot positions, and expects a `Netlist` describing the circuit's
  physics *without committing to a solver*;
- the **solver author** — who, in Phase 5 (MNA) and Phase 6 (WDF), will consume
  the *same builders* as an adapter target;
- the **lab reader** — learning how a real tone control is modelled as a
  passive network, and seeing its frequency response actually computed and
  validated against the published (Duncan-style) analytic transfer function.

### Constraints (from the acfx constitution and repo conventions)

- **Platform-independent core, thin adapters** (Constitution IV). The builder is
  portable primitive code (C++17, header-only, standard-library only); it knows
  nothing of JUCE/libDaisy/Teensy and includes nothing under `core/labs/`.
- **No fallbacks / no mock data** (Constitution, repo standard). Missing or
  malformed input raises a descriptive error; there is no silent repair, no
  fabricated response. The pot end-resistance floor is a *modelled physical
  value*, documented as such — not a bug-hiding clamp.
- **Real-time safety** (Constitution). Not directly exercised here (this
  primitive has no audio-callback path — see boundary), but the builder is
  heap-free by construction: fixed compile-time `Netlist` capacities per
  topology, no `new`/`delete`, no `std::vector`.
- **Strict typing, small modules** (~300–500 lines). Header-only, no `any`, no
  unchecked casts.
- **Progressive learning, one concept at a time** (Constitution XI). This
  deliverable adds the pot + frequency response; it does not touch Phase-5 MNA
  or Phase-6 WDF, and does not un-freeze the component vocabulary.

### Boundary — deferred to later phases / features (progressive-learning discipline)

- **No real-time realization.** No `process()` filter, no bilinear/state-space
  lowering, no WDF. The deliverable is a topology **builder** only; realtime
  realization is a captured future concern (Phase-6 WDF, or a dedicated
  lowering) — see Solution space "Rejected boundary".
- **No new solver machinery beyond a linear complex `.ac` solve.** The lab gains
  a complex-valued linear solve (standard SPICE `.ac`); it does **not** gain
  MNA, Newton, or trapezoidal integration. The `component-abstractions` "lab
  must never grow into MNA" boundary carries forward verbatim.
- **Named products are later `design:feature/*` items.** `fender-tone-stack`,
  `big-muff`, `neve-preamp`, etc. are separate roadmap features built *on* this
  primitive. This primitive ships the shared machinery + two *worked exemplars*
  that prove it; it does not ship the full named-product BOMs, voicings, or UI.

## Solution space

Per the stack-control house rule **capture over YAGNI**, the alternatives below
are recorded even where rejected; the minimal-cut scoping is the spec's job, not
this record's.

### Chosen — Solver-neutral topology builder + build-time pots + complex `.ac` validation

Four coupled decisions:

- **Deliverable boundary = solver-neutral builder.** Parameterized builder
  functions take a bill-of-materials + pot positions and emit a `Netlist` of
  frozen-vocabulary `Resistor`/`Capacitor`/`VoltageSource` components. Topology
  out; **no** solved response, **no** audio path. Mirrors `component-abstractions`
  discipline exactly — physics/topology is produced, a solver consumes it — so
  Phase-5 MNA and Phase-6 WDF later adapt the *same* builders.

- **Potentiometer = build-time parameterization.** A pot is *not* a new
  vocabulary element. A wiper function maps `(rTrack, pos∈[0,1], taper)` to two
  ordinary `Resistor` legs (`rTop + rBottom == rTrack` for a 3-terminal divider;
  one leg for a 2-terminal rheostat). The frozen v1 vocabulary is **unchanged**;
  the pot concept — taper laws + wiper math — lives entirely in this primitive.
  A control change = rebuild the netlist (control-rate, appropriate for a
  topology builder).

- **Frequency probe = complex AC analysis.** A small complex-valued linear solve
  in the lab stamps each component's admittance at `jω` (`R→1/R`, `C→jωC`,
  `L→1/(jωL)`) and Gaussian-eliminates over `std::complex<double>`, reusing the
  existing solver's structure. Yields the **exact continuous-time** `H(jω)` for
  any passive netlist the builder emits — topology-general, no hand-derived
  `H(s)` — and matches the textbook (Duncan) tone-stack curve directly. This is
  standard SPICE `.ac`; it is **not** MNA/Newton, so it respects the lab
  boundary.

- **v1 worked exemplars = FMV + Baxandall (two distinct topologies).** The FMV
  (Fender/Marshall/Vox) 3-band interacting guitar stack **and** the passive
  Baxandall/James 2-band shelving stack. Two genuinely different shapes so the
  builder abstraction is not over-fit to one topology — the same anti-over-fit
  reasoning `component-abstractions` used to retain the inductor `L`. Value-variant
  stacks (Marshall/Vox = FMV re-valued) and other topologies (Big Muff, etc.)
  are captured for later `design:feature/*` items.

**Structure (portable primitive vs. host-only lab):**

```
core/primitives/circuit/tone-stack/     PORTABLE, header-only, C++17, frozen-vocabulary only
    taper.h        Taper{Linear,Log,Antilog}; WiperSplit; wiper(); rheostat()
    tone-stack.h   toneStackFMV(FMVValues, FMVControls, Taper) -> Netlist<N,M>
                   toneStackBaxandall(...) -> Netlist<N,M>       (NO solve, NO process)

core/labs/passive-tone-stacks/          HOST-ONLY, non-normative
    solver/ac-solver.h   complex Gaussian elim; solveAC(netlist, ω, in, out) -> complex H
    harness/...          |H(f)|,∠H(f) for FMV + Baxandall vs analytic (Duncan) curves
    README.md            the load-bearing-boundary note (mirrors component-abstractions)
```

**Isolation guarantee** (mirrors `component-abstractions` SC-007): deleting
`core/labs/passive-tone-stacks/` leaves the `tone-stack/` primitive and its own
value/topology tests compiling; only the assembled-response validations go away.
The builder has no lab include; the solver reads each component's admittance and
never re-derives its constitutive law (the FR-006 seam, carried into the
frequency domain).

### Rejected boundary — Builder + real-time realization

Ship the builder **plus** a `process()` audio-path realization (lowering the
passive network to a sample-by-sample filter). *Rejected for v1:* pulls the
model→realtime lowering concern forward out of Phase 6 (WDF is the platform's
sanctioned realtime-analog path), violating one-concept-at-a-time and
duplicating work WDF will do properly. Captured as a future concern.

### Rejected pot model — New first-class `Potentiometer` element

Add a 3-terminal parameterized `Potentiometer` (position + taper) to the circuit
vocabulary and teach `Netlist`/solver to expand it. *Rejected:* un-freezes the
v1 set `component-abstractions` deliberately closed (OQ4), and injects a
3-terminal element into a 2-terminal-only vocabulary plus a solver expansion
step — real cost for no benefit, since a pot at a fixed position *is* exactly two
resistors. Build-time math is strictly simpler and keeps the seam clean.

### Rejected frequency probe — Swept-sine on the existing time-domain solver

Reuse `LinearSolver` verbatim: drive a sinusoid per frequency, settle, measure
steady-state amplitude/phase (Goertzel). *Rejected:* the measured response is
**backward-Euler-warped**, so it must be validated against the BE-discretized
expectation rather than the textbook continuous curve — a less direct match to
the recognizable tone-stack shape — and it is slow (settle per frequency). Adds
little machinery but buys a weaker, indirect validation.

### Rejected frequency probe — Analytic `H(s)` only, no solve

Hand-derive each stack's closed-form `H(s)` and check the builder feeds it
correctly; never solve the netlist. *Rejected:* exercises no solver, so it proves
only that component *values* are placed right, not that the assembled netlist
solves. Fragile and per-topology (a new hand derivation for every stack), the
opposite of the topology-general `.ac` probe.

### Rejected v1 scope — FMV only

Ship a single worked topology. *Rejected:* the builder abstraction would be
proven against only one shape — precisely the over-fit risk the `L`-precedent
warns against. Two distinct topologies cost little more and guard the
abstraction.

### Rejected v1 scope — Generic assembler + one example, no named builders

Ship only `passiveToneStack(topologySpec)` with one worked example and push every
named stack (incl. FMV) fully into later features. *Rejected:* "a tone stack"
with no named exemplar is abstract and hard to validate against a *known* curve;
a named FMV/Baxandall gives a concrete, published transfer function to grade
against.

## Decisions

- **D1 — Builder is portable primitive code**, not lab code
  (`core/primitives/circuit/tone-stack/`). It depends only on the frozen
  vocabulary + `Netlist`; no `core/labs/` include. Rationale: Phase-5/6 solvers
  adapt the same builders, so they must live in the portable core.
- **D2 — Pot = build-time wiper/taper math** emitting frozen-vocabulary
  `Resistor`s. The component vocabulary stays frozen exactly as
  `component-abstractions` left it.
- **D3 — Frequency validation = complex `.ac` solve** (`std::complex<double>`
  Gaussian elimination), exact continuous `H(jω)`, cross-checked against the
  independently-derived (Duncan) analytic tone-stack transfer function.
- **D4 — v1 exemplars = FMV + Baxandall**, two distinct topologies.
- **D5 — `double` / `std::complex<double>` throughout the lab**, consistent with
  `component-abstractions` ("compute in double; the lab is a validation
  artifact, not the embedded hot path"). No float boundary — no audio path in
  this primitive.
- **D6 — AC solver lives in this deliverable's own lab**
  (`core/labs/passive-tone-stacks/solver/ac-solver.h`), leaving the
  `component-abstractions` lab frozen. Whether it should later *graduate* beside
  `LinearSolver`/`NewtonClipper` as shared Phase-4 machinery is OQ3.
- **D7 — Errors, never fallbacks.** Control-side: `pos∉[0,1]`, non-positive
  values → `std::invalid_argument` on the build thread. AC solver: a singular
  complex system at some `ω` → descriptive `std::runtime_error` naming `ω`,
  never a silent wrong `H` (mirrors `LinearSolver`'s zero-pivot policy).
- **D8 — Pot end-resistance floor.** Endpoints (`pos=0`/`1`) clamp each leg to a
  small modelled wiper end-resistance so a literal 0-Ω `Resistor` cannot create
  a degenerate node that trips `Netlist::prepare()`. Documented as physical, not
  a fallback. Exact value is OQ1.
- **D9 — Two-tier tests, shared assertions** (mirrors `component-abstractions`):
  Tier-1 portable primitive tests (`tests/core/`) prove wiper math + builder
  topology with **no solver**; Tier-2 lab harness + `tone-stack-ac-test.cpp`
  prove `solveAC` on RC/divider sanity networks, then FMV/Baxandall `|H(f)|,∠H(f)`
  vs analytic at ≥3 control settings each incl. the mid-scoop. Lab-isolation is
  itself a success criterion.

## Open questions

Recorded for `/speckit-clarify`; none blocks spec authoring.

1. **OQ1 — Wiper end-resistance floor.** Exact value/form (fixed small Ω vs. a
   fraction of `rTrack`); physical realism vs. numerical safety.
2. **OQ2 — Taper law form.** Pure-exponential audio taper vs. the common
   two-segment linear approximation real pots use; and whether `Antilog` is v1
   or captured.
3. **OQ3 — AC solver home.** Stay in `core/labs/passive-tone-stacks/` (D6) vs.
   graduate beside `LinearSolver` as shared Phase-4 machinery that
   `diode-clippers` / `opamp-stages` could also consume.
4. **OQ4 — Frequency grid & tolerance.** Log-spaced grid density and the dB match
   tolerance for the analytic cross-check — tight enough to catch topology bugs,
   loose enough for honest floating-point.
5. **OQ5 — Baxandall variant.** Passive James network (v1) vs. the active op-amp
   Baxandall — the active form needs the deferred controlled-source/op-amp
   element (OQ4 of `component-abstractions`) and would push to `opamp-stages`.
   v1 takes the **passive** one.
6. **OQ6 — FMV output loading.** Whether the builder models the following-stage
   input impedance (`rL`) as a fixed load or exposes it as a parameter, since the
   stack's response depends on its load.

## Provenance

- **Roadmap:** `design:primitive/passive-tone-stacks`, `status: planned →
  designing` (design pointer set 2026-07-04 via `stackctl workflow link-design`).
  `part-of multi:feature/phase-circuit-modeling`; `depends-on
  multi:feature/phase-dynamic-systems` (shipped).
- **Compass:** `stackctl workflow compass … --intent design` → `on-course`
  (designing is the legitimate next move from planned).
- **Predecessor:** `component-abstractions` design
  (`docs/superpowers/specs/2026-07-03-component-abstractions-design.md`) and its
  shipped artifacts (`core/primitives/circuit/`,
  `core/labs/component-abstractions/`). This record inherits its frozen
  vocabulary, `Netlist`, and lab-isolation discipline, and its "lab must never
  grow into MNA" boundary.
- **Constitution:** Principles I–III (commandments), IV (platform independence),
  IX (lab code graduates or is retained, not discarded), XI (one concept at a
  time). `.specify/memory/constitution.md`.
- **Design method:** `superpowers:brainstorming` driven in-session under
  `/stack-control:design`, with the stack-control house rules injected
  (capture-over-YAGNI; handoff to `/stack-control:define`; installation-anchored
  record path).
- **Domain reference:** the FMV / Baxandall passive tone-control networks and
  their published (Duncan Tone Stack Calculator) analytic transfer functions,
  used as the independent validation cross-check.
- **Decisions recorded binding** from the in-session design review: deliverable
  boundary = solver-neutral builder; pot = build-time parameterization;
  frequency probe = complex `.ac`; v1 exemplars = FMV + Baxandall.
