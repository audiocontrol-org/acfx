# Contract — Analyzers + the effect-agnostic capture seam (tests/support/measurement)

Analyzers reduce a captured output (and, where needed, the input) to raw analysis results.
Distinct from metrics (which derive reported quantities). `namespace acfx::measure`.

## Capture seam (effect-agnostic — FR-004)

```cpp
namespace acfx::measure {
// Run an Effect (prepare once at ctx, process in blocks) over `in` into `out` (same size).
template <class FX> void capture(FX& fx, const ProcessContext& ctx, span<const float> in, span<float> out);
// Run any per-sample callable float(float) over `in` into `out`.
template <class Fn> void captureCallable(Fn&& fn, span<const float> in, span<float> out);
} // namespace acfx::measure
```

Both are the only effect-aware code in the harness; everything else operates on `span<float>`.

## Analyzers

```cpp
namespace acfx::measure {
struct ImpulseAnalyzer {                    // for an impulse-stimulus output
  static int   peakIndex(span<const float> out) noexcept;        // latency seed
};
struct GoertzelAnalyzer {                   // single-bin DFT — magnitude + phase at one freq
  double freqHz, sampleRate;
  struct Bin { double magnitude; double phaseRad; };
  Bin analyze(span<const float> out) const noexcept;            // freq response (mag+phase), THD harmonics
};
struct CorrelationAnalyzer {                // delay via peak cross-correlation
  static int lagSamples(span<const float> in, span<const float> out) noexcept;
};
// FFTAnalyzer — forward-looking (Phase 8); same "reduce to raw results" role.
} // namespace acfx::measure
```

## Normative behavior
- **Single-purpose**: an analyzer produces raw results only; THD/latency/etc. are derived by
  metrics from these (FR-003).
- **Settling**: callers discard a settling prefix for steady-state analyzers (Goertzel on a
  settled region), matching `measureMagnitude`.
- **Allocation-OK**: offline; may use buffers (this is NOT the audio path).
- Goertzel gives both magnitude and phase at its bin (drives frequency + phase response and the
  per-harmonic powers for THD).

## Test obligations
- Goertzel magnitude/phase match closed-form for a pure sine at the bin frequency.
- ImpulseAnalyzer peak index and CorrelationAnalyzer lag match a known delay.
