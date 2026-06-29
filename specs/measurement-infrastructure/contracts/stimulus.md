# Contract — Stimulus generators (tests/support/measurement)

Host-side, allocation-OK (offline) generators that fill a caller-provided span with a known
input signal. `namespace acfx::measure`. No platform headers.

## Shape

```cpp
namespace acfx::measure {
// Each generator fills `out` (size N) with its signal; pure, deterministic.
struct ImpulseGenerator { float amplitude = 1.0f; void fill(span<float> out) const noexcept; };
struct StepGenerator    { float level = 1.0f;      void fill(span<float> out) const noexcept; };
struct SineGenerator    { double freqHz, sampleRate; float amplitude = 1.0f; double phase = 0.0;
                          void fill(span<float> out) const noexcept; };
struct SweepGenerator   { double f0Hz, f1Hz, sampleRate; bool logarithmic = true;
                          void fill(span<float> out) const noexcept; };
struct NoiseGenerator   { float amplitude = 1.0f; std::uint32_t seed = 0x1234u; // deterministic xorshift
                          void fill(span<float> out) const noexcept; };
} // namespace acfx::measure
```

## Normative behavior
- **Deterministic**: identical parameters (incl. seed) produce identical output — required for
  stable assertions and reproducible CSV rows.
- **Self-contained**: a generator only writes its signal; it knows nothing about effects or
  analyzers (single-purpose; FR-003).
- **Sample-rate aware** where applicable (sine/sweep frequencies are relative to `sampleRate`).
- Forward-looking `MultiToneGenerator`/`MLSGenerator` follow the same `fill(span<float>)` shape
  (captured; not implemented in the first cut).

## Test obligations
- Sine/step/impulse content matches closed-form values at sampled points.
- Noise is reproducible for a fixed seed and within `[-amplitude, amplitude]`.
