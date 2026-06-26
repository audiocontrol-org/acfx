### Allocation sentinel misses aligned heap traffic

Finding-ID: AUDIT-BARRAGE-codex-01
Status:     open
Severity:   high
Surface:    tests/support/allocation-sentinel.cpp:24-52

The allocation sentinel only overrides the ordinary `operator new`, `operator new[]`, `operator delete`, and sized delete forms. It does not override the C++17 aligned allocation forms such as `operator new(std::size_t, std::align_val_t)` / `operator new[](std::size_t, std::align_val_t)` or their matching deletes. Because the test binary is compiled as C++20, any over-aligned DSP type, SIMD buffer, or aligned container allocation inside `process()` can bypass these counters entirely.

This matters because the sentinel is the enforcement mechanism for the real-time “no heap allocation in process” invariant. A downstream implementer could introduce an over-aligned allocation in the audio path and still get a green no-allocation test, so the blast radius is a shipped RT-safety false negative rather than mere test hygiene. A reasonable fix is to make the sentinel cover the full replaceable allocation surface used by the language mode, including aligned and nothrow variants, and route all counted forms through the same thread-local counters.
