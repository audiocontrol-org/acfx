---
title: Waveshapers — Nonlinear Memoryless Primitive — Design Record
date: 2026-06-30
roadmap-item: design:primitive/waveshapers
status: awaiting-operator-approval
---

# Waveshapers — Nonlinear Memoryless Primitive

Establish the **first nonlinear primitive** of `phase-nonlinear-dsp`: a family of
memoryless transfer functions (the `nonlinear/` primitive category named by the
prospectus) plus the `waveshaping` laboratory that teaches them. This is also the
**first concept to walk the three-layer graduation pattern greenfield** — the
prior worked example (SVF) was a retroactive migration; waveshaping starts as a
lab and graduates into a primitive for real.

## Problem domain

Constitution **Principle IX (Progressive Layered Architecture)** and the program
prospectus declare `phase-nonlinear-dsp` and its first deliverable, **Waveshapers**.
The prospectus names `nonlinear/` as a primitive category and "waveshaping" as a
laboratory; the four-stage model (Theory → Laboratory → Reusable Primitive →
Production Effect) requires this concept to begin as a lab and graduate into a
primitive.

On disk today:

- `core/primitives/` has `filters/`, `delays/`, `modulation/` — **no `nonlinear/`
  category exists.**
- `core/labs/` holds exactly one inhabitant (`state-variable-filter/`), authored
  retroactively to prove the migration path. **No greenfield lab has yet walked
  the lab→primitive graduation** — waveshaping is the first.
- The **measurement infrastructure** (`design:feature/measurement-infrastructure`,
  shipped) already provides Goertzel-based **THD / harmonic** analysis and a sine
  stimulus. Nonlinear shaping is precisely the thing that infrastructure was built
  to validate, but nothing yet exercises it on a nonlinearity.
- Nonlinear waveshaping generates harmonics above Nyquist that **fold back as
  aliasing**. Two sibling items in this same phase bear on this — `oversampling`
  (a separate primitive) and `harmonic-analysis` (a measurement gap) — so the
  waveshaper's boundaries against both must be drawn explicitly.

This is the **first nonlinear concept** in the program. Per **Principle XI (One
Concept at a Time)** its scope is the memoryless nonlinearity and its lab — not the
anti-aliasing machinery (oversampling sibling), not the circuit-accurate diode
(circuit-modeling phase), not the composed saturation effect (a later item).

### Forces and constraints

- **Real-time safety (Principle VI):** no heap allocation, locks, or unbounded work
  in `process()`. Closed-form `tanh`/`exp` and LUT lookups must both be bounded and
  allocation-free on the audio path; any table is built in `init()`, never in
  `process()`.
- **Platform-independent core, thin adapters (Principle IV):** the kernel and
  primitive compile with no JUCE / libDaisy / Teensy knowledge. These primitives
  must run on MCU targets (Daisy / Teensy), where per-sample transcendental
  functions are costly — motivating the LUT evaluation backend.
- **Evolve, don't discard (Principle IX):** the lab kernel relocates into
  `primitives/nonlinear/` and refines in place; it is never re-derived.
- **Explicit gates, never hooks (Commandment II / Principle II):** enforcement
  extends `scripts/check-portability.sh` (already in CI), never a git hook.
- **Strict typing, small modules (Principle VII):** no `any`/unchecked casts; files
  within ~300–500 lines. The transfer-function catalog is large enough that the
  free functions, the wrapper, the ADAA variant, and LUT support are separate
  units.
- **Measurable engineering (Principle X):** every shape is validated by objective
  harmonic measurement (THD / harmonic spectra) reusing the shipped measurement
  infrastructure, not by listening alone.
- **One concept at a time (Principle XI):** memoryless nonlinearity + its lab only;
  no oversampling machinery, no circuit-solved diode, no composed effect.

## Solution space

Six decisions were explored against alternatives. The chosen positions compose into
`## Decisions`; the rejected alternatives and their reasons are recorded here.

### Chosen — Lab + graduated primitive (full pattern, end-to-end)

Author `core/labs/waveshaping/` (README theory + RT-safe kernel + host-only
harness) and graduate the kernel into `core/primitives/nonlinear/`. Walks the full
Theory→Lab→Primitive pattern greenfield, proving for the first time that a concept
born as a lab graduates cleanly (SVF only proved the retroactive migration).

### Rejected — Primitive only (skip the lab)

Author `core/primitives/nonlinear/` directly. **Rejected:** breaks the just-shipped
Principle-IX graduation discipline at the very first opportunity to exercise it
greenfield; the lab layer would have one retroactive inhabitant and no proof the
forward path works.

