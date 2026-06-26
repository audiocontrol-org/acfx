I'll audit this test-files chunk for the svf-vertical-slice barrage and emit findings anchored to the diff.

### High-resonance test claims "denormal-free" but only asserts finiteness

Finding-ID: AUDIT-BARRAGE-claude-01
Status:     open
Severity:   medium
Surface:    tests/core/svf-test.cpp:84-101 (and the claim in tests/support/svf-reference.h:6-9)

The test case is titled `"high resonance stays NaN/denormal-free and bounded"` and the file header (svf-test.cpp:11-13) states it verifies "NaN/denormal stability at high resonance." But the only per-sample assertion is `REQUIRE(std::isfinite(out))` (line 92) plus a loose magnitude bound. Denormals (subnormal floats) are finite — `std::isfinite` returns true for every subnormal — so the test does not detect the very hazard its name advertises. The actual denormal check would need `std::fpclassify(out) != FP_SUBNORMAL` or a magnitude-floor assertion on the ringing tail after the impulse decays.

Blast radius: denormals are a genuine real-time hazard (FTZ/DAZ stalls in the audio callback, which is the whole point of the RT-safety mandate). An adopter or downstream agent reading "denormal-free verified" trusts a property that was never exercised; an SVF that rings down into subnormals in its decay tail ships with unguarded callback stalls and the green test gives false confidence. A reasonable fix asserts denormal classification on the post-impulse tail (where the ring decays toward zero and subnormals actually appear), not just `isfinite`.

### Allocation sentinel does not intercept aligned (C++17) operator new/delete

Finding-ID: AUDIT-BARRAGE-claude-02
Status:     open
Severity:   medium
Surface:    tests/support/allocation-sentinel.cpp:26-54

The sentinel replaces `operator new(size_t)`, `new[]`, and the matching plain/sized deletes. It does **not** replace the C++17 over-aligned overloads `operator new(std::size_t, std::align_val_t)` / `operator new[](..., align_val_t)` and their aligned deletes. Those are distinct replaceable allocation functions: any allocation of a type whose alignment exceeds `__STDCPP_DEFAULT_NEW_ALIGNMENT__` (commonly 16) — e.g. an `alignas(32)` SIMD buffer or a container of over-aligned elements — routes through the default aligned `operator new`, bumps no counter, and is invisible to the no-allocation invariant (FR-014).

Blast radius: this is a false-negative in the load-bearing RT-safety guard. The dangerous failure mode of a safety net is passing while the unsafe thing happens; an over-aligned heap allocation introduced into a `process()` path later would slip through silently and the sentinel test would stay green. The DSP core today may not over-align, but the sentinel is a general guard meant to catch future regressions, and it has a blind spot exactly where SIMD-aligned audio buffers live. Fix: add the four aligned overloads (`new`/`new[]` + matching deletes taking `std::align_val_t`) routing through `std::aligned_alloc`/`free` with the same counter bumps. (The nothrow variants are fine — they delegate to the throwing form already replaced.)

### Discrete-mapping test cannot distinguish bucket index from scaled value

Finding-ID: AUDIT-BARRAGE-claude-03
Status:     open
Severity:   low
Surface:    tests/core/parameter-test.cpp:24-26, 57-66

The `discreteMode` descriptor is constructed with `min=0.0f, max=2.0f, count=3` (lines 24-26), so its value range `[0,2]` is numerically identical to its bucket-index range `0..count-1 = 0..2`. Every assertion in `"discrete mapping quantizes to buckets and round-trips by index"` (e.g. `denormalize(discreteMode, 0.5f) == 1.0f`) is satisfied equally by an implementation that returns the raw bucket *index* and one that returns the *value scaled into [min,max]*. If discrete `denormalize` is contractually supposed to return a value within `[min,max]` (the same surface the continuous modes use), this test would not catch a regression that returns the index instead.

Blast radius: low — it is a coverage hole, not an active defect, and the other (continuous) cases pin the value-scaling contract. But the test's own comment claims it verifies "round-trips by index," and a future discrete param with `min/max ≠ index range` would expose whichever interpretation is wrong while this test stays green. Strengthening it to a descriptor where value range and index range diverge (e.g. `min=10, max=30, count=3`) would make the assertion discriminate.

### Sentinel comment bundles prepare() into the zero-allocation assertion, over-constraining the RT contract

Finding-ID: AUDIT-BARRAGE-claude-04
Status:     open
Severity:   low
Surface:    tests/support/allocation-sentinel.cpp:7-11

The header comment states the test "asserts zero across a prepare-then-N-process sequence (FR-014)." The real-time-safety invariant in the constitution is scoped to `process()` / the audio callback — `prepare()` is conventionally the *allowed* place to allocate (buffer sizing for a new sample rate / block size). Encoding "zero allocation across prepare + process" as the measured region forbids `prepare()` from ever allocating, which over-constrains the contract beyond what RT-safety requires.

Blast radius: low for the current fixed-state SVF (which legitimately allocates nothing anywhere), but the comment documents an invariant stricter than the design intends. A downstream agent extending the effect family — adding a delay line or FFT buffer that must be allocated in `prepare()` — would read this as "prepare must not allocate" and either contort the design or treat the consequent test failure as a regression rather than expected behavior. The intended invariant is "no allocation in `process()` after `prepare()` has run"; the measured region (in the cross-chunk no-allocation-test) and this comment should reset the counters *after* `prepare()` returns.

---

I checked the parameter-mapping math (linear/geometric-midpoint/discrete-floor arithmetic at lines 36-66 all compute correctly), the fresh-filter-state discipline in the SVF measurements (a new `MonoDriver` per measurement at svf-test.cpp:60-79 correctly avoids state contamination), the analytic tolerance bounds in svf-reference.h (passband/stopband gains are physically sound for a 2nd-order SVF), and the delete-override chain (sized→unsized delegation at lines 49-54 is correct). The four findings above are the substantive ones; the rest of the chunk is clean.