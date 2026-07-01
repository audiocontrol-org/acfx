> ============================================================================
> ‼ acfx COMMANDMENTS (non-negotiable) ‼
> 1. COMMIT AND PUSH EARLY AND OFTEN
> 2. NO GIT HOOKS, EVER
> 3. DESCRIPTIVE NAMES, NEVER NUMERIC PREFIXES
> (acfx Constitution, Principles I-III -- `.specify/memory/constitution.md`)
> ============================================================================

# Saturation — Lab

A saturation effect applies a musical distortion to audio by composing a nonlinear
waveshaping stage between two frequency-shaping filters, giving different voicings
(Soft Clip, Tape, Console, Tube Preamp) distinct spectral and harmonic character.
This lab demonstrates how a production effect is built from already-shipped primitives
and produces the measured evidence that the composition meets the project's real-time
safety and correctness requirements.

## Theory

### Gain-staging: drive and output makeup

Placeholder: explanation of how input gain (`drive`) stages the signal into the
nonlinear stage, and how output-level compensation (`output`) trims the result.

### Per-voicing pre- and post-emphasis filtering

Placeholder: explanation of how each voicing uses a pre-emphasis filter to shape
the signal before the waveshaper, and a post-de-emphasis filter after it, creating
the spectral and harmonic distinctness (tape vs console vs tube character).

### Voicing: shape plus emphasis curves

The four voicings (`saturation-voicings.h::voicingConfig`) each fix a
`Shape` (from the waveshaper primitive) plus a pre-emphasis and
post-de-emphasis `SvfPrimitive` curve pair. Bias remains a separate USER
control (`SaturationCore::setBias`), never baked into the table (design
Decision 5, FR-007) — every voicing is exercised at the same drive/bias so
the difference measured below is attributable to shape + emphasis alone.

- **Soft Clip** — `Shape::softKnee`, near-flat wide lowpass pre/post
  (18 kHz, res 0.1 on both). The least-colored voicing: character comes
  almost entirely from the shape's harder knee (odd harmonics only), not
  from tonal shaping, since the emphasis pair is deliberately transparent
  at audio frequencies.
- **Tape** — `Shape::tanh` (a softer, more rounded odd-only saturator than
  softKnee) through a lowpass pre (9 kHz, res 0.1) that trims extreme highs
  ahead of the nonlinearity, then a **lower**-cutoff lowpass post (4.2 kHz,
  res 0.15) that is the tape HF-loss signature. The post cutoff is
  deliberately pulled down into the measured harmonic band (the 4th-6th
  harmonics of a 1 kHz fundamental land at 4-6 kHz) so the HF loss is
  audible/measurable rather than sitting above the content it is supposed
  to darken — an earlier 8 kHz post cutoff left tape's ratios too close to
  softClip's because the rolloff barely touched anything below Nyquist/6.
  Net: the darkest of the four voicings, dominated by low-order odd
  harmonics with real top-end attenuation.
- **Console** — `Shape::arctan`, the mildest-curvature shape in the
  catalog (lowest THD at a given drive). A gentle highpass pre (120 Hz,
  res 0.1) cleans sub/rumble ("glue"), and a near-flat, slightly resonant
  lowpass post (15 kHz, res 0.2) adds subtle top-end presence without
  darkening. The brightest/cleanest voicing.
- **Tube Preamp** — `Shape::diodeCurve`, the only sign-asymmetric shape in
  the voicing set that still has an analytic antiderivative (ADAA-safe),
  so it is the only voicing with a genuine even-harmonic + DC signature. A
  low-mid lowpass pre (5 kHz, res 0.2) pushes the body of the signal into
  the asymmetry (warmth), and a resonant lowpass post (3.5 kHz, res 0.5)
  adds a presence bump near its cutoff while rolling off the top.

The exact cutoff/resonance numbers above are the tuning-pass OPEN QUESTION
(data-model.md / design open questions): they were chosen and iterated
against the mutual-distinguishability check in
`tests/core/saturation-voicings-test.cpp` (every pairwise spectral distance
>= 0.02 across harmonics 1-6 of a 1 kHz fundamental), not derived from a
reference tape/console/tube measurement.

### Anti-aliasing: naive versus ADAA

Placeholder: explanation of naive waveshaping aliasing at high frequencies and
high drive, and how first-order antiderivative anti-aliasing (ADAA) reduces
inharmonic energy. The quality control selects between naive and ADAA; an
oversampled tier is reserved but not yet wired.

### Dry/wet blending and signal chain composition

Placeholder: the full per-channel signal chain — pre-emphasis filter → nonlinear
waveshaper (with drive and bias) → post-de-emphasis filter → tone tilt → dry/wet
parallel blend against the unprocessed input → output trim.

## Walkthrough

The composition kernel is organized into three headers that will graduate to
`core/effects/saturation/` along with a wrapper header. The walkthrough below
describes each unit and its role in the composition:

### `saturation-core.h`

Placeholder: the RT-safe per-channel composition kernel applying the full signal
chain (filters, waveshaper, blend, trim) with per-channel state management.

### `saturation-voicings.h`

Placeholder: the voicing table mapping each of the four voicings to its fixed
nonlinear shape and pre/post-emphasis filter coefficients.

### `saturation-effect.h`

Placeholder: the effect-contract wrapper adding the constexpr `ParameterDescriptor`
table and lock-free cross-thread parameter handoff (will graduate with the kernel).

### Harness

The host-only harness at `core/labs/saturation/harness/saturation-harness.cpp`
drives the composition kernel and produces measured evidence (to be filled by T021):

- **Per-voicing harmonic signatures** — the harmonic series for each voicing under
  standard stimulus conditions.
- **Drive monotonicity** — how measured distortion increases with input drive.
- **Gain-compensation band** — the output level hold across drive.
- **Naive vs ADAA aliasing comparison** — measured inharmonic energy reduction.

#### Build and run

```
export CPM_SOURCE_CACHE=external/.cpm-cache
cmake --build --preset test -j8
./build/test/acfx_lab_saturation_harness
```

## Composition: which primitives and why

The effect composes two shipped primitives without adding new nonlinearity or
filters:

- **`Waveshaper` primitive** (`core/primitives/nonlinear/`) — the memoryless and
  stateful wrapper providing drive, bias, shape selection, DC-blocking, and optional
  gain compensation; supports naive and ADAA anti-aliasing modes.
- **`SvfPrimitive` filter** (`core/primitives/filters/`) — the state-variable filter
  used for the pre-emphasis, post-de-emphasis, and tone tilt stages, with
  coefficients computed at preparation time.

## Measured evidence

Placeholder: per-voicing harmonic signatures (table format, like the waveshaping
lab), drive→THD monotonicity, gain-compensation band hold, and the naive-vs-ADAA
aliasing comparison. All numbers are derived from the host harness; none are
fabricated.

## Graduation target

The composition kernel (`saturation-core.h` and `saturation-voicings.h`) is authored
here in the lab, then **graduated** by relocation (not re-derivation) into
`core/effects/saturation/`, where the effect-contract wrapper (`saturation-effect.h`)
is added. This lab folder persists as the living lab record — README (theory +
measured evidence) and the host-only harness — now driving the graduated effect.
This is the first concept to walk the Theory → Laboratory → **Production Effect**
graduation chain (prior graduations targeted primitives).
