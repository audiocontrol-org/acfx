# Contract — Metrics + opt-in CSV report (tests/support/measurement)

Metrics derive reported/asserted quantities from analyzer results. CI gates on assertions vs
analytic/named-tolerance bounds; the CSV report is opt-in. `namespace acfx::measure`.

## Metric functions (compositions over capture + analyzers)

```cpp
namespace acfx::measure {
double magnitude(span<const float> out, double freqHz, double sampleRate);   // gain ratio at freq
double phaseRad (span<const float> in, span<const float> out, double freqHz, double sampleRate);
double thd      (span<const float> out, double fundamentalHz, double sampleRate, int harmonics = 5);
int    latencySamples(span<const float> in, span<const float> out);
struct ExecCost { double timePerBlock; int blockSize; };                     // desktop-relative proxy
template <class FX> ExecCost relativeExecTime(FX& fx, const ProcessContext& ctx, int blockSize, int repeats);
struct Stability { bool ok; const char* failedCase; };                       // silence/DC/denormal/idle + NaN/Inf/denormal scan
template <class FX> Stability stability(FX& fx, const ProcessContext& ctx);
// allocation: reuse tests/support/allocation-sentinel directly (assert count == 0).
} // namespace acfx::measure
```

## Normative behavior
- **No false precision (FR-013)**: tests assert metrics against analytic truths + named
  tolerances (passband≈unity, stopband attenuated, `thd < kLinearThdMax`, `latency == known ± tol`),
  never fabricated exact magnitudes.
- **relative execution time (FR-010)**: `ExecCost` is a desktop-relative host time-per-block
  (median of `repeats`), carrying `blockSize`; it is NOT absolute hardware/MCU cycles and must be
  labeled as such wherever surfaced.
- **stability (FR-012)**: returns a pass/fail plus which case failed; covers silence-in→
  silence-out (within an idle-noise-floor tolerance), DC-offset, denormal-prone input, idle
  noise-floor, and a NaN/Inf/denormal + bounds scan.
- **Allocation (FR-011)**: not re-implemented — reuse `AllocationSentinel` around `process()`.

## CSV report (opt-in — FR-014)

```cpp
namespace acfx::measure {
// Canonical CSV schema (defined ONCE here — FR-014). Long/normalized form: one row per
// measured metric, so reports are directly plottable/trendable and never ad-hoc per call site.
struct MeasurementRow {
  std::string effect;      // effect identifier under test (e.g. "svf-lowpass")
  std::string metric;      // metric name (e.g. "magnitude", "thd", "latency", "relative_exec_time")
  std::string stimulus;    // stimulus used (e.g. "sine@1kHz", "impulse")
  double      sampleRate;  // Hz
  int         blockSize;   // samples (relevant to relative_exec_time; 0/NA otherwise)
  double      value;       // the measured value
  std::string units;       // e.g. "ratio", "dB", "radians", "samples", "ms", "time/block", "count"
  double      tolerance;   // the named tolerance asserted against
  bool        pass;        // pass/fail vs the analytic reference bound
};
class CsvReport {                       // opt-in; OFF by default — assertions gate CI regardless
public:
  void add(const MeasurementRow&);
  void write(const std::string& path) const;   // header: the field names above; one row per metric
};
} // namespace acfx::measure
```

- **Canonical header (fixed order)**: `effect,metric,stimulus,sample_rate,block_size,value,units,tolerance,pass`.
- When report emission is **off** (default), no file is written and CI relies solely on the
  doctest assertions. When **on**, a well-formed CSV using exactly this schema is written for
  trending/plotting (the seam labs later reuse — Principle IX). The schema is defined once here
  so every call site emits the same columns (FR-014).

## Test obligations
- Each metric asserted vs an analytic reference within a named tolerance on a known effect.
- THD≈0 (linear) / elevated (known nonlinearity); latency == known delay ± tol.
- Stability verdicts correct for each special case; allocation == 0.
- CSV emission produces a well-formed file; default-off writes nothing.