### Rejected — Lab only, defer graduation

Author the lab now; graduate later once the catalog is proven. **Rejected:** leaves
`nonlinear/` empty and the primitive unbuilt, so the phase's first deliverable
would not actually deliver a reusable primitive; graduation is the proof and should
not be deferred without cause.

### Chosen — Pure free functions + enum-selected stateful wrapper

A namespace of stateless transfer functions (`acfx::shape::tanh(x)`, `cubic(x)`,
…) PLUS a runtime enum-selected `Waveshaper` primitive (`enum class Shape`,
`setShape()`, `process(float)`) that wraps the selected function and owns
drive/bias/gain-comp/DC-block. Matches the existing `SvfPrimitive` idiom; gives
effects a runtime-switchable "character" parameter; keeps the pure functions
independently testable and teachable in the lab.

### Rejected — Template/policy family (`Waveshaper<TanhShape>`)

Each shape a compile-time policy struct. **Rejected:** zero per-sample dispatch is
real, but runtime shape selection (which effects need for a character knob) then
requires an extra dispatch layer, and it diverges from the enum idiom the codebase
already uses (`SvfMode`). The marginal CPU win does not justify the API divergence
for an audio-rate scalar op.

### Rejected — Pure free functions only (no stateful primitive)

Just the shape namespace; drive/bias/gain-comp/DC-block become each caller's
responsibility. **Rejected:** every consuming effect would re-implement the same
gain-staging boilerplate, and stateful concerns (DC-block, ADAA) have nowhere
cohesive to live. The free functions are kept (as a sub-component) but are not the
whole answer.

### Chosen — Both anti-aliasing schools: memoryless core + optional ADAA, oversampling orthogonal

Memoryless shapes are the base primitive. **ADAA** (antiderivative anti-aliasing,
1st/2nd order, stateful) is an opt-in `ADAAWaveshaper` variant. **Oversampling**
(the sibling primitive) remains the orthogonal "wrap any callable" layer:
`saturation = waveshaper inside oversampler`. Captures both anti-aliasing schools
the literature offers without forcing one; planning sequences which lands first.

### Rejected — Memoryless only; oversampling is the sole anti-aliasing path

Waveshapers stay purely memoryless and rely entirely on the oversampling sibling.
**Rejected (captured, not discarded):** clean and orthogonal, but it discards ADAA
— a distinct, cheaper-in-some-regimes technique that belongs to the waveshaping
concept itself. Per capture-over-YAGNI we capture it now rather than re-discover it
later.

### Rejected — Build ADAA into the shaper as the only mechanism

ADAA internal to every shaper, no separate oversampler. **Rejected:** makes the
two phase siblings alternative rather than orthogonal, complicates every shape with
an antiderivative + state, and overlaps the oversampling item's charter (violating
one-concept-at-a-time at the boundary).

### Chosen — Include asymmetric shapers (bias + DC-blocker in the wrapper)

Capture asymmetric/biased curves, adding a `bias` (input offset) control and a
DC-blocker (output high-pass) to the `Waveshaper` wrapper. Asymmetry is what
produces even harmonics (tube/tape character) and is foundational for the
downstream saturation effect; the lab teaches the bias/DC-block concept.

### Rejected — Symmetric (odd-harmonic) shapes only this item

No bias / DC-block; simpler interface. **Rejected:** the saturation effect almost
certainly needs even harmonics, so the asymmetry complexity would merely be pushed
downstream and the lab would teach an incomplete picture of nonlinear shaping.

### Chosen — Include a memoryless diode-style curve here

A memoryless diode-style transfer function (exponential / `tanh`-approx,
asymmetric) is captured as a waveshaper, with the boundary noted explicitly: this
is a *curve*, a different altitude from the circuit-solved diode clipper (which
models the actual I-V relationship with state and numerics) that
`phase-circuit-modeling`'s `design:feature/diode-clippers` owns.

### Rejected — Leave all diode shaping to the circuit phase

Capture only generic asymmetric curves, nothing diode-named. **Rejected:** the
memoryless diode-curve approximation is a common, cheap, teachable stand-in that is
squarely a transfer function; excluding it by name would omit a standard member of
the catalog for fear of an overlap that the altitude distinction already resolves.

### Chosen — Closed-form and LUT as peer selectable evaluation backends

Both `closedForm` (direct `tanh`/`exp`/polynomial — exact, portable) and `lut`
(precomputed table + interpolation — uniform RT cost on MCU) are first-class,
selectable on the `Waveshaper`. The design names table resolution and
interpolation error as quantities the harness validates.

