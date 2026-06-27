### Allocation sentinel misses aligned allocation paths

Finding-ID: AUDIT-BARRAGE-codex-01
Status:     open
Severity:   medium
Surface:    tests/support/allocation-sentinel.cpp:25-54

The allocation sentinel replaces only the ordinary scalar/array `operator new` and `operator delete` overloads. It does not replace the C++17 aligned allocation overloads, such as `operator new(std::size_t, std::align_val_t)` and matching deletes. Any real-time code that allocates an over-aligned object inside the measured region can bypass these counters, so the FR-014 “no allocation” test can report zero allocations while heap traffic occurred.

This matters because the sentinel is test infrastructure for a safety invariant, not just incidental diagnostics. The blast radius is medium: it does not directly break runtime behavior, but it can let a downstream consumer trust a false no-allocation proof and ship RT-unsafe code. A reasonable fix is to add the aligned scalar/array new/delete replacements, count them through the same thread-local counters, and allocate/free with the aligned C runtime APIs available for the configured platforms.
