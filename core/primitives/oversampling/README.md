> ============================================================================
> ‼ acfx COMMANDMENTS (non-negotiable) ‼
> 1. COMMIT AND PUSH EARLY AND OFTEN
> 2. NO GIT HOOKS, EVER
> 3. DESCRIPTIVE NAMES, NEVER NUMERIC PREFIXES
> (acfx Constitution, Principles I-III -- `.specify/memory/constitution.md`)
> ============================================================================

# Oversampling Primitive

A transparent, real-time-safe anti-aliasing block-wrapper that upsamples a single input
sample, invokes a caller-supplied nonlinearity at the oversampled rate on each upsampled
sample, band-limits the result using linear-phase polyphase halfband FIR filtering, and
decimates back to one output sample. `Oversampler<Factor>` (Factor ∈ {2,4,8}) provides a
reusable, platform-independent foundation for nonlinear processors that must suppress
aliased harmonics without phase distortion.

## Overview

### What it is

`Oversampler<Factor>` is a generic C++ template-class block processor wrapping a cascade
of `log₂(Factor)` halfband FIR filter stages (`HalfbandUpsampler` for interpolation,
`HalfbandDownsampler` for decimation). The wrapper owns:

- **Two halfband stage cascades** — one for upsampling (1 → Factor), one for downsampling
  (Factor → 1), each composed of smaller 2× stages. This cascade structure keeps each
  stage's work modest and allows compile-time sizing.
- **A static constexpr coefficient table** — `kHalfbandCoeffs[91]` in
  `halfband-coefficients.h`, a linear-phase halfband FIR designed for ~84 dB stopband
  attenuation and ≤ 0.001 dB passband ripple. The table is generated offline (generator
  at `core/labs/oversampling/tools/gen-halfband.cpp`) and committed in full (Decision 5,
  `specs/oversampling/research.md`).
- **Two fixed-size scratch buffers** — ping-pong arrays sized at compile time to hold
  `Factor` float samples during upsampling and non-linearity evaluation. No dynamic
  allocation.

Supported factors are powers of two only: Factor = 2 (1 stage), 4 (2 stages), 8 (3 stages).
Non-power-of-two ratios and runtime-selectable factors are deferred (spec FR-025).

### RT-safety contract (Constitution VI)

The primitive guarantees:

- **No heap allocation** — all storage (stages, delay lines, scratch buffers) is compiled
  to fixed-size `std::array` value members. Prepare once at plugin load; no `new` in the
  audio thread.
- **No locks** — all per-sample work is lock-free and reentrant. Multiple threads processing
  different `Oversampler<K>` instances do not interfere.
- **Bounded, deterministic work** — per-input sample, the work is proportional to
  `~Factor/2 × tap_count` multiplies and adds, constant across all factors and inputs.
  No branches on the audio path (stages are known at compile time).
- **`noexcept` interface** — every public method is `noexcept`; no C++ exceptions escape
  the audio thread.
- **Platform-independent** — the primitive uses only the C++ standard library; no vendor
  headers (JUCE, libDaisy, Teensy SDK, DaisySP). Core remains portable (Constitution IV).

## Usage

### Basic contract: `init()`, `reset()`, `process()`

```cpp
// Prepare the oversampler for a base sample rate.
// Clears internal filter state. Call at plugin load / audio thread prepare.
void init(float sampleRate) noexcept;

// Clear the filter state (delay lines) without forgetting configuration.
// Call when flushing stale samples (between musical phrases, voice resets, etc.).
void reset() noexcept;

// Process one input sample.
// Caller supplies evalAtHighRate, a noexcept nonlinearity to evaluate at the
// oversampled rate. The oversampler calls evalAtHighRate(sample) exactly Factor
// times, once per upsampled sample, in oversampled-time order.
template <class Eval>
float process(float x, Eval&& evalAtHighRate) noexcept;

// Query the effective internal rate where evalAtHighRate runs.
float oversampledRate() const noexcept;

// Integer processing latency (group delay) referred to the BASE sample rate.
// Constant for a given Factor. Use for host plugin delay compensation (PDC).
int latencySamples() const noexcept;
```

### Example: wrapping a waveshaper

```cpp
// Prepare a waveshaper at the oversampled rate.
acfx::Waveshaper shaper;
acfx::Oversampler<4> oversampler;

void prepare(float baseRate) {
    oversampler.init(baseRate);
    shaper.init(oversampler.oversampledRate());  // Prepare at 4× baseRate
    shaper.setShape(acfx::Shape::tanh);
    shaper.setDrive(2.0f);
}

void process(float x) {
    // The lambda is called 4 times per input sample, at the oversampled rate.
    // It must be noexcept and RT-safe (no heap, no locks).
    return oversampler.process(x, [&](float s) noexcept {
        return shaper.process(s);
    });
}
```

The oversampler is transparent relative to the naive waveshaper — it differs *only* in
suppressing aliased harmonics — because the halfband filters introduce linear phase (no
tone coloration) and the FIR structure is proven and analyzable.

### Caller's nonlinearity contract

The callable passed to `process()` must be:

- **`noexcept`** — declared `noexcept` so the compiler can inline it and prove no exceptions
  escape the audio path. The oversampler uses `static_assert(noexcept(...), ...)` to enforce
  this at compile time.