### Rejected — Closed-form base; capture LUT for later

Closed-form only now, LUT parked in open-questions. **Rejected by the operator** in
favor of peers: the MCU targets are a stated, present constraint (not speculative),
so the table backend is captured as a built peer rather than deferred.

### Rejected — LUT-based as the only path

Table evaluation as the sole backend. **Rejected:** sacrifices the exactness and
zero-setup simplicity of closed-form, and would make every harmonic-accuracy
assertion contend with interpolation error from the outset; closed-form is the
reference the LUT is measured against.

## Decisions

1. **Three-layer graduation, greenfield.** Author `core/labs/waveshaping/`
   (`README.md` theory + walkthrough naming `primitives/nonlinear/` as the
   graduation target; RT-safe kernel; host-only `harness/`), then `git mv` the
   kernel into `core/primitives/nonlinear/` and refine in place. First concept to
   walk the forward pattern.

2. **Two-tier interface.**
   - **`acfx::shape::*`** — stateless, memoryless transfer functions (the
     catalog). No sample rate, no state; the unit the lab teaches and the suite
     unit-tests for analytic correctness.
   - **`Waveshaper`** — runtime enum-selected stateful primitive wrapping a
     selected shape:
     ```
     enum class Shape   { tanh, arctan, cubicSoft, algebraic, hardClip,
                          softKnee, chebyshev, biasedAsym, diodeCurve,
                          sineFold, triangleFold };  // catalog; planning cuts first set
     enum class Evaluation { closedForm, lut };
     class Waveshaper {
       void  init(float sampleRate) noexcept;   // builds LUT if selected
       void  setShape(Shape) noexcept;
       void  setEvaluation(Evaluation) noexcept;
       void  setDrive(float) noexcept;          // pre-gain
       void  setBias(float) noexcept;           // asymmetry / even harmonics
       void  setGainCompensation(bool) noexcept;// auto-makeup toward unity
       void  reset() noexcept;
       float process(float x) noexcept;         // drive → shape → comp → DC-block
     };
     ```
   Mirrors the `SvfPrimitive` idiom (allocation-free, `init/set*/reset/process`).

3. **Transfer-function catalog (captured in full; planning sequences the first
   graduated cut).** Soft saturators (`tanh`, `arctan`, cubic soft-clip `x−x³/3`,
   algebraic `x/√(1+x²)`); `hardClip` and a polynomial soft-knee; Chebyshev
   harmonic generators (target nth harmonic); **asymmetric/biased** curves (even +
   odd harmonics, via `bias` + DC-block); a **memoryless diode-style** curve
   (exp / `tanh`-approx); **wavefolders** (sine-fold, triangle-fold).

4. **Asymmetry → bias + DC-blocker.** The `Waveshaper` owns a `bias` control and a
   one-pole DC-blocking high-pass on the output, so asymmetric shapes contribute
   even harmonics without a DC offset reaching downstream. The DC-block cutoff
   choice (fixed vs parameter) is an open question.

5. **Diode boundary (one-concept-at-a-time).** The diode here is a *memoryless
   transfer function*. The *circuit-accurate* diode clipper (numerically solved I-V
   curve, stateful) is `phase-circuit-modeling`'s `diode-clippers`. The design
   records the boundary so the later item does not read as a duplicate.

6. **Both anti-aliasing schools, captured.** `Waveshaper` is memoryless;
   `ADAAWaveshaper` is an opt-in stateful 1st/2nd-order antiderivative-AA variant;
   the `oversampling` sibling is the orthogonal wrap-any-callable layer. The lab
   harness *compares* naive vs ADAA vs oversampled aliasing so the trade-off is
   measured, not asserted.

7. **Two evaluation backends, peers.** `closedForm` (exact, portable reference) and
   `lut` (precomputed in `init()`, interpolated in `process()` — RT-cheap, uniform
   on MCU) are both first-class and selectable. Table resolution and interpolation
   error are validated quantities, with closed-form as the reference bound.

8. **Validation reuses the shipped measurement infrastructure.** The harness drives
   the existing Goertzel/THD analyzer + sine stimulus to produce per-shape harmonic
   signatures and the naive/ADAA/oversampled aliasing comparison; assertions use
   analytic harmonic truths + named tolerances (the `svf-reference` pattern).
   Deeper, nonlinear-specific harmonic tooling is the `harmonic-analysis` sibling's
   charter — boundary noted.

