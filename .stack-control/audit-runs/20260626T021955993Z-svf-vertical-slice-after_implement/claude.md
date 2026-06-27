I'm being invoked as an independent audit reviewer in a multi-model audit barrage. I walked the test-files chunk (`31c30149ec9faef5`) carefully. Findings below.

### Allocation sentinel does not intercept aligned (`std::align_val_t`) or replacement-bypassing allocation paths

Finding-ID: AUDIT-BARRAGE-claude-01
Status:     open
Severity:   medium
Surface:    tests/support/allocation-sentinel.cpp:27-54

The sentinel replaces only the four non-aligned global operators: `operator new(size_t)`, `operator new[](size_t)`, and their delete counterparts (plus sized-delete shims). It does **not** replace the C++17 over-aligned forms `operator new(std::size_t, std::align_val_t)` / `operator new[](std::size_t, std::align_val_t)`. Those are *distinct* replaceable functions, and the default library implementation does **not** delegate to the throwing `operator new(size_t)` you replaced here — it calls `std::aligned_alloc`/posix path directly. The library nothrow forms *do* delegate to the throwing `operator new`, so they are covered; aligned allocations are the live hole.

This matters because the sentinel is the sole enforcement mechanism for the core RT-safety commandment ("no heap allocation in any `process()` path", FR-014). For the current SVF (a few float state members) no aligned heap allocation occurs, so the test is correct *today*. But this is reusable framework infrastructure: the moment any effect or scratch buffer uses an over-aligned type on the heap (SIMD-aligned blocks are idiomatic in DSP), the allocation escapes the counter, `allocations()` stays `0`, the no-alloc test stays green, and an RT deadline violation ships silently to the Daisy/Teensy targets. The fix is to add `operator new(std::size_t, std::align_val_t)` / `[]` overrides (and aligned deletes) that bump the same counters and call `std::aligned_alloc`/`operator delete` with alignment, so the sentinel cannot be bypassed by alignment.

### "NaN/denormal-free" SVF test verifies finiteness only — denormals are never tested

Finding-ID: AUDIT-BARRAGE-claude-02
Status:     open
Severity:   medium
Surface:    tests/core/svf-test.cpp:92-101, tests/support/svf-reference.h:8-12

The test case is titled `"high resonance stays NaN/denormal-free and bounded"` and the reference header (svf-reference.h:11) advertises "plus NaN/denormal stability at high resonance." But the body only asserts `REQUIRE(std::isfinite(out))` and `maxAbs < 100.0f`. A denormal is a *finite* float, so `std::isfinite` returns `true` for every denormal — the assertion can never detect the condition the title claims it guards. The word "denormal" in the name and header is unbacked by any check.

Blast radius: a downstream reader (or an unattended agent extending the suite) sees this test pass and concludes denormal stability is covered and need not be revisited. On the embedded ARM targets this slice exists to prove portability for, denormals are not a correctness problem but a performance cliff — without flush-to-zero / denormals-are-zero, a ringing high-Q filter that decays into the denormal range can cost orders-of-magnitude more cycles per sample and blow the audio-callback deadline. That failure mode is exactly what an embedded DSP suite must catch, and this test silently doesn't. A real check would, after the impulse decays, assert the tail samples are either exactly `0.0f` or `>= std::numeric_limits<float>::min()` (i.e., not subnormal), or assert the DSP path forces FTZ. At minimum the name/comment should be corrected to "NaN-free and bounded" so it stops overclaiming.

### Positional aggregate init of 9-field `ParameterDescriptor` with three adjacent floats is a silent-swap factory

Finding-ID: AUDIT-BARRAGE-claude-03
Status:     open
Severity:   low
Surface:    tests/core/parameter-test.cpp:15-30 (also svf-test.cpp MonoDriver construction relies on the same layout)

The descriptors are built by positional aggregate initialization across nine fields, three of which are consecutive same-typed floats — `0.0f, 1.0f, 0.0f` for (min, max, default) and `20.0f, 20000.0f, 1000.0f` for `logCutoff`. Nothing pins each literal to its field. If `parameter.h` (defined in another chunk, not visible here) ever reorders or inserts a field — e.g., swaps `default` ahead of `min`, or adds a field between `unit` and `min` — these initializers silently misassign with no compiler diagnostic, and the round-trip tests could keep passing against the *wrong* semantics (a swapped min/max still round-trips a normalized value). The whole point of these tests is to be the contract anchor for the parameter mapping; a brittle, silently-mis-bindable construction undermines that.

This is hygiene, not a current bug (the layout matches today), hence low. The fix is C++20 designated initializers (`{.id = ParamId{1}, .name = "resonance", .min = 0.0f, .max = 1.0f, .defaultValue = 0.0f, …}`), which make a field reorder either a compile error or self-evidently correct, and double as documentation of which literal is which.