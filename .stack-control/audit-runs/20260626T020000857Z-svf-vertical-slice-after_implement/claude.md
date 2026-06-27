I walked Chunk 31c30149ec9faef5 (the test + test-support surfaces) carefully, focused on whether each test actually validates the contract it claims and whether the allocation sentinel can give false confidence about the RT-safety invariant. Findings below.

### Allocation sentinel ignores aligned-new and nothrow allocation entry points

Finding-ID: AUDIT-BARRAGE-claude-01
Status:     open
Severity:   medium
Surface:    tests/support/allocation-sentinel.cpp:27-54

The sentinel overrides only the four "ordinary" allocation functions: `operator new(size_t)`, `operator new[](size_t)`, and the matching scalar/array deletes (plus the two sized-delete forwarders). It does **not** override the C++17 over-aligned forms `operator new(std::size_t, std::align_val_t)` / `operator delete(void*, std::align_val_t)`, nor the `std::nothrow_t` forms. Any allocation that goes through an aligned-new path (over-aligned types, SIMD-aligned buffers, some standard-library internals) or a `nothrow` path bypasses `g_allocations` entirely and is freed by the default aligned/nothrow delete — uncounted in both directions.

Blast radius: this sentinel is the sole mechanical guard behind FR-014 ("zero heap allocation on the audio thread"), a core project commandment. If any code reachable from `prepare()`+`process()` ever allocates via an aligned or nothrow path, `allocations()` returns 0, the no-allocation test passes, and the operator ships believing the audio path is allocation-free when it is not. The probability that the current pure-float SVF triggers this is low, but the guard silently degrades the moment an over-aligned member or aligned scratch buffer is introduced, and nothing flags it. A reasonable fix is to add overrides for the `align_val_t` and `nothrow_t` allocation/deallocation overloads, each bumping the same counters.

### No positive control — a static-lib global-new override that fails to link yields a vacuous always-zero pass

Finding-ID: AUDIT-BARRAGE-claude-02
Status:     open
Severity:   medium
Surface:    tests/support/allocation-sentinel.cpp:27-54, tests/support/allocation-sentinel.h:11-18

Global `operator new`/`operator delete` definitions placed in a test-support translation unit are a well-known link-fragility hazard: if the object file is built into a static library and the final test link does not pull that specific `.o`, the overrides silently do not take effect, `g_allocations` stays 0, and `allocations()` returns 0 unconditionally. In that failure mode the no-allocation assertion (FR-014) passes for the wrong reason — it certifies nothing. The sentinel exposes no self-verification: there is no positive control proving the counter actually moves when a real allocation happens.

The consuming test (`tests/core/no-allocation-test.cpp`) lives in another chunk (1d366441…), so I cannot confirm whether it includes such a control, and `tests/CMakeLists.txt` is likewise out of this chunk — so I cannot verify the override is linked directly into the executable rather than via an archive. The blast radius is the same core RT-safety invariant as finding -01: a green check that means "allocated nothing" is indistinguishable from "the counter was never wired up." A cheap fix is a positive control in the no-allocation test — deliberately `new`/`delete` one object, `CHECK(allocations() >= 1)`, then `reset()` before the measured region — and/or linking `allocation-sentinel.cpp` directly into the test target (not via a static lib) so the global overrides cannot be dropped.

### Discrete-parameter tests cannot distinguish "denormalize returns the bucket index" from "denormalize returns a scaled plain value"

Finding-ID: AUDIT-BARRAGE-claude-03
Status:     open
Severity:   low
Surface:    tests/core/parameter-test.cpp:14-17, 58-69

`discreteMode` is declared with min=0.0, max=2.0, count=3 — so its bucket **indices** (0,1,2) are numerically identical to its **plain values**. Every discrete assertion (`denormalize(...,0.5)==1.0`, the index round-trip at lines 65-68) therefore passes equally whether `denormalize` is contractually defined to return the quantized index or the quantized plain value in `[min,max]`. The test claims to pin "discrete mapping quantizes to buckets and round-trips by index," but with this coincident range it pins neither interpretation uniquely.

Blast radius is low: the only discrete parameter the slice actually ships is SVF mode, whose range also coincides with its indices (0..2), so the ambiguity does not currently produce a wrong result. But it is a latent contract gap — the day a discrete parameter with a non-trivial range is added (e.g. min=10, max=20, count=3), this test offers no guidance on what `denormalize` should return, and the parameter mapping could be built either way without a failing test. A stronger fixture would add one discrete descriptor whose plain-value range does not coincide with `0..count-1`, and assert the intended return semantics explicitly.

### Non-finite-input test leaves the NaN→bucket mapping for discrete parameters unspecified

Finding-ID: AUDIT-BARRAGE-claude-04
Status:     open
Severity:   low
Surface:    tests/core/parameter-test.cpp:71-87

The non-finite test pins precise values only for `logCutoff` (NaN→20, +inf→20000, −inf→20). For the discrete `discreteMode` (and `linearRes`) it asserts only `std::isfinite(...)` of the result, not which bucket NaN/±inf land in. The header comment claims "NaN must map to the minimum (0 normalized), not pass through," but for the discrete descriptor that stated contract is never actually checked — any finite bucket index satisfies the assertion, including a non-minimum one.

Blast radius is low because finiteness is the safety-critical property (a NaN reaching filter state is the real hazard, and that is covered), and the SVF mode parameter's behavior on a NaN is benign either way. Still, the test under-asserts the contract it documents: an implementation that clamped NaN→max-bucket for discrete params would pass. If the "NaN→min" rule is the intended contract for all skews, add `CHECK(denormalize(discreteMode, nan) == doctest::Approx(0.0f))` (and the corresponding linear case) so the rule is enforced uniformly rather than only for the logarithmic descriptor.