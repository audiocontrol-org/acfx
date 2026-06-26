I'll audit chunk 31c30149ec9faef5 (the test files for svf-vertical-slice) as an independent reviewer.

After walking all six files carefully, here are my findings.

---

### Allocation sentinel ignores aligned-new and nothrow-new — RT no-alloc invariant has a silent false-negative gap

Finding-ID: AUDIT-BARRAGE-claude-01
Status:     open
Severity:   medium
Surface:    tests/support/allocation-sentinel.cpp:31-54

The sentinel overrides exactly six entry points: `operator new(size_t)`, `operator new[](size_t)`, the two plain `operator delete`, and the two sized `operator delete`. It does **not** override the C++17 over-aligned forms (`operator new(size_t, std::align_val_t)` / `operator new[](...)`) nor the `std::nothrow_t` forms. Any allocation routed through those paths increments no counter and is invisible to `AllocationSentinel::allocations()`.

This matters because the sentinel is the project's *primary automated guard* for the load-bearing commandment "no heap allocation in any `process()` path" (FR-014). DSP code is exactly the domain that reaches for over-aligned heap buffers — `alignas(16/32)` SIMD scratch is idiomatic. The current scalar `SvfEffect` likely never triggers aligned-new, so the test passes honestly today. But the test is meant to be a *durable* invariant: the first time someone adds a SIMD-aligned heap buffer to a `process()` path, `new` dispatches to the aligned overload, the counter stays zero, the test stays green, and an RT-safety violation ships with a passing guard behind it. Blast-radius: a downstream consumer trusts "0 allocations ✓" while the audio thread is in fact allocating. A reasonable fix is to add the `align_val_t` and `nothrow_t` overloads (forwarding to the same counter + `std::aligned_alloc`/`malloc`), or to document at the call site that the sentinel only covers default new and is therefore necessary-but-not-sufficient.

---

### "NaN/denormal-free" test verifies finiteness only — it never checks for denormals despite its name

Finding-ID: AUDIT-BARRAGE-claude-02
Status:     open
Severity:   medium
Surface:    tests/core/svf-test.cpp:89-102

The test case is titled `"high resonance stays NaN/denormal-free and bounded"` and its body asserts `REQUIRE(std::isfinite(out))` plus `maxAbs < 100.0f`. `std::isfinite` rejects NaN and ±Inf, but **denormals are finite** — a filter ringing down into denormal territory passes `isfinite` cleanly. The test therefore verifies NaN-freedom and boundedness, but provides *zero* evidence for the denormal-freedom its own name advertises.

This is contract/title drift, and it matters more than a cosmetic naming nit because denormals are an RT-correctness concern in DSP, not a cosmetic one: a filter that spends its decay tail in denormals causes order-of-magnitude CPU spikes on the audio thread (the exact failure FTZ/DAZ flushing exists to prevent). An operator or agent reading the test ledger sees "denormal-free ✓ tested at resonance 0.99" and trusts a property nothing actually exercised. The fix is either to drop "denormal-free" from the name (honest scope reduction) or to add a real assertion — e.g., after the ring decays, assert the tail samples are either exactly `0.0f` or `>= std::numeric_limits<float>::min()` (normalized), proving denormals were flushed rather than merely finite.

---

### Discrete-parameter mapping test conflates bucket index with parameter value — the min≠0 contract is untested

Finding-ID: AUDIT-BARRAGE-claude-03
Status:     open
Severity:   medium
Surface:    tests/core/parameter-test.cpp:30-33,57-69

The only discrete fixture is `discreteMode{... min=0.0f, max=2.0f, ... count=3}`, where the bucket **index** (0,1,2) numerically coincides with the parameter **value** (0,1,2 spanning min..max). Every assertion in `"discrete mapping quantizes to buckets and round-trips by index"` — `denormalize(...)==1.0f`, the `static_cast<int>(denormalize(...)) == idx` round-trip — therefore cannot distinguish "denormalize returns the bucket index" from "denormalize returns the mapped value." The test claims to pin the discrete-mapping contract but actually leaves the index↔value relationship to min/max completely unconstrained.

Blast-radius: the parameter system is general, not SVF-specific. The first discrete parameter with a nonzero min (an enum with an offset, a discrete count starting at 1, a quantized range like 100–102) will exercise the code path this test claims to cover, and whichever convention the implementation actually chose (return index vs. return min + index·step) will go unverified — a wrong choice ships green. A fix is to add a second discrete fixture with `min != 0` (e.g., min=10, max=12, count=3) and assert the *value* semantics explicitly, so the contract — not just the degenerate coincidence — is locked.

---

### Bandpass response test asserts only relative ordering, with no absolute attenuation bound

Finding-ID: AUDIT-BARRAGE-claude-04
Status:     open
Severity:   low
Surface:    tests/core/svf-test.cpp:76-86

The lowpass and highpass cases assert absolute bounds (`passband >= kPassbandGainMin` 0.7, `stopband <= kStopbandGainMax` 0.25) *and* ordering. The bandpass case (`"bandpass emphasizes the centre relative to both edges"`) asserts only `centre > low` and `centre > high` — no absolute pass/stop bound at all. A degenerate filter that is essentially allpass with an infinitesimal centre bump (or, worse, a near-flat response with tiny numerical asymmetry) satisfies both inequalities and passes.

Blast-radius is limited because the LP/HP cases would likely catch a grossly broken shared filter core, so this is hygiene rather than a correctness hole. But the bandpass *mode selection / center-frequency* path is partly distinct, and this test is the only thing guarding it; a bandpass that centers on the wrong frequency or barely emphasizes anything would slip through. A fix is to add a `kBandpassCentreGainMin` and assert `centre >= kBandpassCentreGainMin`, plus an edge-attenuation bound, mirroring the rigor the LP/HP cases already apply via `tests/support/svf-reference.h`.

---

These four are the substantive signals. Everything else I checked came back clean: `test-main.cpp` is a correct single-TU doctest entry point; `measureMagnitude` maintains phase continuity across the settle→measure boundary and uses fresh `MonoDriver` instances per frequency (correctly avoiding the state-bleed trap); the thread_local counters are constant-initialized so they don't recurse through operator-new during TLS setup; the linear and logarithmic mapping tests (including the geometric-midpoint and NaN/±Inf clamping cases) pin their contracts with appropriate tolerances.