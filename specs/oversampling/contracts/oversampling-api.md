# Public API Contract — Oversampling Primitive

Normative C++ surface for `core/primitives/oversampling/`. Signatures are the contract other
code (and the first client) builds against; bodies are the implementation's concern. All types
live in `namespace acfx`. Headers are platform-independent (Constitution IV) and RT-safe on the
hot path (VI).

## `Oversampler<int Factor>` — `core/primitives/oversampling/oversampler.h`

```cpp
namespace acfx {

// Generic oversampling block-wrapper. Factor MUST be a power of two in {2, 4, 8};
// enforced with a static_assert. All storage is compile-time-sized (no heap).
template <int Factor>
class Oversampler {
public:
    static_assert(Factor == 2 || Factor == 4 || Factor == 8,
                  "Oversampler Factor must be 2, 4, or 8");

    // Prepare for a base sample rate. Clears filter state. No audio-path work,
    // no allocation. (Coefficients are static constexpr; nothing to compute.)
    void init(float sampleRate) noexcept;

    // Clear all filter delay lines without discarding configuration.
    void reset() noexcept;

    // The effective internal rate the caller's nonlinearity runs at.
    // == sampleRate (from init) * Factor.
    float oversampledRate() const noexcept;

    // Exact linear-phase group delay referred to the BASE rate, in samples.
    // Integer for Factor 2 (45); FRACTIONAL for Factor 4 (67.5) and 8 (78.75).
    // Use for sub-sample-accurate alignment (e.g. a parallel dry-path delay).
    float groupDelaySamples() const noexcept;

    // Integer host-PDC latency: groupDelaySamples() rounded to the nearest whole
    // sample (ties toward the lower index). This is the value a host / integer-
    // sample delay-compensation scheme uses; the exact (possibly fractional)
    // delay is groupDelaySamples() above.
    int latencySamples() const noexcept;

    // Process one input sample:
    //   1. upsample x to `Factor` samples (anti-image halfband interpolation),
    //   2. invoke evalAtHighRate on EACH oversampled sample (caller's nonlinearity),
    //   3. anti-alias halfband decimation back to exactly one output sample.
    // evalAtHighRate is called at oversampledRate() and MUST be RT-safe + noexcept.
    // Allocation-free, lock-free, bounded (Constitution VI).
    template <class Eval>
    float process(float x, Eval&& evalAtHighRate) noexcept;
};

} // namespace acfx
```

**Contract notes**

- `process()` is total over finite input: for finite `x` and a finite, total `evalAtHighRate`, the
  return value is finite (never NaN/Inf) — FR-016.
- With an identity `evalAtHighRate` (`[](float s){ return s; }`), the output equals the input
  delayed by `latencySamples()` within the named passband ripple tolerance — FR-007.
- `evalAtHighRate` is invoked exactly `Factor` times per `process()` call, in oversampled-time
  order. The primitive holds no reference to it beyond the call.
- The primitive embeds NO nonlinearity and NO DC handling — those remain the caller's concern
  (matches the `Waveshaper` DC-blocker ownership) — FR-002, edge cases.
- Changing factor means using a different `Oversampler<N>` instance (compile-time) — FR-003.

## `HalfbandUpsampler` / `HalfbandDownsampler` — `core/primitives/oversampling/halfband-stage.h`

```cpp
namespace acfx {

// One 2x interpolation stage (1 -> 2 samples), linear-phase polyphase halfband FIR.
class HalfbandUpsampler {
public:
    void  init() noexcept;                 // clear delay line
    void  reset() noexcept;                // clear delay line
    void  process(float in, float& out0, float& out1) noexcept; // two interpolated samples
    static constexpr int latency() noexcept;  // group delay at THIS stage's output rate
};

// One 2x decimation stage (2 -> 1 samples), linear-phase polyphase halfband FIR.
class HalfbandDownsampler {
public:
    void  init() noexcept;                 // clear delay line
    void  reset() noexcept;                // clear delay line
    float process(float in0, float in1) noexcept;  // band-limited, decimated sample
    static constexpr int latency() noexcept;  // group delay at this stage's INPUT rate
};

} // namespace acfx
```

**Contract notes**

- Both stages are value-typed with a fixed-size `std::array` delay line — no heap (FR-013/014).
- `latency()` is `static constexpr` (analytic group delay); `Oversampler::latencySamples()` sums
  the cascade's per-stage latencies and converts to base-rate samples (FR-006/012).
- Both stages read the shared `static constexpr` coefficient table.

## Coefficients — `core/primitives/oversampling/halfband-coefficients.h`

```cpp
namespace acfx {
// Symmetric (linear-phase) halfband FIR taps; ~half are exactly zero (halfband),
// exploited by the polyphase decomposition. Provenance (generator + design params:
// transition band, >= 80 dB stopband, <= 0.1 dB passband ripple) documented inline.
inline constexpr int   kHalfbandTaps = /* N */;
inline constexpr float kHalfbandCoeffs[kHalfbandTaps] = { /* ... */ };
} // namespace acfx
```

## First-client surface changes (saturation — closes FR-015)

Not a new public API; the contract change on the existing saturation surface:

- `core/effects/saturation/saturation-core.h`: `SaturationCore` gains an `Oversampler<K>` member and
  an `oversampledShaper_` prepared at `oversampledRate()`; the `oversampled` case in `process()`
  becomes a real oversampled path (was the interim ADAA mapping). RT-safe; both shapers stay
  parameter-identical.
- `core/effects/saturation/saturation-effect.h`: `oversampled` is added to `kQualityLabels` (now
  user-selectable) and mapped in the discrete-quality bucket→enum resolution.

## Invariants asserted by tests (see quickstart.md)

| Invariant | Requirement | Suite |
|---|---|---|
| identity round-trip ≈ delayed input | FR-007 / SC-001 | `oversampler-transparency-test` |
| oversampled inharmonic << naive | FR-008 / SC-002 | `oversampler-aliasing-test` |
| stopband ≥ / ripple ≤ named tolerances | FR-009 / SC-003 | `oversampler-response-test` |
| reported latency == measured group delay | FR-012 / SC-004 | `oversampler-latency-test` |
| higher factor never worsens aliasing | FR-010 / SC-007 | `oversampler-aliasing-test` |
| no heap alloc / no locks in process() | FR-013 / SC-005 | `no-allocation-test` (extended) |
| saturation oversampled < naive, selectable | FR-017..021 / SC-006 | `saturation-aliasing-test` (modified) |
