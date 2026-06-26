### Allocation sentinel misses aligned heap allocations

Finding-ID: AUDIT-BARRAGE-codex-01
Status:     open
Severity:   high
Surface:    tests/support/allocation-sentinel.cpp:24-54

The sentinel claims to count global heap traffic, but it only overrides ordinary `operator new`, `operator new[]`, and their non-aligned delete forms. In C++17 and later, over-aligned allocations dispatch through `operator new(std::size_t, std::align_val_t)` / array variants, which are not counted here. That creates a false negative for the RT no-allocation invariant: production code could introduce an over-aligned allocation in the measured processing path and the test would still report zero allocations.

The blast radius is high because downstream consumers will treat the no-allocation test as proof of audio-thread safety, and this gap silently permits a class of real heap allocations. A reasonable fix is to add the aligned allocation/deallocation overloads, count them through the same thread-local counters, and back the sentinel with a small test-only fixture that proves ordinary and aligned allocations are both observed.

### SVF “reference” test can pass materially wrong filter implementations

Finding-ID: AUDIT-BARRAGE-codex-02
Status:     open
Severity:   medium
Surface:    tests/support/svf-reference.h:4-49 and tests/core/svf-test.cpp:52-83

The SVF test describes “known-good references” and “frequency response vs the known-good references,” but the actual assertions are broad qualitative checks: lowpass passes 100 Hz, attenuates 8 kHz, highpass does the inverse, and bandpass has more gain at 1 kHz than at the two edges. There is no numeric expected magnitude at cutoff, no resonance-sensitive reference, no slope tolerance, and no comparison against an independently implemented SVF transfer function. A wrong implementation with the right broad shape but incorrect cutoff calibration, Q behavior, or topology could pass.

The blast radius is medium because this is test coverage rather than shipping DSP code, but it weakens the acceptance signal for a core feature. A reasonable fix is to either rename the helper/comments to reflect that these are smoke tests, or add actual reference magnitudes generated from an independent analytic implementation for the tested modes and parameter settings.
