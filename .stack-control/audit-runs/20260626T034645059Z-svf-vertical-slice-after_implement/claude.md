I walked the test/support chunk (`31c30149ec9faef5`) carefully — the parameter mapping tests, the SVF response tests, the allocation sentinel, and the magnitude-measurement helper. Three findings worth surfacing.

### Allocation sentinel does not override the C++17 aligned `operator new`/`delete`, so over-aligned heap allocations are invisible to the no-allocation invariant

Finding-ID: AUDIT-BARRAGE-claude-01
Status:     open
Severity:   medium
Surface:    tests/support/allocation-sentinel.cpp:27-54

The sentinel overrides only the four non-aligned forms (`operator new(size_t)`, `operator new[](size_t)`, and their plain/sized deletes). It does **not** override the aligned overloads introduced in C++17 — `operator new(std::size_t, std::align_val_t)` and the matching aligned deletes. Any heap allocation of an over-aligned type (`alignof(T) > __STDCPP_DEFAULT_NEW_ALIGNMENT__`, i.e. SIMD/cache-line-aligned buffers, which are common in DSP) routes through the aligned operator, lands in the default library allocator, and increments neither `g_allocations` nor `g_deallocations`.

This is a false-negative in the exact mechanism that exists to prove FR-014 (no heap traffic on the `process()` path). The no-allocation test (in a sibling chunk) resets these counters and asserts zero; if a future change in the SVF or a DaisySP path performs an over-aligned `new`, the counters stay at zero and the invariant test passes green while a real RT allocation occurs. Blast radius: an adopter reads a passing RT-safety test and trusts an invariant the harness cannot actually observe. A reasonable fix is to add the aligned `operator new`/`operator new[]`/`operator delete` overloads (guarded on `__cpp_aligned_new`) that bump the same counters via `std::aligned_alloc`/`free`, so every heap path is instrumented.

### "high resonance stays NaN/denormal-free" test asserts only `std::isfinite`, which is true for denormals — the denormal half of the claim is unverified

Finding-ID: AUDIT-BARRAGE-claude-02
Status:     open
Severity:   medium
Surface:    tests/core/svf-test.cpp:89-101 (and the file header comment, line 12-13)

The TEST_CASE is named `"high resonance stays NaN/denormal-free and bounded"` and the file header (lines 12-13) advertises "NaN/denormal stability at high resonance," but the only per-sample assertion is `REQUIRE(std::isfinite(out))`. `std::isfinite` returns `true` for subnormal (denormal) floats and for zero — it detects NaN and ±inf only. The "denormal-free" portion of the contract is therefore asserted nowhere.

Denormals matter here precisely because they are the RT-performance failure mode the project cares about: a filter that decays into the subnormal range without flushing-to-zero can cause 10–100× slowdowns on the target hardware while every value remains `isfinite`. A reader trusting the test name believes denormal flushing is covered; a regression that reintroduces denormal ringing would ship green. Blast radius is false assurance on an RT property, not a hard crash, so medium. A fix would either assert `std::fpclassify(out) != FP_SUBNORMAL` (or `out == 0.0f || std::fabs(out) >= FLT_MIN`) over the decay tail, or rename the case/comment to claim only what `isfinite` actually checks (NaN/inf-free).

### High-resonance stability is exercised for bandpass only; lowpass and highpass at high resonance are untested despite the file claiming per-mode coverage

Finding-ID: AUDIT-BARRAGE-claude-03
Status:     open
Severity:   medium
Surface:    tests/core/svf-test.cpp:89-101

The stability test instantiates `MonoDriver ring{SvfMode::bandpass, 0.99f}` and rings an impulse — but only for **bandpass**. Lowpass and highpass at high resonance are never driven near the stability limit. The three frequency-response cases above each cover their own mode, and the header comment frames the file as "per-mode frequency response … plus NaN/denormal stability at high resonance," which reads as mode-general coverage.

State-variable filter modes share the integrator state but tap it differently; a coefficient or clamp regression could blow up (or denormal-ring) in LP/HP while bandpass stays bounded, and this suite would not catch it. The qualitative response tests run at resonance 0, so nothing else exercises LP/HP at the Q where instability appears. Blast radius: a high-Q instability in two of three shipped modes passes the test gate. The fix is to loop the ring test over `{lowpass, highpass, bandpass}` (the `MonoDriver` already parameterizes mode and resonance, so this is a small change) and assert finiteness + bound for each.