9. **Portability gate extended.** `scripts/check-portability.sh` (in CI, never a
   hook) learns `core/labs/waveshaping/**` and `core/primitives/nonlinear/**` so its
   harness-isolation, dependency-direction, platform-independence, and file-size
   checks cover the new units. A lab harness may consume the kernel/primitive;
   nothing portable may include a harness.

10. **Scope discipline (Principle XI).** This item delivers the memoryless
    nonlinearity + its lab only. Excluded: the oversampling machinery (sibling), the
    circuit-solved diode (circuit phase), deeper harmonic-analysis tooling
    (sibling), and the composed saturation effect (later item).

## Open questions

Captured per the capture-over-YAGNI house rule — parked for an explicit later
scoping pass (`/speckit-clarify` / planning), **not** discarded:

- **First graduated cut.** Which catalog members land in the first graduated
  primitive vs which stay captured-for-later. A planning/sequencing decision, not a
  design blocker.
- **ADAA order and coverage.** 1st- vs 2nd-order ADAA, and which shapes get an
  analytic antiderivative (some catalog members — e.g. wavefolders — have awkward or
  piecewise antiderivatives).
- **LUT resolution / interpolation scheme.** Table size and linear vs higher-order
  interpolation per shape; which shapes actually benefit from a LUT vs are already
  cheap closed-form (`cubicSoft`, `hardClip`).
- **Gain-compensation law.** The auto-makeup model (peak-normalize, RMS-match at a
  reference level, or analytic unity-at-low-signal) and whether it is per-shape.
- **DC-block cutoff.** Fixed (e.g. ~5–20 Hz one-pole) vs a parameter; interaction
  with very-low-frequency content.
- **Wavefolder parameterization.** Fold count / fold gain as parameters and how they
  map to the `drive` control.
- **Lab/harness output contract.** Whether the harness emits the standardized
  artifact the measurement-infrastructure design flagged (CSV harmonic spectra) so
  nonlinear labs are comparable — shared with the open question already recorded for
  the three-layer-structure lab layer.

## Provenance

- **Roadmap item:** `design:primitive/waveshapers` (status `planned` →
  `designing`), `part-of: multi:feature/phase-nonlinear-dsp`. Design pointer set via
  `stackctl workflow link-design` to this file.
- **Constitution:** Principle IX (Progressive Layered Architecture), with supporting
  Principles IV (Platform-Independent Core), VI (Real-Time Safety), VII (Strict
  Typing & Small Modules), X (Measurable Engineering), XI (One Concept at a Time).
  `.specify/memory/constitution.md`.
- **Program vision:**
  `docs/superpowers/specs/2026-06-29-acfx-progressive-dsp-prospectus.md` (Phase 2 —
  Nonlinear DSP: Waveshapers / Saturation / Oversampling / Harmonic analysis; the
  `nonlinear/` primitive category; the four-stage graduation model).
- **Pattern precedent:**
  `docs/superpowers/specs/2026-06-29-three-layer-structure-design.md` (lab kernel +
  host-only harness; `git mv` graduation; portability-gate extension as the
  enforcement seam).
- **Reused infrastructure:**
  `docs/superpowers/specs/2026-06-29-measurement-infrastructure-design.md` and the
  shipped Goertzel/THD analyzer + sine stimulus
  (`tests/core/measurement-distortion-test.cpp`, `measurement-support.h`).
- **Current code surveyed:** `core/primitives/{filters/svf-primitive,delays/
  delay-line,modulation/lfo}.h`, `core/labs/state-variable-filter/`, `core/dsp/`,
  `scripts/check-portability.sh`.
- **Design method:** `superpowers:brainstorming` driven in-session under the
  `/stack-control:design` frontend; house-rules block `stack-control-design-v1`
  injected (capture-over-YAGNI, ≥2 solution-space alternatives, required sections,
  operator-approval marker, handoff to `/stack-control:define`).
- **Decisions driven by the operator** across six forks: altitude (lab + graduated
  primitive), interface (pure functions + enum-selected wrapper), anti-aliasing
  (both memoryless core + ADAA, oversampling orthogonal), asymmetry (include +
  DC-block), diode boundary (memoryless diode curve here), evaluation (closed-form
  and LUT as peers). The operator consistently chose the capture-everything option,
  aligning with the capture-over-YAGNI house rule.
- **Next step:** operator records the `design-approved:` marker on the roadmap node;
  on a met `design-to-spec` gate, hand off to `/stack-control:define` to author the
  Spec Kit spec.
