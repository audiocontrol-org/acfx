### Allocation sentinel misses aligned heap traffic

Finding-ID: AUDIT-BARRAGE-codex-01
Status:     open
Severity:   medium
Surface:    tests/support/allocation-sentinel.cpp:27-54

`AllocationSentinel` claims to count heap traffic via global allocation overrides, but it only replaces plain `operator new`, `operator new[]`, and the plain/sized deletes. It does not replace the C++17 aligned allocation forms, for example `operator new(std::size_t, std::align_val_t)` / `operator new[](std::size_t, std::align_val_t)` and matching deletes. Any over-aligned type, SIMD buffer, or aligned audio primitive allocated in the measured region can bypass these counters while the FR-014 “no allocation” test still reports zero.

The blast radius is medium because this is test infrastructure, not runtime code, but downstream consumers can treat the no-allocation invariant as proven when one real allocation channel is unobserved. A reasonable fix is to add the aligned new/delete replacement set and increment the same thread-local counters, with a small fixture that allocates an `alignas(64)` object or array and proves the sentinel sees it.

### Denormal-free claim is not actually tested

Finding-ID: AUDIT-BARRAGE-codex-02
Status:     open
Severity:   medium
Surface:    tests/core/svf-test.cpp:90-101

The test is named and commented as verifying “NaN/denormal-free” behavior, but the assertion only checks `std::isfinite(out)` and `maxAbs < 100.0f`. Subnormal floats are finite, so a filter that decays into denormals for a long tail will pass this test even though the claimed RT-safety/performance invariant is violated.

The blast radius is medium because the unchecked behavior is exactly the kind of audio-thread performance issue the test advertises as covering. A reasonable fix is to explicitly classify nonzero outputs with `std::fpclassify(out) != FP_SUBNORMAL`, or define a flush-to-zero threshold/invariant and assert the filter state/output respects it during the impulse tail.
