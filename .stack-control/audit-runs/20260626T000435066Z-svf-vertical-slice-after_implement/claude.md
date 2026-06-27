I'm acting as the `claude` lane of the audit barrage on chunk `5d46bb000cdab808` (the test/test-support surface). I walked the five files carefully — the SVF response tests, the doctest entry point, the allocation sentinel, and the reference helper. Findings below.

### Test claims denormal stability but `std::isfinite` cannot detect denormals

Finding-ID: AUDIT-BARRAGE-claude-01
Status:     open
Severity:   medium
Surface:    tests/core/svf-test.cpp:84-101 (and the file header comment, lines 9-11)

The header comment promises "NaN/**denormal** stability at high resonance" and the case is named `"high resonance stays NaN/denormal-free and bounded"`, but the only per-sample assertion is `REQUIRE(std::isfinite(out))` (line 95). `std::isfinite` returns `true` for subnormal/denormal values — it is false only for `inf`/`NaN`. So the test verifies *nothing* about denormals: an SVF that drifts into the denormal range during the 200k-sample impulse decay (a textbook IIR hazard without flush-to-zero, and directly relevant to this project's RT-safety principle) passes this test silently.

Blast radius: a downstream developer or unattended agent reading the suite will believe denormal protection is verified and proven, when the production filter could emit denormals that cause RT-callback CPU spikes that never trip CI. The label is load-bearing precisely because it reads as a settled guarantee. A real fix asserts on the denormal predicate directly — e.g. `REQUIRE(out == 0.0f || std::fabs(out) >= std::numeric_limits<float>::min())` over the tail, or `std::fpclassify(out) != FP_SUBNORMAL` — or drops the denormal claim from the name/comment if only NaN/inf bounding is intended.

### Allocation sentinel does not intercept aligned (or nothrow) `operator new`/`delete`

Finding-ID: AUDIT-BARRAGE-claude-02
Status:     open
Severity:   medium
Surface:    tests/support/allocation-sentinel.cpp:27-54

The sentinel overrides only the four classic forms: throwing `operator new`/`new[]` and plain/sized `operator delete`/`delete[]` (lines 27-54). It does **not** override the C++17 over-aligned forms `operator new(std::size_t, std::align_val_t)` / matching deletes, nor the `std::nothrow_t` forms. Any allocation routed through those overloads — e.g. a buffer of an over-aligned (`alignas(16/32/64)`) SIMD type, or anything allocated via `new(std::nothrow)` — bumps no counter, so the no-allocation invariant test (FR-014) would report `allocations() == 0` while heap traffic actually occurred.

Blast radius: this is the *measurement instrument* for the project's central RT-safety guarantee. A blind spot in it means the guarantee can be silently false for exactly the high-performance (SIMD-aligned) data paths a DSP core is most likely to grow. An adopter extending the core with an over-aligned type and trusting a green no-allocation run gets a false negative. A fix adds the `std::align_val_t` new/delete pair (and ideally the `nothrow` pair) bumping the same counters, or — if only the default-alignment path is in scope — documents that aligned allocations are intentionally out of the sentinel's coverage so the limitation is visible rather than assumed-complete.

### All frequency-response tests exercise only single-frame blocks; the real multi-frame block path is never driven

Finding-ID: AUDIT-BARRAGE-claude-03
Status:     open
Severity:   medium
Surface:    tests/core/svf-test.cpp:37-53 (MonoDriver), driving 44-82

`MonoDriver::operator()` constructs a fresh `AudioBlock(chans, 1, 1)` — **one channel, one frame** — per sample (lines 47-52), and `prepare` is called with `ProcessContext{kRefSampleRate, 1, 1}`. Every correctness test (lowpass/highpass/bandpass response, and the resonance ring) therefore only ever invokes `process()` on a 1-frame block. The production audio callback (workbench, plugin, Daisy/Teensy adapters) calls `process()` with blocks of many frames. A defect that manifests only across a multi-frame block — an off-by-one over the block length, a per-block loop that processes only the first frame, stale-length reuse, or state mishandled at block boundaries — would pass 100% of these tests because each "block" is a single frame.

Blast radius: the suite is presented as proving SVF correctness (T016 vs the T013 references), but the contract it actually validates is "correct when called one frame at a time," which is not how any consumer calls it. An adopter trusting green tests could still hit audible breakage with real block sizes. A fix adds at least one response or sample-identity test that pushes a multi-frame block (e.g. 64 or 512 frames) through `process()` and compares against the per-sample path, ensuring the block loop itself is covered.

### High-resonance ring test passes for an all-zeros output (no liveness assertion)

Finding-ID: AUDIT-BARRAGE-claude-04
Status:     open
Severity:   low
Surface:    tests/core/svf-test.cpp:84-101

The ring test feeds an impulse then silence and asserts only `std::isfinite(out)` and `maxAbs < 100.0f`. Both predicates are satisfied by a filter that outputs identically zero — a degenerate/dead filter (or one whose resonance parameter wiring is broken so it never rings) passes this case unchanged. There is no lower bound confirming the impulse actually produced non-trivial output, so the test cannot distinguish "stable, bounded oscillation" from "silent."

Blast radius: limited, because the three response tests independently confirm the filter is not dead, so a regression here alone wouldn't ship a totally-broken filter. But as written the case provides weaker signal than its name implies. A minimal hardening adds a liveness floor (e.g. `CHECK(maxAbs > <small-positive>)`) so the test asserts the filter both rings *and* stays bounded, which is the stability contract it intends to cover.