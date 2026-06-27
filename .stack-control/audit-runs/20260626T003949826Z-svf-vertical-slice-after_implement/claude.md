I'll audit this chunk (the test files for `svf-vertical-slice`) and emit findings in the required format.

### SVF response tests assert loose bounds that do not pin the cutoff — a wrong-cutoff filter passes green

Finding-ID: AUDIT-BARRAGE-claude-01
Status:     open
Severity:   medium
Surface:    tests/core/svf-test.cpp:62-99, tests/support/svf-reference.h:40-48

The header comment for `svf-test.cpp` claims these cases validate "per-mode frequency response vs the known-good references (T013)", and `MonoDriver` deliberately round-trips the cutoff through `normalize(...kCutoff..., kRefCutoffHz)` (lines 28-31) — implying the test verifies the cutoff is set correctly. It does not. The only assertions are loose magnitude bounds (`kPassbandGainMin = 0.7`, `kStopbandGainMax = 0.25`) measured a decade below (100 Hz) and three octaves above (8000 Hz) a 1000 Hz cutoff. Work the numbers: a filter whose cutoff is actually 2000 Hz (an octave high, e.g. a broken normalize/denormalize round-trip or a frequency-warping bug) still passes every assertion — 100 Hz is still ~unity (≥ 0.7 ✓) and 8000 Hz is still two octaves above 2000 Hz at roughly −24 dB ≈ 0.063 (≤ 0.25 ✓). The lowpass/highpass cases reduce to "rolls off in the right direction," and the bandpass case only checks relative ordering (`centre > low`, `centre > high`), never an absolute or cutoff-anchored bound.

Blast radius: an adopter or unattended agent reading the test names and the "known-good references" comment will believe per-mode cutoff accuracy is covered and will not add tighter coverage. A cutoff regression of up to roughly an octave ships green. A reasonable fix is to add at least one assertion that pins the response *at* `kRefCutoffHz` — e.g. the lowpass/highpass −3 dB point near unity-over-root-2 within a tolerance, or a bandpass peak that is materially higher than the existing edge measurements rather than merely greater-than. The author's "no false-precision magic numbers" intent (svf-reference.h:6-11) is reasonable, but the −3 dB corner is an analytic truth, not a fabricated number, and is exactly the truth these bounds currently fail to test.

### Allocation sentinel only intercepts global `operator new`/`new[]` — aligned-new, nothrow-new, and direct `malloc` bypass it, allowing a false PASS on the RT-safety gate

Finding-ID: AUDIT-BARRAGE-claude-02
Status:     open
Severity:   medium
Surface:    tests/support/allocation-sentinel.cpp:27-54

The sentinel backs FR-014's "zero heap allocations across prepare-then-N-process" invariant — the safety net that proves the DSP core is RT-safe for embedded targets. But it instruments only four entry points: throwing `operator new(size)`, `operator new[](size)`, and their matching deletes. Three allocation channels slip through uncounted: (1) C++17 aligned allocation `operator new(std::size_t, std::align_val_t)` / `operator delete(void*, std::align_val_t)`, which is what an over-aligned (`alignas`/SIMD) type triggers — plausible in DSP code; (2) `operator new(size, std::nothrow_t)`; and (3) any C-level allocation (`malloc`, `calloc`, `posix_memalign`, `std::aligned_alloc`) that never routes through global `operator new`. Allocations via any of these increment nothing, so `AllocationSentinel::allocations()` reads zero and the test passes even though the audio path allocated.

Blast radius: this is a safety gate whose entire value is catching accidental allocation before it reaches a Daisy/Teensy target where a heap call in `process()` causes a dropout or crash. A net with holes is worse than a visible gap because it manufactures false confidence — the audit log records "no-allocation verified" while an aligned-container or `aligned_alloc` path is invisible. Common accidental paths (`std::vector`, `std::string`) *are* caught because they use throwing `operator new`, which is why I rate this medium rather than high — but the blind spots are real and silent. A fix adds the `align_val_t` and `nothrow_t` operator overloads bumping the same counters, and (for full coverage) the header comment at allocation-sentinel.h:7-9 should state the malloc-level limitation explicitly so adopters know the guarantee's boundary rather than assuming it covers all heap traffic.

### `measureMagnitude` window is not an integer number of cycles — RMS estimate carries a windowing error

Finding-ID: AUDIT-BARRAGE-claude-03
Status:     open
Severity:   low
Surface:    tests/support/svf-reference.h:18-35

`measureMagnitude` computes the steady-state gain as `sqrt(sum(out^2)/sum(in^2))` over a fixed `measure = 16384`-sample window. For an LTI filter driven by a steady sinusoid this equals `|H(f)|` only when the window spans an integer number of cycles; otherwise the partial trailing cycle biases both sums. At the reference frequencies the window is non-integer — e.g. 1000 Hz at 48 kHz is `16384 * 1000/48000 ≈ 341.33` cycles. The resulting error is small (sub-percent) and is comfortably absorbed by the generous tolerances, so no current assertion is endangered.

Blast radius: low and self-limiting today, but it becomes a latent trap if a future change tightens the tolerances (e.g. to address Finding-01) without first making the window cycle-aligned — the tightened test would then fail on measurement noise rather than on a real filter defect, and the failure would be hard to attribute. A one-line fix rounds `measure` (or the analyzed span) to the nearest whole cycle of `freqHz`, i.e. `measure = round(cycles) * samplesPerCycle`, which removes the bias and makes the helper safe to tighten against later.

### Unexplained magic bound `100.0f` in the high-resonance stability check

Finding-ID: AUDIT-BARRAGE-claude-04
Status:     open
Severity:   low
Surface:    tests/core/svf-test.cpp:91-101

The high-resonance case asserts `maxAbs < 100.0f` after a unit impulse at `resonanceNorm = 0.99`, with the comment "Self-oscillation must not blow up." The bound `100.0f` is arbitrary and undocumented — it is neither derived from the resonance-to-Q mapping nor from the impulse-response peak of a just-stable SVF. The test's real intent (caught by the `REQUIRE(std::isfinite(out))` on every sample) is "no NaN/Inf"; the `< 100` ceiling is a weak secondary guard that would not catch a filter ringing at, say, peak 50× (audibly broken at Q this high) yet would also not obviously be wrong.

Blast radius: low — the `isfinite` check is the load-bearing assertion and it is correct. But the magic constant invites a future maintainer to "tune" it without understanding what bound is actually meaningful, and it reads as false precision. A small fix either drops the constant in favor of the finiteness check alone, or replaces it with a value tied to the resonance mapping (e.g. a documented multiple of the expected steady-state self-oscillation amplitude) so the threshold means something.

---

**Checks that came back clean:** `test-main.cpp` is a correct single-TU doctest entry point. The per-measurement fresh-`MonoDriver` construction in each case (no state bleed between passband/stopband measurements) is correct, and `measureMagnitude`'s continuous-phase handling across the settle→measure boundary (svf-reference.h:23-30) is right — no discontinuity artifact. The forwarding-reference `Proc&&` mutating the caller's driver in place is safe given each driver is measured exactly once. `inSq` is never zero (sum of `sin²` over thousands of samples), so no division-by-zero. The sized/unsized delete delegation in allocation-sentinel.cpp:48-53 is consistent.