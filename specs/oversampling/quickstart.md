# Quickstart — Oversampling primitive validation

How to build and validate the oversampling primitive (and its first client, saturation)
end-to-end. This is a run/validate guide; implementation detail lives in `tasks.md` and the
source. Build uses CMake presets (CPM-pinned deps). Offline sandbox:
`export CPM_SOURCE_CACHE=external/.cpm-cache`.

## Prerequisites

- The repo builds today: `cmake --preset test` succeeds on the branch.
- No new third-party dependency: the primitive authors its own linear-phase halfband FIR (DaisySP
  ships no resampler). The primitive introduces **no** DaisySP dependency.
- The first client's nonlinearity is already shipped: `acfx::Waveshaper`
  (`core/primitives/nonlinear/`).
- The base measurement infrastructure (Goertzel/THD analyzer, sine stimulus, allocation sentinel,
  analytic-bound helpers) is already shipped under `tests/support/measurement/`. The aliasing
  measure (`aliasingMeasure`) exists in the saturation aliasing test; if reused, it is factored into
  `tests/support/measurement/` so both suites share one implementation.

## Build + run the host test suite

```bash
cmake --preset test
cmake --build --preset test
ctest --preset test
```

Expected: the new oversampler doctest suites pass alongside the existing suites, and the modified
saturation aliasing suite passes with its new `oversampled < naive` assertion.

## Validation scenarios (map to user stories / success criteria)

1. **Transparency (US1 / SC-001)** — `oversampler-transparency-test.cpp` drives a passband sine
   through `Oversampler<2/4/8>` with an identity `eval`; asserts the output equals the input
   delayed by `latencySamples()` within the named passband-ripple tolerance; asserts silence-in →
   silence-out (no denormal/NaN/Inf); asserts `reset()` restores fresh behavior.

2. **Anti-aliasing reduction (US1/US2 / SC-002, SC-007)** — `oversampler-aliasing-test.cpp` wraps a
   hard nonlinearity that generates supra-Nyquist harmonics on a high-fundamental sine; asserts the
   oversampled inharmonic power is below the naive (base-rate) path by ≥ the named margin, for each
   factor; asserts a higher factor never worsens residual aliasing.

3. **Filter response (US2 / SC-003)** — `oversampler-response-test.cpp` asserts the halfband
   stopband rejection ≥ and passband ripple ≤ their named tolerances, against analytic FIR truth
   (the `svf-reference` assertion pattern).

4. **Latency correctness (US3 / SC-004)** — `oversampler-latency-test.cpp` measures the group delay
   (impulse / reference) per factor and asserts it equals `latencySamples()` exactly.

5. **Real-time safety (SC-005)** — `no-allocation-test.cpp` (extended) wraps `Oversampler::process()`
   for each factor in the allocation sentinel; asserts zero heap allocations and no locks.

6. **First client — saturation oversampled tier (US4 / SC-006)** — `saturation-aliasing-test.cpp`
   (modified) selects the saturation `oversampled` quality; asserts its inharmonic energy is below
   the `naive` tier by ≥ the named margin (no longer identical to the ADAA path), that `oversampled`
   is exposed as a user-selectable quality option, and that runtime quality switching is RT-safe
   with no stale-state artifact.

## Lab harness (host-only, optional exploration)

`core/labs/oversampling/harness/oversampling-harness.cpp` renders transparency and
aliasing-reduction evidence (spectra, sweeps) for the README theory. Host-only; not on any
embedded/audio path.

## Portability gate (explicit, never a hook — Principle II)

```bash
scripts/check-portability.sh
```

Expected: `core/labs/oversampling/**` and `core/primitives/oversampling/**` are reported
platform-free and (for the primitive) harness-free.
