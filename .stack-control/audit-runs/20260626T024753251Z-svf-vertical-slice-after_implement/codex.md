### Allocation sentinel misses aligned heap allocations

Finding-ID: AUDIT-BARRAGE-codex-01  
Status:     open  
Severity:   medium  
Surface:    tests/support/allocation-sentinel.cpp:25-53

The test-only global allocation hooks override ordinary `operator new`, `operator new[]`, and ordinary delete forms, but they do not override the C++17 aligned allocation/deallocation overloads such as `operator new(std::size_t, std::align_val_t)`. Any over-aligned type allocated inside the measured audio path can bypass `g_allocations` entirely, so the no-allocation invariant can report zero while heap traffic still occurs.

The blast radius is medium because this is not production DSP behavior directly, but it weakens an RT-safety gate the feature appears to rely on. A reasonable fix is to add aligned `new`/`delete` and aligned array overloads to the sentinel, incrementing the same thread-local counters and using an aligned allocation primitive.

### Denormal-free test does not test denormals

Finding-ID: AUDIT-BARRAGE-codex-02  
Status:     open  
Severity:   medium  
Surface:    tests/core/svf-test.cpp:87-98

The test case is named `"high resonance stays NaN/denormal-free and bounded"` and the file header says it covers “NaN/denormal stability,” but the assertion inside the loop only checks `std::isfinite(out)` and a loose max amplitude bound. Subnormal floating-point values are finite, so this test would pass even if the SVF emitted denormals during the long impulse tail.

The blast radius is medium because denormal behavior is a real audio RT performance invariant, and this test gives downstream consumers false confidence that it is covered. A reasonable fix is to explicitly check `std::fpclassify(out) != FP_SUBNORMAL` or define the project’s denormal policy in the core and assert that policy here.
