> ============================================================================
> ‼ acfx COMMANDMENTS (non-negotiable) ‼
> 1. COMMIT AND PUSH EARLY AND OFTEN
> 2. NO GIT HOOKS, EVER
> 3. DESCRIPTIVE NAMES, NEVER NUMERIC PREFIXES
> (acfx Constitution, Principles I-III -- `.specify/memory/constitution.md`)
> ============================================================================

# Oversampling — Lab

Oversampling applies a memoryless nonlinearity at a higher sample rate, then band-limits
and decimates back to the original rate, suppressing aliased images that would otherwise
fold back into the audio band when the nonlinearity generates harmonics above the Nyquist
limit. This lab demonstrates how oversampling achieves transparent anti-aliasing through
linear-phase polyphase halfband FIR filtering and produces the measured evidence that the
composition meets the project's real-time safety and correctness requirements.

## Theory

### Aliasing in nonlinear processing

A memoryless nonlinearity evaluated sample-by-sample — like a waveshaper with a high-order
transfer function — generates harmonics without regard for the Nyquist limit. When the
input frequency and harmonic order are high enough, harmonic images above `fs/2` fold back
into the audible band as inharmonic (non-musical) energy. The effect is especially
pronounced at high drive levels where the nonlinearity generates many high-order harmonics.

Example: a 5 kHz sine driven hard through a waveshaper at 48 kHz will generate harmonics
at 10 kHz, 15 kHz, 20 kHz, 25 kHz, ... — and the 25 kHz harmonic aliases back to 23 kHz,
the 30 kHz harmonic to 18 kHz, and so on, muddying the midrange with artifacts the
original signal never contained.

### Oversampling + band-limiting mechanism

The oversampling solution evaluates the same nonlinearity at a higher rate (e.g., 4× the
original `fs`), where the Nyquist limit is 4 times higher. The harmonics generated are now
far less likely to alias because the absolute frequency of each harmonic is higher, but
they are further from the higher Nyquist limit in relative terms. A linear-phase lowpass
filter operating at the oversampled rate then band-limits the output before decimating
back to the original rate, removing the images the nonlinearity created above the original
Nyquist — the ones that would have aliased back in.

The key insight: band-limiting at the higher rate gives you a wider transition band (both
in absolute Hz and in filter-tap count) before the original audio band, so you can achieve
steeper, sharper cutoff without distorting the audio you care about. After decimation, the
remaining images are far below the floor you care about.

### Linear-phase FIR filtering and transparent phase response

Transparent oversampling preserves the phase response of the underlying nonlinearity
(ideally) so the oversampled path differs from the naive (unoversampled) path *only* in
aliasing suppression, not in tone or transient response. Linear-phase FIR filters achieve
this: they introduce no phase distortion, only a constant, predictable delay (group delay)
across all frequencies.

This is why halfband FIR filters — not minimum-phase IIR, not Butterworth cascades — are
the foundation here: they are symmetric (odd length, center tap at the geometric middle),
their group delay is constant and equal to `(N-1)/2` samples, and the aliasing test
`aliasingMeasure` can isolate the reduction because the phase path remains unchanged.

### Polyphase decomposition and computational efficiency

A naive approach would evaluate the full FIR at the oversampled rate: for a halfband filter
with 91 taps (the coefficient table in `halfband-coefficients.h`) running at 4× the base
rate, that is 91 taps × 4 = 364 multiplies per base-rate output sample. Expensive.

The polyphase trick exploits the halfband structure: the 91-tap symmetric linear-phase
halfband FIR has approximately half its taps identically zero (every other even-offset tap
from center is exactly zero). Splitting the filter by tap parity yields two sub-filters
(polyphase branches) that each run at the *lower* rate:

- **Even branch** — the dense taps, runs at the base rate, ~46 taps.
- **Odd branch** — almost all zero except the center tap (0.5), a pure delayed pass-through.

So a 2× halfband stage costs roughly `46 mults per base sample`, not `91 × 2`. When you
cascade stages (e.g., 4× = two 2× stages, 8× = three 2× stages), each stage after the first
already operates at a higher base rate, so the cumulative work per input sample stays modest.

### Group delay and latency

Each halfband stage contributes a constant, integer group delay at its operating rate. For
the 91-tap halfband, the group delay is `(91-1)/2 = 45` samples at the rate that stage
operates. The `Oversampler<Factor>` sums the per-stage delays and converts them to base-rate
samples: this is the quantity `latencySamples()` reports, so the host can apply plugin
delay compensation (PDC) if needed. The latency is an integer and is deterministic,
testable, and invariant.

## Walkthrough

The oversampling concept is organized as a reusable primitive under `core/primitives/oversampling/`
and a living laboratory under `core/labs/oversampling/` that exercises and validates it.
Unlike the saturation lab, the primitive and the lab are distinct artifacts: the primitive
(coefficient table, halfband stages, `Oversampler` wrapper) is authored directly under
`core/primitives/` and is never relocated; the lab persists as the documentation + host-only
measurement harness.

### `core/primitives/oversampling/` (the reusable primitive)

Three portable headers:

- **`halfband-coefficients.h`** — the `static constexpr` linear-phase halfband FIR
  coefficient table (91 taps, symmetric, center tap = 0.5, designed for ~84 dB stopband
  attenuation). The header records the generator invocation and design parameters so the
  coefficients are reproducible and traceable (Decision 5, `research.md`).
