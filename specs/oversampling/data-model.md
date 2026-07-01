# Phase 1 Data Model — Oversampling Primitive

Entities are DSP value-types (no persistence, no external storage). "Fields" are the
compile-time / value members that define each unit's state and contract. All are RT-safe by
construction (fixed-size, no heap).

## Entity: `Oversampler<int Factor>`

The public primitive — a generic oversampling block-wrapper.

- **Type parameter**: `Factor` ∈ {2, 4, 8} (power of two). Determines the number of cascaded 2×
  stages (`log2(Factor)`) and all buffer sizes at compile time.
- **Composed sub-units (value members)**:
  - `std::array<HalfbandUpsampler, log2(Factor)>` — the interpolation cascade (fine→coarse order
    on the way up).
  - `std::array<HalfbandDownsampler, log2(Factor)>` — the decimation cascade (reverse order on the
    way down).
  - a small `std::array<float, Factor>` scratch for the oversampled sample run (fixed-size).
  - `float sampleRate_` (base rate, set at `init()`).
- **Derived / reported values**:
  - `oversampledRate()` = `sampleRate_ * Factor`.
  - `latencySamples()` = summed per-stage halfband group delay, expressed in **base-rate** samples
    (integer).
- **Lifecycle / behavior**:
  - `init(float sampleRate)` — store base rate, prepare stages (no allocation).
  - `reset()` — clear every stage's delay line; configuration (Factor, rate) preserved.
  - `process(float x, Eval&& eval)` — upsample → run `eval` at high rate → decimate → one output.
- **Validation rules (from requirements)**:
  - identity `eval` ⇒ output ≈ input delayed by `latencySamples()` within passband ripple (FR-007).
  - total over finite input — never NaN/Inf for finite input + total `eval` (FR-016).
  - `process()` allocates no heap, takes no locks (FR-013).

## Entity: `HalfbandUpsampler`

A single 2× interpolation stage (1 input sample → 2 output samples), linear-phase polyphase
halfband FIR.

- **Fields**: a fixed-size delay-line `std::array<float, N>` (N from the coefficient table length);
  a write index. No heap.
- **Behavior**: `init()`/`reset()` clear the delay line; `process(float in, float& out0, float&
  out1)` (or returns a 2-sample struct) produces the two interpolated samples using the polyphase
  branches (one branch is the delayed input pass-through for a halfband; the other is the FIR sum
  over the non-zero taps).
- **Validation rules**: contributes its analytic group delay to the parent's `latencySamples()`;
  its magnitude response is the shared halfband spec (Decision 6).

## Entity: `HalfbandDownsampler`

A single 2× decimation stage (2 input samples → 1 output sample), linear-phase polyphase halfband
FIR (the transpose/dual of the upsampler).

- **Fields**: fixed-size delay-line `std::array<float, N>`; write index. No heap.
- **Behavior**: `init()`/`reset()` clear state; `process(float in0, float in1) -> float` band-limits
  and keeps one sample.
- **Validation rules**: same halfband magnitude spec; group delay summed into the parent latency.

## Entity: Halfband coefficient table (`halfband-coefficients.h`)

The `static constexpr` linear-phase halfband FIR coefficients shared by both stage types.

- **Fields**: `constexpr` array of taps (symmetric; halfband ⇒ ~half are zero, exploited by the
  polyphase form); the tap count `N`; a documented provenance comment (generator invocation +
  design parameters — transition band, stopband target per Decision 6).
- **Validation rules**: symmetric (linear phase); meets the stopband/ripple spec (asserted by
  `oversampler-response-test`); no runtime state.

## Entity (client): Saturation oversampled path

The saturation core's realization of the reserved `oversampled` tier (closes saturation FR-015).

- **New members on `SaturationCore`**: an `Oversampler<K>` (default `K = 4`) and an
  `oversampledShaper_` (`Waveshaper`) prepared at `oversampledRate()`, kept parameter-identical to
  the other shapers (shares the existing `applyDrive()/applyBias()/configureShapers()` fan-out).
- **Behavior**: `process()`'s `oversampled` case runs pre-emphasis (base) → oversampler wrapping
  `oversampledShaper_` (high rate) → post-de-emphasis (base). `oversampled` becomes a
  user-selectable quality label.
- **Validation rules**: inharmonic power < `naive` by the named margin (FR-020); runtime
  quality-switch RT-safe with no stale-state artifact beyond a bounded transient (FR-021).

## Relationships

```text
Oversampler<Factor>
├── HalfbandUpsampler   × log2(Factor)   ──┐
├── HalfbandDownsampler × log2(Factor)   ──┼── share ──► halfband-coefficients.h (constexpr)
└── Eval (caller nonlinearity, template) ──┘

SaturationCore (client)
├── Oversampler<K>
└── oversampledShaper_ : Waveshaper  (prepared @ Oversampler::oversampledRate())
```

## State transitions

None persistent. Runtime state is limited to the filter delay lines (advanced per `process()`,
cleared by `reset()`), and — on the client — the active-quality selector (`naive`/`adaa`/
`oversampled`) whose switch is RT-safe (client FR-021).
