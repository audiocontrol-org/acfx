I'm acting as the independent audit reviewer for chunk `31c30149ec9faef5` (the test surfaces). I walked all six files for correctness, coverage-vs-claim, and the channel-enumeration / round-0 drivers. Findings below.

### Allocation sentinel does not intercept the over-aligned (`std::align_val_t`) new/delete channel

Finding-ID: AUDIT-BARRAGE-claude-01
Status:     open
Severity:   medium
Surface:    tests/support/allocation-sentinel.cpp:28-53

The sentinel replaces `operator new`/`operator new[]` and the unsized/sized `operator delete` variants, but not the C++17 over-aligned overloads (`operator new(std::size_t, std::align_val_t)` and the matching aligned `operator delete`). The default library aligned-new is a *separate* implementation — it does **not** delegate to the throwing `operator new(std::size_t)` you replaced — so any allocation of an over-aligned type slips past the counters entirely. (The nothrow channel, by contrast, is fine: the default `operator new(size, nothrow)` wraps the throwing form, which you do intercept.)

Applying the channel-enumeration driver to this counting surface: the **value** channel (regular/array new) is covered, nothrow is covered indirectly, but the **alignment** channel is an opened-but-unfixtured path. The blast radius is the failure mode this infrastructure exists to prevent: a downstream consumer adds a SIMD/`alignas(N)` buffer or an aligned-allocator container inside `process()`, the no-allocation test (FR-014) stays green, and an RT-safety regression ships undetected — precisely the silent miss the sentinel is supposed to catch. A reasonable fix is to add the aligned `operator new`/`operator new[](std::size_t, std::align_val_t)` and aligned `operator delete` overloads bumping the same counters, plus a fixture that allocates an over-aligned type and asserts the count moves.

### SVF "frequency-response" tests assert only loose directional bounds — cutoff and Q are never verified

Finding-ID: AUDIT-BARRAGE-claude-02
Status:     open
Severity:   medium
Surface:    tests/core/svf-test.cpp:55-92, tests/support/svf-reference.h:38-48

The svf-test header claims these cases validate "per-mode frequency response vs the known-good references (T013)", but the asserted bounds are loose enough that the filter's actual tuning is never checked. The lowpass case (`svf-test.cpp:55-63`) only requires `passband(100Hz) >= 0.7` and `stopband(8000Hz) <= 0.25` — a filter whose cutoff was implemented at 500 Hz or 2000 Hz (instead of 1000 Hz) would pass identically, since 100 Hz still passes and 8000 Hz is still attenuated. The bandpass case (`svf-test.cpp:77-90`) is weaker still: it asserts only `centre > low` and `centre > high`, so a bandpass mis-centred at 1500 Hz, or one with a substantially wrong Q, also passes. Resonance is never asserted numerically at all (the high-res case only checks finiteness/boundedness).

The svf-reference.h comment is honest that it deliberately avoids exact magnitude numbers to dodge false precision — that intent is fine — but the resulting suite verifies *rolloff direction*, not *frequency response*, while the test file and tasks ledger (T016) describe it as the latter. The blast radius is false confidence: a downstream reader trusts that "T016 validates the SVF response" and skips manual DAW verification of cutoff accuracy, when in fact a noticeably mistuned filter would ship green. A reasonable fix is to add at least one bound that pins the cutoff — e.g. assert the lowpass −3 dB point is near 1000 Hz (gain at exactly `kRefCutoffHz` ≈ 0.707 within a tolerance), and assert the bandpass peak gain at centre exceeds a Q-derived floor — and to soften the file/ledger wording so it claims only what it tests.

### `normalize()` non-finite neutralization is untested — only `denormalize()` is covered

Finding-ID: AUDIT-BARRAGE-claude-03
Status:     open
Severity:   low
Surface:    tests/core/parameter-test.cpp:71-87

The "non-finite normalized inputs are neutralized" case exercises `denormalize()` with NaN/±inf and asserts finite, bounded results — good. But the inverse mapping `normalize()` is never tested with a non-finite *plain* input, even though it is a live path: svf-test calls `normalize(SvfEffect::kParams[...], plainHz)` to convert host/plain values into normalized form (`svf-test.cpp:36-44`), and in real use `normalize()` is the host→engine boundary where a NaN plain value could arrive. If `normalize()` lacks the same NaN guard that `denormalize()` got in the round-4 "NaN-safe clamp" work, a non-finite plain value would produce a non-finite normalized value that then feeds `setParameter` and the filter state.

The blast radius is bounded — the dominant runtime path is host→normalized→denormalize, and the denormalize guard is the last line of defense — which is why this is low rather than medium. But the round-4 fix introduced a NaN-clamp invariant and this is the symmetric, unfixtured half of it (round-0 self-red-team: the fix closed one direction; the other direction has no test). A reasonable fix is one CHECK loop asserting `std::isfinite(normalize(d, nan/±inf))` and that NaN maps to the min for each descriptor, mirroring the existing denormalize block.