- **`halfband-stage.h`** — `HalfbandUpsampler` (1→2) and `HalfbandDownsampler` (2→1), the
  two polyphase FIR stage types. Each carries fixed-size delay-line arrays (compile-time
  sized, no heap), implements the polyphase decomposition, and is allocation-free and
  lock-free in the per-sample `process()`.
- **`oversampler.h`** — `Oversampler<Factor>` (Factor ∈ {2,4,8}), the generic block
  wrapper that cascades 2× halfband stages (log₂(Factor) of them), interpolates one input
  sample up to `Factor` oversampled samples, invokes a caller-supplied nonlinearity on each,
  and decimates back to one output sample. Exposes `oversampledRate()` and `latencySamples()`
  for the caller to prepare rate-dependent DSP and align wet/dry for PDC.

These three headers are the primitive, authored once and reused by any client that needs
anti-aliasing (currently, the saturation effect's `oversampled` quality path).

### Lab: `core/labs/oversampling/` (theory + measurement)

This directory contains the theory documentation (this README) and the host-only measurement
harness. It drives the primitive to produce measured evidence of transparency and aliasing
reduction:

- **This README** — theory explanation of sampling, aliasing, linear-phase filtering, polyphase
  structure, and group delay.
- **`harness/oversampling-harness.cpp`** — host-only (no audio-path build dependency):
  Exercises the primitive under the identity evaluation (transparency evidence), driven
  nonlinearity (aliasing reduction evidence), and frequency-sweep measurements (stopband
  rejection, passband ripple). Reuses the shipped Goertzel/THD analyzer and sine stimulus
  infrastructure.
- **`tools/gen-halfband.cpp`** — the committed offline coefficient generator. Not on any
  build or audio path; kept here as a reproducibility aid so the committed coefficient
  table can be verified and regenerated if needed (Decision 5, FR-024).

The lab includes the measured harness output, similar to the waveshaping lab's "Measured
evidence" section, once the harness is built and run.

## Composition: which primitives and why

The oversampling primitive composes no prior primitives — it is the first new reusable DSP
building block for anti-aliasing. It invents no new concept (Principle XI: one concept at a
time) — oversampling as a technique is well-established; the novelty is the transparent,
RT-safe, embedded-friendly reusable wrapper.

The first client of the primitive is the saturation effect (`core/effects/saturation/`),
which gains an `Oversampler<4>` member and a dedicated waveshaper prepared at the
oversampled rate, closing the reserved `oversampled` quality seam (saturation FR-015). The
saturation test suite will validate that the oversampled path achieves measurably lower
inharmonic energy than the naive path.

## Measured evidence

All measured evidence in this section comes from running the host-only harness
(`core/labs/oversampling/harness/oversampling-harness.cpp`, `acfx_lab_oversampling_harness`).
The harness reuses the shipped Goertzel/THD analyzer and sine stimulus infrastructure to
produce:

- **Transparency under identity** — for each Factor (2, 4, 8), the oversampler with an
  identity nonlinearity should reproduce the input with only a delay equal to
  `latencySamples()`. Measured round-trip error should be below floating-point noise
  (passband ripple tolerance).
- **Aliasing reduction** — for a driven nonlinearity (e.g., a waveshaper) at high frequency
  and high drive, total harmonic power, inharmonic power, and the inharmonic reduction ratio
  (oversampled vs. naive) demonstrate that oversampling suppresses folded harmonics by the
  design margin (≥ 20 dB initial target).
- **Stopband rejection and passband ripple** — frequency-sweep measurements confirm the
  halfband filter meets the design targets (≥ 80 dB stopband, ≤ 0.1 dB passband ripple across
  the audio band).

Per-factor latency equality (measured impulse-peak delay vs. `latencySamples()`) is asserted
by the `oversampler-latency-test.cpp` doctest suite rather than the harness, as it is a
correctness gate, not comparative evidence.

#### Build and run

```
export CPM_SOURCE_CACHE=external/.cpm-cache
cmake --build --preset test --target acfx_lab_oversampling_harness
./build/test/acfx_lab_oversampling_harness         # human-readable report
./build/test/acfx_lab_oversampling_harness --csv   # machine-readable CSV (factor, stimulus, measurement)
```

## References

- **Design record**: `docs/superpowers/specs/2026-07-01-oversampling-design.md` (operator-approved, charter + decisions).
- **Specification**: `specs/oversampling/spec.md` (feature charter + acceptance criteria).
- **Research + Decisions**: `specs/oversampling/research.md` (phase 0 output, load-bearing technical decisions with rationale).
- **Data model**: `specs/oversampling/data-model.md` (phase 1 output, entities and composition).
- **Quickstart**: `specs/oversampling/quickstart.md` (phase 1 output, how to build and run).
- **API contract**: `specs/oversampling/contracts/oversampling-api.md` (the primitive's public C++ surface).

## Graduation and living documentation

The three portable primitive headers (`halfband-coefficients.h`, `halfband-stage.h`,
`oversampler.h`) are authored directly under `core/primitives/oversampling/` and remain
there permanently. Unlike the saturation concept (which graduated from lab kernel to effect
kernel via `git mv`), the oversampling primitive is not relocated — it is authored once at
the primitives layer and reused by clients.

This lab folder persists as the living documentation record: this README (theory +
walkthrough + measured evidence), the host-only harness (now driven by the primitive via
updated `#include` paths), and the coefficient generator (for reproducibility). The pattern
mirrors the waveshaping lab, which is the living companion to the graduated waveshaper
primitive in `core/primitives/nonlinear/`.
