# Phase 1 Data Model: Measurement Infrastructure

**Feature**: [spec.md](./spec.md) | **Plan**: [plan.md](./plan.md) | **Date**: 2026-06-29

Host-side test/support types (`tests/support/measurement/`). Allocation-friendly buffers are
fine here (this is offline analysis, NOT the audio path). `namespace acfx::measure`.

## Entities

### Stimulus generator
Produces a known input signal into a caller-provided buffer/span.
| Generator | Parameters | Output |
|---|---|---|
| `ImpulseGenerator` | length, amplitude | unit impulse then zeros |
| `StepGenerator` | length, level | step |
| `SineGenerator` | freq, sampleRate, length, amplitude, phase | sine |
| `SweepGenerator` | f0, f1, sampleRate, length | log/linear sweep |
| `NoiseGenerator` | length, amplitude, seed | deterministic white noise (seedable PRNG) |
| `MultiToneGenerator`, `MLSGenerator` | — | **forward-looking** (captured, not first cut) |

### Analyzer
Reduces a captured output (and, where needed, the input) to raw results.
| Analyzer | Input | Raw result |
|---|---|---|
| `ImpulseAnalyzer` | output of an impulse | impulse-response samples; peak index |
| `GoertzelAnalyzer` | output samples, bin freq, sampleRate | complex/magnitude at that bin |
| `CorrelationAnalyzer` | input + output | lag of peak cross-correlation (delay) |
| `FFTAnalyzer` | — | **forward-looking** (Phase 8) |

### Metric (derived, asserted/reported)
| Metric | Derived from | Value |
|---|---|---|
| magnitude | sine + Goertzel/RMS | gain ratio (or dB) at a frequency |
| frequency response | sweep of magnitudes | curve (freq → magnitude) |
| phase | Goertzel complex bin / correlation | phase shift (radians/deg) at a frequency |
| impulse response | ImpulseAnalyzer | sample series |
| THD | GoertzelAnalyzer over harmonics | total harmonic distortion ratio |
| latency | ImpulseAnalyzer peak / CorrelationAnalyzer lag | samples (and ms) |
| relative execution time | timed `process()` repeats | desktop-relative time per block (+ block size) |
| allocation | `AllocationSentinel` around `process()` | allocation count (assert 0) |
| stability verdict | output scan + special-case stimuli | pass/fail + which case failed |

### Measurement run + capture helper
- `capture(effect_or_callable, stimulus_span, out_span)` — runs an `Effect` (via prepare/process
  over blocks) OR a per-sample `float(float)` callable across the stimulus into the output buffer.
  The single effect-agnostic seam (FR-004).
- A `MeasurementRow` is one normalized record (one per measured metric) for the optional CSV report — canonical schema in `contracts/metrics.md` (effect, metric, stimulus, sample rate, block size, value, units, tolerance, pass).

### Reference bound
An analytic truth + named tolerance a metric is asserted against (e.g. `kPassbandMinGain`,
`kStopbandMaxGain`, `kLinearThdMax`, `kLatencyTolSamples`) — the `svf-reference` pattern.
No fabricated exact numbers.

## Validation rules (mapped to requirements)
- Effect-agnostic capture works for `Effect` and callable (FR-004, SC-001).
- Magnitude/impulse/phase asserted vs analytic refs within tolerance (FR-005/006/007, SC-002).
- THD ≈0 for linear, elevated for known nonlinearity; latency matches known delay (FR-008/009, SC-003).
- Relative-exec-time labeled desktop-relative + block size recorded (FR-010, SC-004).
- Allocation = 0 via sentinel (FR-011, SC-004).
- Stability: silence/DC/denormal/idle verdicts correct, no NaN/Inf/denormal (FR-012, SC-004).
- Assertions gate CI; CSV opt-in/off-by-default (FR-013/014, SC-005).
- Host-side only, platform-independent, no new dep, no general FFT (FR-015/016/019, SC-006).
- All 8 metrics represented (FR-005–012, SC-007).
