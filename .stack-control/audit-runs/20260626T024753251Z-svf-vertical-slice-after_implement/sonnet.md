I'll work through the diff carefully and emit findings per the audit-barrage format.

---

### `measureMagnitude` has no guard against division by zero when `inSq` is 0

Finding-ID: AUDIT-BARRAGE-claude-01
Status:     open
Severity:   medium
Surface:    tests/support/svf-reference.h:36

`measureMagnitude` accumulates `inSq += in * in` over the measure window then returns `std::sqrt(outSq / inSq)`. If `freqHz` is exactly 0, or is an integer multiple of `sampleRate`, `std::sin(w * n)` evaluates to zero for every sample and `inSq` remains 0. The return then produces `sqrt(inf)` or NaN rather than a meaningful ratio. No guard or assertion protects against this.

The immediate blast radius is moderate: the current call sites use hard-coded frequencies (100 Hz, 1000 Hz, 8000 Hz at 48 kHz) that are not multiples of the sample rate, so the bug is dormant today. But `measureMagnitude` is a public template in a shared support header. Any future test author who calls it with `freqHz = 0` (a plausible "DC response" check) or who rounds a frequency to a sample-rate multiple will silently receive a NaN magnitude, which doctest's `Approx` comparisons propagate as a passing check (NaN comparisons return false, so `CHECK(NaN >= 0.7)` fails — but the failure message will be a non-obvious magnitude value, not "division by zero"). A simple guard — `assert(inSq > 0)` or an early-return — would make the failure explicit.

---

### SVF frequency-response tolerances are too loose to catch a significantly broken filter

Finding-ID: AUDIT-BARRAGE-claude-02
Status:     open
Severity:   medium
Surface:    tests/support/svf-reference.h:43–47, tests/core/svf-test.cpp:67–82

`kPassbandGainMin = 0.7` (≈ −3 dB) and `kStopbandGainMax = 0.25` (≈ −12 dB) are the only absolute bounds the LP/HP tests enforce. For a 2nd-order filter at 1 kHz cutoff: at 100 Hz the correct LP passband gain is within a few percent of 1.0; the 0.7 floor would pass a filter with −3 dB of passband loss — severe degradation. At 8 kHz (3 octaves above cutoff), 2nd-order rolloff predicts gain ≈ (1000/8000)² ≈ 0.016; the 0.25 ceiling is ~12 dB above that. A broken or unstable filter could produce gain of 0.20 at 8 kHz and still pass the test.

The bandpass test (svf-test.cpp:84–95) is weaker still — it only asserts relative ordering (`centre > low`, `centre > high`), so any monotonically-peaked response qualifies regardless of how narrow or broad the peak is. The comment in `svf-reference.h:43` calls these "generous" and "analytic truths," but the tolerances are loose enough that the tests no longer verify a correctly-tuned filter; they verify only that the implementation has the right general shape. Tightening to ±1 dB in the passband and −20 dB in the stopband would make these tests a meaningful regression gate.

---

### NaN→minimum contract is documented but only partially verified

Finding-ID: AUDIT-BARRAGE-claude-03
Status:     open
Severity:   low
Surface:    tests/core/parameter-test.cpp:76–88

The comment at line 78 states the contract: "NaN must map to the minimum (0 normalized), not pass through and poison the filter state." The test then verifies the exact NaN result only for `logCutoff` (lines 85–87). For `linearRes` and `discreteMode`, only `std::isfinite` is checked (lines 81–83) — any finite value satisfies those assertions, including a value far from the minimum. If `denormalize(linearRes, NaN)` silently returned 0.5 (the midpoint), all three `isfinite` checks would still pass.

Blast radius: the contract matters because NaN inputs to a parameter would otherwise propagate into filter coefficients and corrupt audio. The test claims to guard against this but leaves two of the three descriptors underspecified. Extending lines 85–87 to include `linearRes` and `discreteMode` would close the gap.

---

### Resonance stability bound (`< 100.0f`) does not catch a blowing-up filter

Finding-ID: AUDIT-BARRAGE-claude-04
Status:     open
Severity:   low
Surface:    tests/core/svf-test.cpp:100

`CHECK(maxAbs < 100.0f)` in the high-resonance ring-down test is the only bound on output magnitude. A real SVF at resonanceNorm=0.99 near the Nyquist stability boundary can oscillate with gain that grows into the tens before the check fires. An impulse into a marginally unstable filter could reach gain 50–80 and still pass. The test would catch catastrophic blowup (output clips to ±∞) but would miss a real-world failure mode where the filter self-oscillates with bounded but large amplitude — which is also undesirable in an embedded audio context where headroom is limited and clipping is damaging.

A bound of `< 2.0f` (or even `< 10.0f`) would be a meaningful near-unity ceiling for a bandpass ring that should not amplify above the input. The 200,000 sample window (≈4 s at 48 kHz) is appropriate; only the magnitude threshold needs tightening.

---

### `kPassbandFreqHz`/`kStopbandFreqHz` names are biased toward lowpass and mislead in the highpass test

Finding-ID: AUDIT-BARRAGE-claude-05
Status:     open
Severity:   low
Surface:    tests/support/svf-reference.h:39–47, tests/core/svf-test.cpp:75–82

`kPassbandFreqHz = 100.0` and `kStopbandFreqHz = 8000.0` are named from the lowpass perspective; the comment at line 39 reads "Passband: a decade below cutoff should pass at roughly unity gain." In the highpass test (svf-test.cpp:75–82), the same constants are used with their roles swapped: `kPassbandFreqHz` is now the stopband frequency (100 Hz is attenuated by the HP filter) and `kStopbandFreqHz` is the passband frequency. The test logic is correct, but a reader applying the constant names as documentation will build an inverted mental model. Neutral names like `kLowTestFreqHz` / `kHighTestFreqHz` would prevent this confusion without changing behavior.

---

### Missing `nothrow` overloads in `allocation-sentinel.cpp` create false negatives for RT no-alloc tests

Finding-ID: AUDIT-BARRAGE-claude-06
Status:     open
Severity:   low
Surface:    tests/support/allocation-sentinel.cpp:28–54

The sentinel overrides the six standard throwing `operator new`/`delete` forms but omits the two `nothrow` variants:

```cpp
void* operator new(std::size_t, const std::nothrow_t&) noexcept;
void* operator new[](std::size_t, const std::nothrow_t&) noexcept;
```

Any allocation via `new(std::nothrow)` will not increment `g_allocations`, so the no-allocation invariant test (FR-014) would pass silently even if the audio path used nothrow allocation. In practice, `new(std::nothrow)` is rare in DSP code, and the project constitution forbids heap in the RT path — so this is an unlikely active bug. However, third-party library code (JUCE, doctest internals, or future dependencies) might use nothrow forms on the calling thread; the missing overrides mean those allocations go uncounted if they fall within a measured region. Adding the two missing overloads closes the false-negative path entirely.