---
doc-grammar: design-record
item: design:feature/tape-dynamics
date: 2026-07-03
---

# tape-dynamics — Design Record

Capstone feature of **phase-dynamic-systems**. Teaches **nonlinearity with
memory** through a **Jiles-Atherton magnetic-hysteresis** model, plus the
**dynamic compression that emerges** from magnetic saturation — with an
*optional* explicit envelope-driven trim layered on top.

> House rules (stack-control `/stack-control:design`): capture-over-YAGNI (the
> solution space below records the full surface; first-cut *sequencing* is a
> later `/speckit-clarify` pass, not a scope cut here). Handoff target is
> `/stack-control:define`, never `writing-plans`.

## Problem domain

The platform has, through phase-nonlinear-dsp and the earlier members of
phase-dynamic-systems, shipped **static** and **level-tracking** processing:

- `saturation` — a *static* `tape` voicing (tanh + HF-loss pre/post emphasis);
  memoryless: output depends only on the instantaneous input sample.
- `program-dependent-saturation` — drive that tracks a detected envelope, but
  the nonlinearity itself is still memoryless per-sample.
- `compressors` / `envelope-followers` — level-dependent **gain** dynamics via a
  control path (`EnvelopeFollower` → `GainComputer`).

None of these captures the defining electro-magnetic behavior of tape: **magnetic
hysteresis**. In a real tape machine the magnetization `M` of the medium lags the
applied field `H` — the transfer curve is *not single-valued*; it traces an open
loop whose shape depends on the recent history of the signal. This is a
**nonlinearity with memory**, a genuinely new concept for this phase and the
natural culmination of "dynamic systems": the step beyond a static waveshaper and
beyond envelope-tracked drive.

Two consequences of the magnetics are worth teaching together:

1. **Hysteresis** — the history-dependent loop itself (the numerical-methods and
   physical-modeling lesson).
2. **Emergent dynamic compression** — as level rises, the saturating magnetics
   flatten the transfer, reducing dynamic range. This *falls out of the physics*;
   it is not a control-path compressor. Measuring and hearing it is a distinct
   sub-lesson.

