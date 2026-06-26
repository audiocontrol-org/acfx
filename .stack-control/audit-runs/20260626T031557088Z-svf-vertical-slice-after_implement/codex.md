### Non-finite parameter tests only enforce neutralization semantics for one descriptor

Finding-ID: AUDIT-BARRAGE-codex-01
Status:     open
Severity:   medium
Surface:    tests/core/parameter-test.cpp:74-87

The test name and comment state the contract broadly: non-finite normalized inputs are “neutralized, never propagated,” with `NaN -> min`, `+inf -> max`, and `-inf -> min`. But lines 80-84 only require finite outputs for `linearRes` and `discreteMode`; the exact min/max behavior is asserted only for `logCutoff` on lines 85-87. An implementation could map `NaN` resonance to `0.5f`, or map discrete `+inf` to the wrong bucket, and this acceptance test would still pass.

Blast radius is medium because this is a test-gate hole, not the runtime bug itself. A downstream unattended agent could reasonably treat this file as proving the full parameter contract, while two of the three parameter shapes remain under-specified for non-finite inputs. A reasonable fix is to assert the same `NaN`, `+inf`, and `-inf` expected values for each descriptor, including discrete bucket behavior.

### Denormal-free SVF test does not test for denormals

Finding-ID: AUDIT-BARRAGE-codex-02
Status:     open
Severity:   medium
Surface:    tests/core/svf-test.cpp:90-101

The test case is named “high resonance stays NaN/denormal-free and bounded,” but the only per-sample validity assertion is `std::isfinite(out)` on line 97. Subnormal floating-point values are finite, so this test will pass while the filter emits denormals throughout the impulse tail. That misses the real-time performance hazard the test name claims to cover.

Blast radius is medium because the acceptance surface can certify an RT-safety property that it does not actually check. A reasonable fix is to classify nonzero outputs with `std::fpclassify(out)` and reject `FP_SUBNORMAL`, or explicitly flush/threshold and assert the intended behavior.

### Allocation sentinel misses aligned and nothrow allocation channels

Finding-ID: AUDIT-BARRAGE-codex-03
Status:     open
Severity:   medium
Surface:    tests/support/allocation-sentinel.cpp:27-54

The sentinel overrides only ordinary throwing `operator new`, `operator new[]`, and sized deletes. In C++17 and later, over-aligned allocations dispatch through `operator new(std::size_t, std::align_val_t)` / array variants, and nothrow allocations dispatch through `operator new(std::size_t, const std::nothrow_t&)`. Those channels are not counted here, so a measured region can allocate heap memory through standard language allocation paths while `AllocationSentinel::allocations()` still reports zero.

Blast radius is medium because this weakens the FR-014 no-allocation invariant without causing a compile failure. A downstream consumer relying on the test binary as the RT-allocation gate could ship code that allocates through an uninstrumented channel. A reasonable fix is to override the aligned and nothrow new/delete overload families used by the project’s configured C++ standard, and route them through the same counters.
