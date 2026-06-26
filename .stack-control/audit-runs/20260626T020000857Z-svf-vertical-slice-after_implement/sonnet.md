I'll audit this chunk carefully for correctness bugs, design issues, missed edge cases, and code-quality concerns.

### Aligned `operator new` overloads absent from `allocation-sentinel.cpp`

Finding-ID: AUDIT-BARRAGE-claude-01
Status:     open
Severity:   medium
Surface:    tests/support/allocation-sentinel.cpp:25–54

C++17 defines separate replaceable operators for over-aligned types: `operator new(std::size_t, std::align_val_t)`, `operator new[](std::size_t, std::align_val_t)`, and their `noexcept` delete counterparts. Audio DSP code commonly uses `alignas(16/32)` for SIMD registers or cache-line sizing. `allocation-sentinel.cpp` only replaces the four non-aligned variants (lines 25–54). Any aligned allocation in the audio path would silently pass through to the runtime's allocator without incrementing `g_allocations`, yielding a false-clean result from the no-allocation invariant test (FR-014). The fix is to add the four C++17 aligned new/delete overloads alongside the existing ones.

---

### Nothrow `operator new` not intercepted by `allocation-sentinel.cpp`

Finding-ID: AUDIT-BARRAGE-claude-02
Status:     open
Severity:   low
Surface:    tests/support/allocation-sentinel.cpp:25–54

`operator new(std::size_t, const std::nothrow_t&)` and its array variant are replaceable but are not overridden. Some standard-library internals (e.g., `std::vector` reallocation paths on certain libstdc++ versions) route through the nothrow form. Allocations on this path are invisible to the sentinel, creating a silent blind spot in the no-allocation invariant. Fix: add `void* operator new(std::size_t, const std::nothrow_t&) noexcept` and its array counterpart, delegating to the throw-variant (so `g_allocations` is always incremented) then returning `nullptr` on failure rather than propagating an exception.

---

### NaN-value assertions are incomplete for non-logarithmic descriptors in `parameter-test.cpp`

Finding-ID: AUDIT-BARRAGE-claude-03
Status:     open
Severity:   medium
Surface:    tests/core/parameter-test.cpp:69–79

The test comment at lines 70–72 states a contract: "NaN must map to the minimum (0 normalized), not pass through and poison the filter state." This is asserted explicitly only for `logCutoff` (lines 75–79). For `linearRes` and `discreteMode`, the loop at lines 73–76 only checks `std::isfinite(denormalize(d, nan))` — not the specific value. An implementation that returns, say, `0.7f` for `denormalize(linearRes, NaN)` (e.g., a half-range default or an uninitialized value) would pass the test while violating the stated contract. The missing assertions are:

```cpp
CHECK(denormalize(linearRes, nan) == doctest::Approx(0.0f));
CHECK(denormalize(discreteMode, nan) == doctest::Approx(0.0f));
```

This matters because a mid-range NaN escape into the resonance parameter could destabilise the SVF filter, which is the exact failure mode the test is meant to guard against.

---

### Bandpass test asserts only relative ordering, not minimum usable gain

Finding-ID: AUDIT-BARRAGE-claude-04
Status:     open
Severity:   low
Surface:    tests/core/svf-test.cpp:81–92

The `"bandpass emphasizes the centre relative to both edges"` test only checks `centre > low` and `centre > high`. An implementation where the centre gain is `0.01` and the edge gains are `0.001` would pass. There is no assertion that the bandpass passes any meaningful signal near cutoff. The lowpass and highpass tests have explicit bounds (`kPassbandGainMin` and `kStopbandGainMax`), so the asymmetry is conspicuous. A reasonable addition is:

```cpp
CHECK(centre >= kPassbandGainMin);
```

This ensures the filter is actually passing the band, not merely attenuating everything slightly less at centre.

---

### `MonoDriver` sets resonance via raw normalized value but cutoff/mode via `normalize()` — inconsistency risks silent misconfiguration

Finding-ID: AUDIT-BARRAGE-claude-05
Status:     open
Severity:   low
Surface:    tests/core/svf-test.cpp:37–45

In the `MonoDriver` constructor, cutoff and mode are mapped through `normalize()` before being passed to `setParameter()`, reflecting the fact that `setParameter` takes a normalized [0, 1] value. Resonance, however, is passed as `resonanceNorm` directly (line 40), which is documented by the parameter name — the caller is expected to supply an already-normalized value.

The inconsistency makes the constructor's interface asymmetric without documentation. Call sites (e.g., `MonoDriver ring{SvfMode::bandpass, /*resonanceNorm=*/0.99f}`) must know the resonance is normalized while cutoff and mode are physical values. A future test author writing `MonoDriver{SvfMode::lowpass, 8000.0f}` to mean "8 kHz resonance" would silently pass a clipped `1.0f` resonance. Adding a brief comment to the constructor declaration — or making all three parameters consistently normalized or consistently physical — would close this trap.