**Users of this feature** (consistent with the platform's audience framing):
- the **DSP effect author / host integrator** wiring a tape-dynamics effect into
  a plugin or embedded target;
- the **primitive consumer** composing the graduated stateful `Hysteresis`
  primitive into their own processors;
- the **lab reader** learning the Jiles-Atherton model, the explicit-vs-implicit
  solver tradeoff, and the emergent-compression measurement.

### Boundary — deferred to later phases (progressive-learning discipline)

This is a *progressive* platform; a feature teaches the one new concept its phase
owns and reuses (never re-derives or front-runs) neighbors. Explicitly **out of
scope here**, with their rightful home:

| Deferred concept | Rightful home |
|---|---|
| Wow / flutter (pitch instability) | already shipped as `WowFlutterStage` (modulated-delay); *integration* belongs to `tape-machine` |
| Frequency-dependent HF / gap loss (convolution) | **phase-convolution** (FIR/FFT) |
| Bias modeling, tape noise/hiss, named-deck EQ curves, full record/repro path | **`tape-machine`** (phase-reference-hardware) |

`design:feature/tape-machine` (phase-reference-hardware, later) is the full named
reference deck; `tape-dynamics` delivers the reusable *dynamics/memory* building
block it will later specialize.

## Solution space

The scope was narrowed through the design conversation across four axes: feature
identity, hysteresis model, compression modeling, and solver surface. The chosen
design and the rejected alternatives at each axis:

### Chosen — Hysteresis-focused vertical with a layered emergent+explicit compression thread

A three-layer vertical (lab → graduated primitive → effect) centered on a
**stateful Jiles-Atherton `Hysteresis` primitive** with a **selectable numerical
solver**, composed under the shipped `Oversampler<Factor>` inside a
`TapeDynamicsEffect`. Tape compression is taught **both** as an emergent property
of the magnetics (the core, measured lesson) **and** as an optional explicit
envelope-driven trim reusing `EnvelopeFollower` + `GainComputer`.

Why: it is the one concept this phase uniquely owns (memory), maximizes reuse of
already-taught primitives (oversampler, envelope-follower, gain-computer), and
does not front-run convolution or the reference-hardware deck.

### Rejected — Integrated full tape chain (hysteresis + wow/flutter + HF-loss + compression + bias/noise)

The broadest reading of "tape dynamics." Rejected because on a progressive
platform it **pulls later-phase concepts forward**: it would duplicate the shipped
`WowFlutterStage`, pre-empt phase-convolution's frequency-dependent loss, and
absorb `tape-machine`'s reason to exist. It collapses several phases' lessons into
one feature — the opposite of progressive layering.

### Rejected — Tape compression only (no new nonlinearity)

Model tape's leveling purely as an `EnvelopeFollower` + `GainComputer` control
path voiced for tape. Rejected: it teaches nothing new — mechanically the same
lesson as the `compressors` feature — and omits the defining tape phenomenon
(hysteresis). Retained only as the *optional* explicit layer atop the physics.

### Rejected model — Simplified biased-memory nonlinearity

A one-pole-smoothed / biased waveshaper with a memory term approximating the loop
without the stiff ODE. Rejected as the *primary* model: cheaper but not physically
grounded and a much smaller teaching surface (no numerical-methods lesson, no
physical parameters). Jiles-Atherton chosen instead as the industry-standard
audio tape model (Chowdhury, DAFx 2019 / CHOW Tape).

### Rejected model — Preisach hysteresis

Hysteresis as a superposition of elementary relay (hysteron) operators.
Conceptually elegant and general but heavier state and far less common in audio
tape emulation than Jiles-Atherton; a weaker fit for the canonical lesson.

### Rejected solver scope — Single / two fixed solvers

Shipping only RK4 (or RK4 + Newton). Rejected as the captured surface: the
selectable-solver menu **is** a core teaching axis (explicit vs implicit, order of
accuracy, stability under stiffness/oversampling). Capture-over-YAGNI keeps the
full `Solver` surface (RK2 / RK4 / Newton-Raphson); which solver(s) land in the
first implementation cut is a `/speckit-clarify` sequencing decision, not a design
cut.

## Decisions

**D1 — Three-layer vertical, following the shipped pattern.**
```
core/labs/tape-dynamics/        README (theory) + host-only measurement harness + RT kernel
core/primitives/nonlinear/hysteresis.h   graduated stateful Jiles-Atherton primitive
core/effects/tape-dynamics/     core / effect / parameters / presets
```

**D2 — `Hysteresis` is the first *stateful* member of `nonlinear/`.** It lives
alongside the stateless waveshaper family and is explicitly contrasted with
`adaa-waveshaper` in the README: ADAA (antiderivative antialiasing) cannot replace
oversampling here because state carries across samples — hence oversampling is the
antialiasing route for the magnetics.

**D3 — Jiles-Atherton model, five physical parameters.** `Ms` (saturation
magnetization / ceiling), `a` (anhysteretic shape), `α` (inter-domain coupling),
`k` (coercivity → loop width / memory), `c` (reversibility → loop openness).
Anhysteretic curve via the Langevin function `L(x)=coth(x)−1/x`, with the
irreversible + reversible split.

**D4 — Selectable solver surface.** A runtime `Solver` enum: **RK2**, **RK4**
(explicit), **Newton-Raphson** (implicit). First-ship ordering deferred to
`/speckit-clarify`.

**D5 — Compose the shipped `Oversampler<Factor>` verbatim.** The per-sample JA
integration step is passed as the `evalAtHighRate` callable to
`Oversampler<Factor>::process(x, eval)`. This is the composition seam; no new
oversampling code.

**D6 — Layered compression.** Emergent saturation-compression is the core,
*measured* lesson (no control path). An **optional** explicit trim
(`EnvelopeFollower` → `GainComputer`) is offered on top; its sequencing/first-cut
inclusion is a `/speckit-clarify` decision.

**D7 — Host-facing effect parameters (macros → physics).** `drive` (input gain
into the magnetics), `saturation`/`ceiling` (→ `Ms`), `width` (→ `k`), `solver`,
`oversampling` (factor select), optional `trim.{enabled,attack,release,amount}`,
`mix` (dry/wet), `output` (makeup). Emergent compression is **not** a parameter —
it is inherent and exposed through the level curve + lab measurement.

**D8 — RT-safety & platform independence (Constitution IV/VI).** All state
preallocated in `prepare()`; `process()` is zero-heap, lock-free, O(1) bounded.
Standard library only — no JUCE/libDaisy/Teensy in core. `reset()` clears
`M`/history state.

**D9 — Stiff-solver stability guard.** The ODE state (`M`, previous `H`, `dH/dt`)
is clamped / deNaN'd so a hot transient cannot blow the integrator to NaN/Inf; the
guard resolves to a defined, stable state (consistent with the platform's existing
guard conventions), never propagating NaN.

**D10 — Effect conforms to the platform `Effect` concept.** `prepare(ProcessContext)`
/ `process(AudioBlock)`, matching `saturation` / `compressor` / `svf` siblings.

**D11 — Host-only validation, no hardware/DAW dependency.** Reuses the analysis
harness (`host/analysis/thdn.h`, `alias-sweep.h`, etc.).

## Testing & validation strategy

- **Hysteresis loop (proves memory):** a sinusoidal `H` traces a *closed loop*
  in `M`-vs-`H` with area > 0; a static waveshaper would trace a single-valued
  curve (area ≈ 0). This is the defining pass/fail for "nonlinearity with memory."
- **Solver agreement & stability:** RK2 / RK4 / Newton converge to the same loop
  within tolerance as oversampling increases; none diverge on a hot transient
  (D9 guard holds; output stays finite).
- **Emergent compression:** input-vs-output level curve is monotonic and
  compressive above a threshold; a dynamic-range-reduction metric increases with
  `drive` — with **no** explicit trim engaged.
- **THD / aliasing vs oversampling:** harmonic + alias content measured against
  oversampling factor via the existing analysis harness.
- **Passthrough / guards:** `drive`=0 / bypass ≈ unity gain; NaN-free under
  extreme drive and parameter sweeps.
- **Primitive graduation checks:** `core/primitives/nonlinear/hysteresis.h`
  exists; `core/primitives/README.md` lists it; portability gate passes over the
  new paths; lab contains README + kernel + host-only harness.

## Open questions

- **OQ1 (sequencing):** which solver(s) ship in the first implementation cut
  (RK4-only first, or RK2+RK4+Newton together)? — `/speckit-clarify`.
- **OQ2 (sequencing):** does the optional explicit-trim compression layer land in
  the first cut or a follow-up? — `/speckit-clarify`.
- **OQ3:** parameter *ranges/mapping* for the physics macros (`drive`,
  `saturation`→`Ms`, `width`→`k`) — musically useful ranges to be tuned during
  implementation against harness measurements (as `saturation` voicings were).
- **OQ4:** default oversampling factor and the exposed factor menu (which
  `Oversampler<Factor>` instantiations to build) — bounded by CPU targets on the
  embedded platforms.
- **OQ5:** whether `Hysteresis` warrants its own `tests/core/` unit test file in
  addition to the lab harness (expected yes, mirroring `delay-line-test.cpp`).

## Provenance

- **Roadmap item:** `design:feature/tape-dynamics` — status `planned`,
  depends-on `multi:feature/phase-nonlinear-dsp` (closed, validated), part-of
  `multi:feature/phase-dynamic-systems`.
- **Design conversation (2026-07-03):** driven in-session via
  `superpowers:brainstorming` under `/stack-control:design`. Four decisions taken
  interactively: (1) identity → hysteresis + tape compression, re-scoped from an
  initial "integrated chain" to preserve progressive-learning layering; (2) model
  → Jiles-Atherton; (3) compression → both emergent + optional explicit, layered;
  (4) solver → selectable surface.
- **Reuse basis (already shipped):** `core/primitives/oversampling/oversampler.h`
  (`Oversampler<Factor>::process(x, evalAtHighRate)`),
  `core/primitives/dynamics/envelope-follower.h`,
  `core/primitives/dynamics/gain-computer.h`,
  `core/effects/saturation/` (static `tape` voicing precedent),
  `core/effects/modulated-delay/wow-flutter.h` (deferred-integration reference).
- **External reference:** Jiles & Atherton magnetic hysteresis model; J. Chowdhury,
  "Real-Time Physical Modelling for Analog Tape Machines," DAFx 2019 (CHOW Tape).
- **Handoff:** on operator approval + design-to-spec gate met → `/stack-control:define`.