- **RT-safe** — no heap allocation, no locks, no slow library calls; must execute in bounded
  time.
- **Stateless or locally-captured state** — if the nonlinearity holds state (e.g., a
  waveshaper's DC-blocker), that state must be prepared at the oversampled rate the caller
  query from `oversampledRate()`. The oversampler does not manage the nonlinearity's state;
  it only calls the function.

### Latency and group delay

Each halfband stage contributes an integer group delay. The upsampler cascade and
downsampler cascade combine to produce a total group delay:

```
latencySamples() = 90 * (Factor - 1) / Factor   (rounded to nearest, ties toward lower index)

Factor 2 -> 45 samples
Factor 4 -> 67 samples
Factor 8 -> 79 samples
```

This is a linear-phase composite, so the delay is exact, constant, and testable. The caller
can query `latencySamples()` and inform the host plugin interface of the delay for PDC
(plugin delay compensation), ensuring wet/dry mix alignment.

## Building blocks: halfband stages

The primitive builds on two lightweight, reusable halfband FIR stages defined in
`halfband-stage.h`:

### `HalfbandUpsampler` (1 → 2 interpolation)

Interpolates one input sample to two oversampled samples using a polyphase decomposition:

- Accepts one base-rate input, outputs two high-rate samples (even and odd phase).
- Uses a 46-tap dense branch (the even-index taps of the halfband) and a degenerate odd
  branch (pure delayed 0.5 pass-through, the center tap).
- Per-base-rate-sample work: ~46 multiplies.
- `latency()` (at high rate): 45 samples.
- Scales outputs by 2 to preserve passband gain after zero-stuffing.

### `HalfbandDownsampler` (2 → 1 decimation)

Band-limits and decimates two high-rate samples (even and odd phase) to one base-rate
sample:

- Accepts two high-rate inputs (even and odd phase), outputs one base-rate sample.
- Applies the same 91-tap halfband, polyphase-decomposed.
- Per-base-rate-sample work: ~46 multiplies.
- `latency()` (at high rate): 45 samples.

Both stages are value-typed with fixed-size delay-line arrays (no dynamic allocation) and
are `noexcept` throughout.

## Filter specification

The static constexpr coefficient table is a linear-phase halfband FIR designed with the
following targets:

- **Tap count**: 91 (Type-I symmetric, odd length)
- **Design method**: Kaiser-windowed ideal-sinc, linear phase
- **Stopband**: ≥ 80 dB attenuation (achieved ~84.4 dB)
- **Passband ripple**: ≤ 0.1 dB across the base-rate audio band (achieved ~0.001 dB)
- **Transition band**: Placed at the halfband point (0.5 normalized frequency) so the
  base-rate audio band (to ~20 kHz at 48 kHz) is in the passband

The header `halfband-coefficients.h` records the generator invocation and design parameters
so the table is reproducible (Decision 5, FR-024). Regenerate by running:

```
cpp core/labs/oversampling/tools/gen-halfband.cpp
```

No hand-tuning; coefficients are deterministic and analytically justified.

## Platform-independent design

The primitive uses only:

- C++ standard library (`<array>`, `<cstddef>`, `<utility>`)
- In-repo headers (none outside `core/`)
- No DaisySP, JUCE, Daisy, Teensy SDK, or vendor-specific libraries

This ensures the primitive is usable in any context: desktop DAW plugins, embedded
processors (Daisy, Teensy via the generic HAL), test harnesses, and future clients.
The test/harness layer (`core/labs/oversampling/harness/`) is host-only and does not
pollute the portable primitive.

## Testing and validation

The primitive is validated by the test suites under `tests/core/`:

- `oversampler-transparency-test.cpp` — identity evaluation (input == output after latency
  alignment) and reset behavior.
- `oversampler-aliasing-test.cpp` — driven nonlinearity showing inharmonic energy reduction
  ≥ 20 dB margin vs. naive evaluation.
- `oversampler-response-test.cpp` — frequency-sweep stopband rejection and passband ripple
  vs. design tolerances.
- `oversampler-latency-test.cpp` — measured group delay matches `latencySamples()` exactly.
- `no-allocation-test.cpp` (extended) — `process()` allocation-free sentinel pass.

All bounds are named constants, not magic numbers. All testing reuses the shipped measurement
infrastructure (Goertzel/THD analyzer, sine stimulus, `svf-reference` analytic-tolerance
pattern).

## See also

- **Laboratory documentation**: `core/labs/oversampling/README.md` — theory (sampling, aliasing,
  halfband FIR, polyphase decomposition, group delay).
- **Specification**: `specs/oversampling/spec.md` (charter + acceptance criteria).
- **Research / Decisions**: `specs/oversampling/research.md` (technical decisions + rationale).
- **Data model**: `specs/oversampling/data-model.md` (entities and composition).
- **Design record**: `docs/superpowers/specs/2026-07-01-oversampling-design.md` (operator-approved).
- **Coefficient generator**: `core/labs/oversampling/tools/gen-halfband.cpp` (reproducibility).
- **First client**: `core/effects/saturation/saturation-core.h` (integrates the primitive for
  anti-aliasing the `oversampled` quality path).
