I'll review the diff carefully for chunk `31c30149ec9faef5` covering the test files.

The audit-barrage prompt was delivered in the original system message — that IS the skill payload. I have all the material needed. Proceeding with the audit now.

---

### NaN contract only partially asserted — linear and discrete modes not checked against their minimum

Finding-ID: AUDIT-BARRAGE-claude-01
Status:     open
Severity:   medium
Surface:    tests/core/parameter-test.cpp:75–87

The test case "non-finite normalized inputs are neutralized, never propagated" contains the comment "NaN must map to the minimum (0 normalized)" and loops over all three descriptors to verify finiteness (lines 78–83). However, only `logCutoff` is subsequently checked against a specific boundary value (lines 84–86). For `linearRes` (min=0.0f) and `discreteMode` (min=0.0f / step 0), the test asserts only `std::isfinite()` — not that the result equals the descriptor minimum.

This is a real test-contract gap. `std::clamp(NaN, lo, hi)` has implementation-defined behavior for NaN because both `NaN < lo` and `hi < NaN` are false in IEEE 754: a naive clamp returns NaN unchanged. If `denormalize` correctly pre-guards with `if (!std::isfinite(n)) n = 0;` then `denormalize(linearRes, NaN)` returns `0.0f` and `isfinite` passes — but the test cannot distinguish "correctly maps NaN → min" from "maps NaN → 0.5" (the midpoint, which is also finite). If the implementation maps NaN to the maximum rather than the minimum (e.g., due to a sign error in the guard), the test passes silently for linear and discrete. Blast radius: downstream code that relies on "NaN always folds to minimum" builds on an unverified contract for two of the three parameter kinds. Fix: add explicit `== doctest::Approx(0.0f)` and `== doctest::Approx(1.0f)` boundary checks for `linearRes` and `discreteMode` matching the pattern already applied to `logCutoff` on lines 84–86.

---

### Out-of-range normalized inputs not tested for the discrete parameter kind

Finding-ID: AUDIT-BARRAGE-claude-02
Status:     open
Severity:   low
Surface:    tests/core/parameter-test.cpp:68–73

The "out-of-range normalized inputs clamp to the parameter bounds" test exercises `linearRes` with -0.5 and 1.5, and `logCutoff` with -1.0 and 2.0. `discreteMode` is absent. For discrete parameters the concern is different: a naive implementation using `floor(n * numSteps)` with no clamping yields `floor(-0.5 * 3) = -2` (a negative index) or `floor(1.5 * 3) = 4` (an out-of-bounds index). If the discretization formula clamps the normalized value before flooring, these produce 0 and 2 respectively; if it clamps after flooring, the result depends on integer vs. float clamp order. Neither path is tested. Blast radius: a discrete parameter passed a slightly-out-of-range normalized value from the plugin host (VST3 hosts are not guaranteed to stay within [0,1]) could produce an out-of-bounds mode index that is silently used as an array subscript elsewhere.

---

### `kStopbandGainMax = 0.25` too permissive to validate 2nd-order rolloff

Finding-ID: AUDIT-BARRAGE-claude-03
Status:     open
Severity:   medium
Surface:    tests/support/svf-reference.h:41–47

At `kStopbandFreqHz = 8000 Hz` with `kRefCutoffHz = 1000 Hz` the frequency ratio is 8× (3 octaves above cutoff). A correct 2nd-order lowpass has ≈40 dB/decade rolloff: at 8× the cutoff the expected magnitude is `1/(8² ) = 1/64 ≈ 0.016`. The threshold `kStopbandGainMax = 0.25` (-12 dB) is 16× more generous than the theoretical 2nd-order response. A 1st-order lowpass at this point yields `1/√(1 + 64) ≈ 0.124`, which is below 0.25 and would pass this check. The passband threshold `kPassbandGainMin = 0.7` (-3 dB) at `kPassbandFreqHz = 100 Hz` (a decade below cutoff) has the same character: the actual passband gain of a correct 2nd-order filter there is >0.9999, so the floor of 0.7 would accept a heavily rolled-off implementation.

