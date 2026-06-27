### Missing C++17 aligned-`operator new` overrides leave allocation sentinel with uncounted allocations

Finding-ID: AUDIT-BARRAGE-claude-01
Status:     open
Severity:   medium
Surface:    tests/support/allocation-sentinel.cpp:26–53

`allocation-sentinel.cpp` replaces `operator new(size_t)`, `operator new[](size_t)`, and the corresponding delete variants, but does **not** replace the C++17 aligned forms:

```
operator new(std::size_t, std::align_val_t)
operator new[](std::size_t, std::align_val_t)
operator delete(void*, std::align_val_t)
operator delete[](void*, std::align_val_t)
```

It also omits the `std::nothrow_t` overloads:

```
operator new(std::size_t, std::nothrow_t) noexcept
operator new[](std::size_t, std::nothrow_t) noexcept
```

In a DSP codebase where SIMD alignment is common (`alignas(16)` / `alignas(32)` on audio buffers or coefficient arrays, `std::aligned_alloc`-backed allocators), any heap allocation that goes through an aligned `operator new` call silently bypasses `g_allocations`, and the no-allocation test passes falsely. The whole point of the sentinel is to enforce the RT no-alloc invariant; a gap in the interception layer turns a failing test into a false-clean. The fix is to add the four aligned overrides (and the two nothrow overrides) that delegate to their sized/non-sized counterparts while still incrementing `g_allocations`.

---

### NaN normalization assertions are incomplete: only `logCutoff` has an explicit value check

Finding-ID: AUDIT-BARRAGE-claude-02
Status:     open
Severity:   low
Surface:    tests/core/parameter-test.cpp:70–87

The test comment at line 73 states "NaN must map to the minimum (0 normalized), not pass through and poison the filter state." The loop at lines 76–80 checks only `std::isfinite(denormalize(d, nan|inf|-inf))` for all three descriptors. Explicit value assertions follow immediately, but only for `logCutoff` (lines 82–87). `linearRes` and `discreteMode` receive no `doctest::Approx` check.

An implementation that maps `NaN` to the *maximum* value (1.0 for `linearRes`, 2.0 for `discreteMode`) would satisfy every `std::isfinite` assertion while violating the stated contract ("map to minimum"). The fix is to add:

```cpp
CHECK(denormalize(linearRes, nan) == doctest::Approx(0.0f));
CHECK(denormalize(linearRes, inf) == doctest::Approx(1.0f));
CHECK(denormalize(linearRes, -inf) == doctest::Approx(0.0f));
```

and analogous lines for `discreteMode`. This directly parallels what is already present for `logCutoff`.

---

### Bandpass test verifies only ordering — a degenerate implementation can pass

Finding-ID: AUDIT-BARRAGE-claude-03
Status:     open
Severity:   low
Surface:    tests/core/svf-test.cpp:84–95

The lowpass and highpass test cases bound their gains against `kPassbandGainMin` (0.7) and `kStopbandGainMax` (0.25), giving absolute correctness floors. The bandpass test at lines 88–95 only asserts `centre > low` and `centre > high` — strict ordering, no absolute threshold.

A sufficiently attenuating filter — including a broken one that outputs near-zero uniformly — could produce, for example, `centre = 0.001`, `low = 0.0009`, `high = 0.0008` and pass all three checks while delivering no usable signal. The intent (bandpass emphasizes the centre) is not captured by ordering alone. Adding a check like `CHECK(centre >= kPassbandGainMin)` mirrors what the other two modes assert and catches degenerate cases the ordering check misses.

---

### Single-sample `AudioBlock` constructed from a scalar `float`; fragile against minimum-block-size assumptions

Finding-ID: AUDIT-BARRAGE-claude-04
Status:     open
Severity:   informational
Surface:    tests/core/svf-test.cpp:47–51

`MonoDriver::operator()` builds an `AudioBlock` from a pointer to a single `float scratch` with a frame count of 1:

```cpp
scratch = in;
float* chans[1] = {&scratch};
AudioBlock block(chans, 1, 1);
fx.process(block);
```

If `SvfEffect::process()` or any `AudioBlock` accessor internally rounds a 1-frame block up to the next SIMD lane width (e.g., 4 or 8 floats), it writes beyond the single `float`, producing undefined behaviour. The `AudioBlock` and `SvfEffect` implementations are in other chunks not visible in this diff, so the risk can't be confirmed here. If the DSP path ever adopts a SIMD loop over the frame range without guarding on actual frame count, this test harness will silently corrupt stack memory. A safer pattern is to allocate `scratch[8] = {}` (one full SIMD lane) and pass frame count 1, keeping the single-sample semantics while providing safe padding.