### C++17 aligned-allocation overrides missing from AllocationSentinel

Finding-ID: AUDIT-BARRAGE-claude-01
Status:     open
Severity:   medium
Surface:    tests/support/allocation-sentinel.cpp:28–54

`allocation-sentinel.cpp` overrides the six classic `operator new`/`delete` forms but omits all C++17 aligned-allocation variants: `operator new(std::size_t, std::align_val_t)`, `operator new[](std::size_t, std::align_val_t)`, and their three corresponding `delete` overloads. In C++17, when the compiler allocates a type whose alignment exceeds `__STDCPP_DEFAULT_NEW_ALIGNMENT__` (typically 16 bytes on x86-64), it silently routes through the aligned variants — it does **not** fall back to the basic form. DSP code commonly uses over-aligned objects (`alignas(32)` for AVX, `alignas(16)` for SSE/NEON; JUCE's `AudioBlock`, many ring-buffer designs). Any such allocation escapes the sentinel counters entirely, causing the RT-safety test (FR-014, `no-allocation-test.cpp`) to report zero allocations even when the audio path heaps. The nothrow variants (`operator new(size_t, const std::nothrow_t&)`) are also absent, though those are less likely to appear in DSP paths.

The fix is to add the three aligned-new/delete pairs (and optionally the two nothrow forms). On compilers that don't support `std::align_val_t` before C++17, guard them with `#if __cpp_aligned_new >= 201606L`.

---

### AllocationSentinel has no RAII guard — requires fragile manual `reset()` placement

Finding-ID: AUDIT-BARRAGE-claude-02
Status:     open
Severity:   low
Surface:    tests/support/allocation-sentinel.h:12–19 / tests/support/allocation-sentinel.cpp:17–22

`AllocationSentinel::reset()` must be called on the correct thread immediately before the measured region; if a test forgets this, or calls `reset()` one statement too early (before a doctest macro that itself allocates), the window is wrong and the test either false-passes or false-fails. The struct provides no RAII scope guard that would enforce this discipline. A minimal `struct AllocationGuard { AllocationGuard() { AllocationSentinel::reset(); } }` used as a local in the measured scope would remove the manual-placement burden entirely. Without seeing `no-allocation-test.cpp` (another chunk) it isn't possible to confirm whether the current call site is placed correctly, which is itself a sign the interface makes verification harder than it needs to be. This is a design rather than a correctness issue because a correct call site today can regress silently when a test is restructured.

---

### `MonoDriver` couples `SvfEffect::kCutoff` as both array index and `ParamId` with no assertion

Finding-ID: AUDIT-BARRAGE-claude-03
Status:     open
Severity:   low
Surface:    tests/core/svf-test.cpp:37–44

The constructor of `MonoDriver` (lines 37–44) calls:

```cpp
fx.setParameter(ParamId{SvfEffect::kCutoff},
                normalize(SvfEffect::kParams[SvfEffect::kCutoff], ...));
```

This silently assumes that (a) `SvfEffect::kCutoff` is a valid 0-based index into `kParams[]`, and (b) the numeric value of `SvfEffect::kCutoff` equals the `ParamId` the effect recognises for cutoff. The same pattern repeats for `kResonance` (line 40) and `kMode` (line 44). There is no `static_assert` or compile-time check that `kParams[kCutoff].id == ParamId{kCutoff}`. If `kCutoff` is ever renumbered (e.g., to accommodate a new parameter inserted before it), the test would silently set resonance at the cutoff frequency and cutoff at the resonance slot, producing measurement results that could still pass (the filter would be misconfigured but might still separate passband/stopband in relative terms). A `static_assert(SvfEffect::kParams[SvfEffect::kCutoff].id == ParamId{SvfEffect::kCutoff}, "index/id mismatch")` guarding each constant would catch this at compile time.

---

### High-resonance stability bound is too loose to gate gradual divergence

Finding-ID: AUDIT-BARRAGE-claude-04
Status:     open
Severity:   low
Surface:    tests/core/svf-test.cpp:97–100

The bound `CHECK(maxAbs < 100.0f)` (line 100) on 200 000 samples of impulse response at `resonanceNorm=0.99` permits roughly 40 dB of amplitude above the unit impulse. An implementation that linearly accumulates numerical error — growing from 1.0 to 60.0 over 200 k samples — would pass this check while exhibiting clearly divergent behaviour that would cause audible distortion or clipping in real use. The `REQUIRE(std::isfinite(out))` guard (line 97) correctly catches NaN/inf early, but the amplitude bound is a separate gate for the *degree* of bounded-input-bounded-output stability. Without knowledge of how `resonanceNorm=0.99` maps to physical Q, choosing 100.0 appears speculative. A comment explaining the calculation (e.g., "Q ≈ N at this resonance norm, so peak bandpass gain ≈ N, leaving headroom of 2×") would both justify the threshold and make regressions detectable if the resonance mapping changes. As-is, the bound is more a guard against catastrophic explosion than a meaningful stability contract.