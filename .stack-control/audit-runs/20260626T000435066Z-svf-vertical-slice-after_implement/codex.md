### Allocation sentinel misses aligned heap allocations

Finding-ID: AUDIT-BARRAGE-codex-01
Status:     open
Severity:   medium
Surface:    tests/support/allocation-sentinel.cpp:24-54

The allocation sentinel overrides ordinary `operator new` / `new[]` and delete variants, but it does not override the aligned allocation overloads introduced for over-aligned types, such as `operator new(std::size_t, std::align_val_t)` and `operator new[](std::size_t, std::align_val_t)`. Any DSP code that allocates an over-aligned buffer or object during the measured processing region can bypass these counters entirely, so the FR-014 “zero allocations” test can report clean while real heap traffic occurred.

The blast radius is medium: this is test infrastructure, not shipped runtime behavior, but it weakens a stated portability/realtime invariant and could let downstream code trust a false no-allocation guarantee. A reasonable fix is to add the aligned new/delete overload set and increment the same thread-local counters before delegating to aligned allocation/free APIs.

### SVF “known-good reference” is only a loose behavioral smoke test

Finding-ID: AUDIT-BARRAGE-codex-02
Status:     open
Severity:   medium
Surface:    tests/support/svf-reference.h:5-45 and tests/core/svf-test.cpp:11-15

The comments claim this is a “known-good SVF frequency-response reference” and that the SVF test checks “per-mode frequency response vs the known-good references,” but the implementation only asserts broad inequalities: lowpass passes 100 Hz and attenuates 8 kHz, highpass does the inverse, and bandpass is louder at cutoff than at two edge frequencies. That would not catch many materially wrong SVF implementations: incorrect Q mapping, wrong cutoff warping, first-order behavior, significant gain error, or a bandpass with the wrong bandwidth could still satisfy these loose thresholds.

The blast radius is medium because the feature’s core DSP correctness is being certified by tests that do not actually encode the claimed reference response. A reasonable fix is to store or compute explicit expected magnitudes for each mode at several frequencies and tolerances, then assert against those values rather than only relative pass/stop behavior.
