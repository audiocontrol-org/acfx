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
struct MeasurementRow { std::string name; double magnitude, phaseRad, thd, latencySamples,
                        execTimePerBlock; int blockSize; bool stable; long allocations; };
class CsvReport {                       // opt-in; OFF by default — assertions gate CI regardless
public:
  void add(const MeasurementRow&);
  void write(const std::string& path) const;   // well-formed CSV (header + rows)
};
} // namespace acfx::measure
```

- When report emission is **off** (default), no file is written and CI relies solely on the
  doctest assertions. When **on**, a well-formed CSV (header + one row per run) is written for
  trending/plotting (the seam labs later reuse — Principle IX).

## Test obligations
- Each metric asserted vs an analytic reference within a named tolerance on a known effect.
- THD≈0 (linear) / elevated (known nonlinearity); latency == known delay ± tol.
- Stability verdicts correct for each special case; allocation == 0.
- CSV emission produces a well-formed file; default-off writes nothing.
