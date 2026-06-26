### `normalize` contract for out-of-range raw values is untested

Finding-ID: AUDIT-BARRAGE-claude-01
Status:     open
Severity:   medium
Surface:    tests/core/parameter-test.cpp:61-68

The test case "out-of-range normalized inputs clamp to the parameter bounds" (lines 61-68) exercises `denormalize` with normalized values outside [0,1] and verifies they clamp to the physical parameter bounds. There is no symmetric test for `normalize` called with a raw value outside `[min, max]`. If `normalize` lacks clamping, an adapter passing an out-of-range physical value (e.g., a DAW host automating to a valid-to-it but out-of-spec value) would produce a normalized value outside [0,1]. That in turn flows back through `denormalize` to produce a raw value that silently breaks the parameter contract. The blast radius is that every adapter that calls `normalize` at a boundary (plugin automations, parameter-recall on load) could silently feed the SVF coefficients an out-of-range frequency or resonance. A test mirroring lines 61-68 but for raw-value inputs — `normalize(linearRes, -0.5f)` → expect 0.0, `normalize(logCutoff, 0.0f)` → expect 0.0, `normalize(logCutoff, 40000.0f)` → expect 1.0 — would close the gap.

---

### `SvfMode` enum-cast assumes 0-based contiguous underlying integer values

Finding-ID: AUDIT-BARRAGE-claude-02
Status:     open
Severity:   medium
Surface:    tests/core/svf-test.cpp:42-44

```cpp
const float modeIndex = static_cast<float>(static_cast<int>(mode));
fx.setParameter(ParamId{SvfEffect::kMode},
                normalize(SvfEffect::kParams[SvfEffect::kMode], modeIndex));
```

`MonoDriver` converts `SvfMode` to its underlying integer to produce the raw value passed to `normalize`. This is correct only if `static_cast<int>(SvfMode::lowpass) == 0`, `highpass == 1`, `bandpass == 2` — exactly the index range that `SvfEffect::kParams[kMode]` expects (as established by the `discreteMode` descriptor with min=0, max=2, count=3 in `parameter-test.cpp`). The `SvfMode` enum definition is in `core/effects/svf/svf-effect.h` (a different chunk, not in scope here). If the enum uses explicit non-zero or non-contiguous values (a common defensive practice), the cast produces a wrong index, and `normalize` maps it to an incorrect normalized value. All three SVF mode tests would then pass while actually exercising the wrong filter path — the tests become vacuous. A safer design is a `toParamIndex(SvfMode)` helper defined alongside the enum so the coupling is explicit and not duplicated across the test and any other adapter that needs to map mode to a raw value.

---

### `AllocationSentinel` thread-local contract is not enforced; test could be vacuous if threads diverge

Finding-ID: AUDIT-BARRAGE-claude-03
Status:     open
Severity:   medium
Surface:    tests/support/allocation-sentinel.h:9-19, tests/support/allocation-sentinel.cpp:13-24

`reset()`, `allocations()`, and `deallocations()` all read/write thread-local variables. The contract documented in the header — "on this thread since the last reset()" — requires that all three calls execute on the same thread as the audio-processing loop under test. `no-allocation-test.cpp` (chunk 1d366441c57c4606, not in scope) is the only consumer. If that test calls `reset()` on the doctest driver thread and then simulates audio processing on a different thread — a common pattern in RT testing where the audio callback runs on a dedicated thread — the audio thread's counter is never zeroed and never read, and the assertion `allocations() == 0` on the driver thread trivially passes while the audio thread allocates freely. There is no assertion or debug check that the thread ID at `reset()` matches the thread ID at `allocations()`. A minimal guard — storing `std::this_thread::get_id()` at `reset()` time and asserting it matches in `allocations()` — would turn a silent vacuous test into a loud misuse error. Without seeing `no-allocation-test.cpp`, whether this is currently wrong is unverifiable; the sentinel itself provides no protection.

---

### Resonance stability test covers only bandpass; lowpass and highpass at high Q are untested

Finding-ID: AUDIT-BARRAGE-claude-04
Status:     open
Severity:   low
Surface:    tests/core/svf-test.cpp:89-102

The "high resonance stays NaN/denormal-free and bounded" test instantiates only a `SvfMode::bandpass` driver at `resonanceNorm=0.99f`. The stability requirement (finite output, bounded amplitude) applies to all three filter modes. A future regression that destabilizes the lowpass or highpass path at high Q — e.g., a coefficient recalculation error in those branches — would not be caught. The three modes share the same state-variable update topology, so the marginal cost of adding two more impulse-response loops (one per untested mode) is low relative to the confidence gained. The bound of 100.0f (line 101) is also a magic number with no comment linking it to an analytic stability bound; a comment explaining the expected self-oscillation amplitude for this Q would help reviewers calibrate whether the bound is tight enough.

---

### `measureMagnitude` divides by `inSq` without a zero-guard; `freqHz = 0` silently produces NaN

Finding-ID: AUDIT-BARRAGE-claude-05
Status:     open
Severity:   low
Surface:    tests/support/svf-reference.h:40-41

```cpp
return std::sqrt(outSq / inSq);
```

When `freqHz == 0`, `w == 0`, every sine sample is `sin(0) == 0`, `inSq` accumulates to zero, and the return value is `sqrt(0.0/0.0)` — a quiet NaN in IEEE 754. The NaN propagates into `CHECK` comparisons, which fail with a numeric result that gives no indication the root cause is a zero-frequency input. No current call site passes `freqHz = 0`, so this is latent, but `measureMagnitude` is a public template function in a shared support header. A future test author passing an uninitialized or computed frequency variable could produce silent NaN-based spurious failures. A `REQUIRE(freqHz > 0)` at the top of the function (using doctest's REQUIRE so the test aborts with a clear message) would catch the misuse immediately.

---

### Highpass test uses lowpass-perspective frequency constants with semantically inverted roles

Finding-ID: AUDIT-BARRAGE-claude-06
Status:     open
Severity:   informational
Surface:    tests/core/svf-test.cpp:65-75

`kPassbandFreqHz` (100 Hz) and `kStopbandFreqHz` (8000 Hz) are named and documented from the lowpass filter's perspective in `svf-reference.h`. The highpass test uses the same constants but with inverted semantics: 100 Hz is the highpass *stopband* (should attenuate), and 8000 Hz is the highpass *passband* (should pass). The test logic is correct — the CHECK thresholds are applied consistently — but reading the highpass case in isolation, `measureMagnitude(hpLow, kPassbandFreqHz, ...)` sounds like it is measuring a passband gain for the highpass, when it is in fact measuring the attenuation of the highpass's stopband. Renaming the constants to `kLowFreqHz` / `kHighFreqHz` (removing the lowpass-specific pass/stop framing) would make both the lowpass and highpass tests self-explanatory without requiring knowledge of which filter mode is the reference.