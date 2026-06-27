### `source_` used in audio callback after `prepareToPlay` exception skips `prepare`

Finding-ID: AUDIT-BARRAGE-claude-01
Status:     open
Severity:   high
Surface:    adapters/workbench/workbench-app.cpp:74–97

`prepareToPlay` wraps both `source_.useLiveInput(inputs)` and `source_.prepare(sampleRate, blockSize)` in a single `try` block. If `useLiveInput` throws an `AudioSourceError`, execution jumps to the catch handler and `source_.prepare` is never called. The catch handler posts an async dialog but does not set any flag that would gate `getNextAudioBlock`. As a result, `source_.fillBlock(region)` at line ~97 runs on a `WorkbenchAudioSource` that was never prepared — a use-before-prepare that the RT callback cannot recover from. Whether `WorkbenchAudioSource::fillBlock` on an unprepared instance crashes, silently outputs garbage, or accesses freed memory is not visible in this diff, but the state is unconditionally reachable: live input present + driver-level error opening it. The constitution (Commandment V) says "raise descriptive errors for missing functionality instead of silently falling back" — the current code shows a dialog but continues running with a broken source, which is a soft fallback rather than a clean stop. A minimal fix: set `bool sourcePrepared_ = false` in the catch path and guard `source_.fillBlock` in `getNextAudioBlock` behind it, or re-throw to abort audio initialisation.

---

### `WorkbenchComponent::params_` is a dead field containing a dangling-span risk

Finding-ID: AUDIT-BARRAGE-claude-02
Status:     open
Severity:   medium
Surface:    adapters/workbench/workbench-app.cpp:42–43, and the `params_` member declaration

`params_` is assigned `node_->parameters()` in the constructor body but is never read anywhere in the class. The `ParameterView` is already initialised from `node_->parameters()` directly in the member-initialiser list. `params_` is therefore dead code. Beyond being noise, the field stores a non-owning `span<const ParameterDescriptor>` that points into memory owned by `node_`. A future refactor that resets `node_` (plausible given `unique_ptr` ownership) would silently dangle `params_` with no compile-time or run-time warning. The field should be removed; if it was intended as a cached view for future use, that intent should be expressed in a comment and the field should at minimum be used or there should be a `static_assert(false, "remove me")` holding the place.

---

### `AudioBlock::channel()` performs unchecked array access with no bounds assertion

Finding-ID: AUDIT-BARRAGE-claude-03
Status:     open
Severity:   medium
Surface:    core/dsp/audio-block.h:23

`float* channel(int ch) const noexcept { return channels_[ch]; }` dereferences `channels_` at `ch` with no bounds check. Out-of-range `ch` is undefined behaviour. In the RT path UB must be avoided — the whole point of `noexcept` here is RT-safety — but the class has `numChannels_` available to validate against. A debug-only `assert(ch >= 0 && ch < numChannels_)` (compiled out in release) would catch every caller that exceeds the prepared channel count without adding any RT overhead in production. The constructor similarly accepts negative `numChannels` and `numSamples` with no validation; a consumer that passes a negative count will silently produce a malformed block. These two missing precondition checks mean every incorrect caller produces silent UB rather than a deterministic assertion failure during development.

---

### `source_.fillBlock` fills all buffer channels; `AudioBlock` processes only the clamped subset

Finding-ID: AUDIT-BARRAGE-claude-04
Status:     open
Severity:   medium
Surface:    adapters/workbench/workbench-app.cpp:93–106

In `getNextAudioBlock`, `region` is constructed with `buffer.getNumChannels()` (all channels in the device buffer), and `source_.fillBlock(region)` writes to every channel. However, `numChannels` is clamped to `preparedChannels_` (the count the effect was prepared for), and `AudioBlock block` is built from only those first `numChannels` channels. Channels beyond `numChannels` receive source audio from `fillBlock` but bypass `node_->processBlock` entirely, leaving them dry regardless of the A/B toggle. If `processed_` is true the operator hears an asymmetric mix: processed audio on channels 0…N-1 and raw (unprocessed) source audio on channels N…M-1. Whether this is intentional (pass-through for surplus channels) or a bug is not documented. Without a comment or test, a downstream integrator will misread this as "all channels are processed when `processed_` is true."

---

### `setNormalized` clamps `discreteCount < 2` to 2, but the constructor adds exactly `discreteCount` items

Finding-ID: AUDIT-BARRAGE-claude-05
Status:     open
Severity:   low
Surface:    adapters/workbench/parameter-view.cpp:60–68 vs. parameter-view.cpp:18–24

In the constructor, `row.combo->addItem` is called `d.discreteCount` times, so a descriptor with `discreteCount == 1` produces a combo with exactly one item (valid index 0 only). In `setNormalized`, the recovery formula uses `count = max(2, row.descriptor.discreteCount)`, which for `discreteCount == 1` yields `count = 2`. A normalised value of `0.75f` then computes `index = floor(0.75 * 2) = 1`, and `setSelectedItemIndex(1, ...)` is called on a one-item combo — an out-of-range selection. JUCE typically silences this rather than crashing, but the control ends up in an indeterminate state. The two sites should agree: either enforce `discreteCount >= 2` as a precondition on the descriptor (and assert it in the constructor), or apply the same `max(2, count)` guard in the constructor when building items.

---

### Teensy toolchain does not set `CMAKE_CXX_STANDARD_REQUIRED`, risking silent standard downgrade

Finding-ID: AUDIT-BARRAGE-claude-06
Status:     open
Severity:   low
Surface:    cmake/toolchains/teensy.cmake:36

`set(CMAKE_CXX_STANDARD ${ACFX_TEENSY_CXX_STANDARD})` is set without a companion `set(CMAKE_CXX_STANDARD_REQUIRED ON)`. Without this, CMake silently degrades to a lower standard if the installed arm-none-eabi toolchain does not support C++17. C++17 features (structured bindings, `if constexpr`, `std::optional`, etc.) that compile cleanly with degradation set to C++14 will produce silent mis-compilation or link failures at runtime rather than a clean configure-time error. The Daisy toolchain file (`daisy.cmake`) does not share this pattern — it does not set `CMAKE_CXX_STANDARD` at all and inherits from the top-level CMake, which is a different shape. Adding `set(CMAKE_CXX_STANDARD_REQUIRED ON)` immediately after the standard assignment in `teensy.cmake` makes the configure step fail loudly when the toolchain does not meet the minimum bar rather than producing a corrupted binary.