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

Placeholder: the four voicings (Soft Clip, Tape, Console, Tube Preamp) each fix
a nonlinear shape (from the waveshaper primitive) and a pair of emphasis filter
curves. Bias remains a user control, not baked.

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