The thresholds are acknowledged as "generous" in the comments, and the stated intent is to capture only "analytic truths." However, at this looseness a regression to first-order behavior — or a filter with a substantially mistuned coefficient — goes undetected. The stopband test in particular provides no discriminating power over a 1st-order filter. Blast radius: a broken `SvfEffect` that internally degrades to first-order behavior would pass all four frequency-response test cases and ship silently. Fix: tighten `kStopbandGainMax` to at most `0.05` (≈ -26 dB), which a correct 2nd-order filter comfortably satisfies while failing a 1st-order regression.

---

### `ParamId` and `kParams[]` index conflated in `MonoDriver` — silent mismatch if they diverge

Finding-ID: AUDIT-BARRAGE-claude-04
Status:     open
Severity:   medium
Surface:    tests/core/svf-test.cpp:38–45

`MonoDriver` calls:
```cpp
fx.setParameter(ParamId{SvfEffect::kCutoff},
                normalize(SvfEffect::kParams[SvfEffect::kCutoff], ...));
fx.setParameter(ParamId{SvfEffect::kResonance}, resonanceNorm);
fx.setParameter(ParamId{SvfEffect::kMode},
                normalize(SvfEffect::kParams[SvfEffect::kMode], modeIndex));
```

`SvfEffect::kCutoff`, `kResonance`, and `kMode` are used simultaneously as `ParamId` values and as array subscripts into `kParams[]`. This is correct only if `ParamId` values are exactly equal to their position in `kParams`. If `SvfEffect` were ever refactored so that, for example, `kCutoff = ParamId{0}` but `kParams[1]` describes the cutoff, the test would silently set the cutoff `ParamId` using the resonance descriptor's normalization and vice versa. The test would still compile and run, but would configure the filter incorrectly, producing frequency-response numbers that could still satisfy the loose thresholds in finding-03. The definition of `kParams` and the `ParamId` values live in other chunks (`core/effects/svf/svf-effect.h`), so the coupling is implicit and non-local. Blast radius: any of the four frequency-response tests could pass with a misconfigured filter.

---

### Allocation sentinel does not override `std::nothrow_t` new overloads

Finding-ID: AUDIT-BARRAGE-claude-05
Status:     open
Severity:   low
Surface:    tests/support/allocation-sentinel.cpp:28–35

The sentinel intercepts `operator new(size_t)` and `operator new[](size_t)` (throwing forms) and their corresponding deletes. The nothrow forms — `operator new(size_t, const std::nothrow_t&)` and `operator new[](size_t, const std::nothrow_t&)` — are not overridden. Per the C++ standard the default nothrow overloads call the throwing form internally, so in most hosted implementations the counter still fires. However, on bare-metal toolchains (Daisy/Teensy, both in scope per `adapters/daisy/` and `adapters/teensy/`) the C++ runtime is thin and the nothrow form may be a direct `malloc` call that bypasses the throwing form entirely. Any DSP code that uses `new(std::nothrow)` on those targets would produce a false-negative clean result from the no-allocation test. This is low severity because audio code that uses nothrow new is already in violation of the RT-safety contract and unlikely to appear — but the gap is worth noting given the multi-platform target.

---

### `kPassbandFreqHz` / `kStopbandFreqHz` names are lowpass-centric, mislead highpass test readers

Finding-ID: AUDIT-BARRAGE-claude-06
Status:     open
Severity:   low
Surface:    tests/core/svf-test.cpp:55–68

In the highpass test case the roles of the two frequency constants are inverted: `kPassbandFreqHz = 100 Hz` (a decade below the 1000 Hz cutoff) is used as the *stopband* frequency for a highpass, and `kStopbandFreqHz = 8000 Hz` is used as the *passband* frequency. The assertions are logically correct (`lowGain <= kStopbandGainMax`, `highGain >= kPassbandGainMin`), but a reader scanning the constant names expects `kPassbandFreqHz` to be in the passband and `kStopbandFreqHz` to be in the stopband — which is only true for the lowpass test. The bandpass test has the same issue. The names are defined in `svf-reference.h` with no indication they are lowpass-relative. A future maintainer adding a mode or adjusting thresholds is likely to misread the highpass/bandpass assertions.