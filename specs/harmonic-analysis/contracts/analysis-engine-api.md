# Contract — Host Analysis Engine (`host/analysis/`)

Host/desktop only. May allocate at construction/`init()`; runs **only off the audio thread**. Namespace `acfx::analysis` (host). Shared verbatim by the offline test suites and the workbench + plugin live readouts — the one-engine guarantee (FR-014/015). **Never reachable from portable `core/`** (Constitution IV).

## Building blocks

```
// window.h
enum class WindowKind { BlackmanHarris /*default*/, Hann, FlatTop };   // FR-025
struct Window { WindowKind kind; /* init-time coeffs */ };

// fft.h — windowed radix-2, power-of-two only
class Fft {
  explicit Fft(int size);            // size MUST be a power of two, else throws a descriptive error (FR-026)
  void forward(span<const float> in, span<Complex> out) const;         // applies the configured window
};
```

## Metric surface (each validated against an analytic reference within a named tolerance — V, X)

```
// spectrum.h  (FR-001, US1)
HarmonicSpectrum harmonicSpectrum(Signal in, double fundamentalHz, int numHarmonics);
//   per-harmonic magnitude AND phase; out-of-band harmonics not-measured; sub-floor phase → NaN

// thdn.h  (FR-002, US1)
ThdnResult thdPlusN(Signal in, double fundamentalHz);
//   residual method: RMS(non-fundamental)/RMS(fundamental); noiseFloor; snr vs fundamental; NaN if no fundamental

// imd.h  (FR-003, US2)
ImdResult imd(EffectOrCallable fx, ImdMethod method /* SMPTE | CCIF */);
//   difference AND sum products; twin-tone stimulus built internally

// alias-sweep.h  (FR-004, US2)
AliasSweepCurve aliasSweep(EffectOrCallable fx, FrequencyRange sweep);
//   inharmonic (folded) energy vs swept frequency; reuses the integer-cycle inharmonic measure

// drive-series.h  (FR-005, US3)
DriveSeries driveSeries(EffectFactory fx, DriveRange drive, int numHarmonics);
//   drive→THD and drive→per-harmonic curves

// retained exact known-bin path (FR-007/010)
double goertzelBin(Signal in, double freqHz, double sampleRate);   // integer-cycle, unwindowed, leakage-free
```

- **Effect-agnostic** (FR-006): every entry takes an `Effect`-contract implementation or a per-sample callable; no effect-specific code.
- **Unmeasurable → NaN** (FR-008): no fabricated 0.0 for a missing fundamental or an out-of-band harmonic.
- **Live/offline parity** (FR-015): the workbench/plugin readout calls these same functions on samples drained from the capture probe; a parity test asserts equality with the direct offline call within tolerance.

## Boundary

- Host-only; MUST NOT be `#include`d from `core/`. The RT capture probe (portable) has no dependency on this library.
- The FFT introduced here is a forward seam Phase 8 (Convolution) reuses/supersedes (FR-018); this feature builds no convolution.
