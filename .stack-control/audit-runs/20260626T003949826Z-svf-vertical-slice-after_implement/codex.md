### Allocation sentinel misses aligned and nothrow allocation paths

Finding-ID: AUDIT-BARRAGE-codex-01
Status:     open
Severity:   medium
Surface:    tests/support/allocation-sentinel.cpp:24-52

`AllocationSentinel` claims to enforce the no-heap RT invariant by overriding global `operator new/delete`, but it only covers the ordinary scalar/array and sized delete overloads. C++17 over-aligned allocations use `operator new(std::size_t, std::align_val_t)` / matching deletes, and nothrow allocation paths use `operator new(std::size_t, const std::nothrow_t&)`; neither is counted here.

The blast radius is medium because this can produce false-green no-allocation tests if DSP code or a dependency starts allocating over-aligned objects during the measured processing region. A reasonable fix is to add the aligned and nothrow overload families to the same counter path, preserving correct alignment semantics and deallocation matching.
