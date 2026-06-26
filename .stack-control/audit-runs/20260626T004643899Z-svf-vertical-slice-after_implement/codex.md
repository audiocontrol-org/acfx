### The no-allocation gate misses valid C++ allocation channels

Finding-ID: AUDIT-BARRAGE-codex-01  
Status:     open  
Severity:   high  
Surface:    tests/core/no-allocation-test.cpp:29-36; tests/core/no-allocation-test.cpp:51-56; missing overload coverage in `tests/support/allocation-sentinel.cpp`

The FR-014 gate resets `AllocationSentinel` and treats a zero count as proof that `process()` and the host boundary allocate nothing. That proof is incomplete: the sentinel implementation only replaces ordinary `operator new(std::size_t)` / `operator new[](std::size_t)`, while C++17/C++20 code can allocate through replaceable aligned and nothrow forms such as `operator new(std::size_t, std::align_val_t)` or `operator new(std::size_t, const std::nothrow_t&)`. Those channels are still heap allocations, but they would not increment the counter this test reads at lines 36 and 56.

The blast radius is high because this is the automated real-time safety gate for FR-014/SC-003. A downstream implementer could add an over-aligned SIMD buffer or nothrow allocation in `process()` and still get a green “zero allocations” result from this test as written. A reasonable fix is to make the sentinel cover every replaceable allocation/deallocation overload used by the selected language standard, especially aligned and nothrow variants, and add a small self-test that proves each allocation channel increments the counter before relying on it for `process()` verification.
