I was dispatched as an independent audit reviewer for chunk `31c30149ec9faef5` (the test-suite surface of `svf-vertical-slice`). I walked all six files. My findings follow.

### Allocation sentinel ignores aligned-new and nothrow-new, leaving holes in the RT no-alloc safety net

Finding-ID: AUDIT-BARRAGE-claude-01
Status:     open
Severity:   medium
Surface:    tests/support/allocation-sentinel.cpp:28-54

The sentinel replaces only the four "ordinary" global operators: throwing `operator new(size)` / `new[](size)` and the unsized/sized `operator delete`. It does **not** override the C++17 over-aligned forms `operator new(std::size_t, std::align_val_t)` / its array form, nor the `nothrow` forms `operator new(std::size_t, const std::nothrow_t&)`. Any heap allocation that routes through those operators — e.g. a future over-aligned (`alignas(16/32)`, SIMD-friendly) type allocated on the heap inside a `process()` path — is invisible to `g_allocations`, so the no-allocation invariant test (FR-014, in the sibling chunk) would return a false green.

This matters because the sentinel **is** the mechanism that enforces the project's load-bearing RT-safety guarantee. A safety net with a silent hole is worse than no net: a downstream contributor who adds an over-aligned heap allocation in the audio callback gets a passing test and ships a heap allocation into the RT path. The header (`allocation-sentinel.h:7`) advertises this as a counter of "heap-allocation" generically, implying completeness it doesn't have. A reasonable fix adds the aligned and nothrow overloads (and their matching deletes) so every standard allocation channel funnels through the counters, or documents the covered surface explicitly so the gap is intentional and visible.

### `high resonance stays NaN/denormal-free` test never checks for denormals

Finding-ID: AUDIT-BARRAGE-claude-02
Status:     open
Severity:   medium
Surface:    tests/core/svf-test.cpp:90-102

The test case name and the file-header comment (`svf-test.cpp:13`) both claim "NaN/denormal stability," but the body only asserts `std::isfinite(out)` and `maxAbs < 100.0f`. `std::isfinite` is true for denormals — denormalized floats are finite. So the test verifies NaN/Inf-freedom and boundedness, but provides **zero** coverage of the denormal claim it advertises.

Denormal flushing is a real RT concern in this project (denormals in a recursive filter's feedback path cause order-of-magnitude CPU spikes on x86 without FTZ/DAZ), and this is exactly the scenario — an impulse rung out for 200k samples into near-silence — where denormals accumulate. A reader (or an unattended agent) trusts the green test as evidence that denormal handling is correct when nothing in the suite exercises it. A faithful fix either renames/rescopes the test to its actual contract (finiteness + boundedness) or adds a real denormal assertion — e.g. checking that the tail output is flushed to exactly `0.0f` rather than lingering at ~1e-30 once the ring decays below the denormal threshold.

### SVF reference bounds are loose enough that a 1st-order filter would pass the "2nd-order" response tests

Finding-ID: AUDIT-BARRAGE-claude-03
Status:     open
Severity:   medium
Surface:    tests/support/svf-reference.h:42-48, tests/core/svf-test.cpp:55-89

The header comment claims these references capture "the analytic truths a *correct 2nd-order* state-variable filter must satisfy." But the chosen bounds don't validate the order. `kStopbandGainMax = 0.25` (≈ −12 dB) is checked three octaves above cutoff (8 kHz vs 1 kHz). A true 2nd-order rolloff there is ≈ −36 dB (≈ 0.016); even a **1st-order** filter gives ≈ −18 dB (≈ 0.125) — comfortably under 0.25. Likewise the lowpass/highpass/bandpass cases only assert relative ordering (`passband > stopband`, `centre > low/high`), never that the −3 dB corner actually sits at `kRefCutoffHz`.

The consequence is a false-coverage trap: a regression that drops the filter to 1st-order, or shifts the cutoff by an octave, or halves the slope, would still pass every test here while the comment asserts the response is validated against known-good references. For a vertical-slice whose entire point is a correct SVF, that is the one property most worth pinning. A stronger version asserts a slope check (magnitude at cutoff vs at cutoff×2 should differ by ≈ 12 dB for 2nd-order) and an approximate −3 dB point at the cutoff, rather than the current generous one-sided thresholds.

### Discrete-parameter test descriptor makes bucket-index == value, leaving the discrete mapping under-determined

Finding-ID: AUDIT-BARRAGE-claude-04
Status:     open
Severity:   low
Surface:    tests/core/parameter-test.cpp:24-27, 52-62

`discreteMode` is constructed with `min=0, max=2, count=3`, which collapses two distinct semantics into the same observable values: "denormalize returns the bucket *index*" and "denormalize returns `min + index*(max-min)/(count-1)`" both yield `{0,1,2}` for this descriptor. The test therefore cannot tell which formula `parameter.h` actually implements, and it never exercises a discrete parameter with a non-zero `min` or a non-`(count-1)` span — the case where the two readings diverge and a real off-by-mapping bug would live. The quantization step itself is similarly under-pinned: `floor(norm*count)` and a round-to-nearest scheme agree on all four sampled points (0.0, 0.5, 0.99, 1.0), so the test passes either way.

Blast radius is bounded today (the only discrete param in this slice is `mode`, with `min=0`), so I'm rating this low. But it's a genuine false-coverage gap: a future discrete param with an offset range could ship a wrong mapping with the suite still green. A second descriptor with `min!=0` (e.g. `min=1, max=4, count=4`) plus an intermediate quantization point that distinguishes floor from round would close the gap and make the discrete contract actually testable.