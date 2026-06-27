### `CMAKE_CXX_STANDARD_REQUIRED` not set in Teensy toolchain

Finding-ID: AUDIT-BARRAGE-claude-01
Status:     open
Severity:   medium
Surface:    cmake/toolchains/teensy.cmake:28-31

`CMAKE_CXX_STANDARD` is assigned (default 17 via `ACFX_TEENSY_CXX_STANDARD`) but `CMAKE_CXX_STANDARD_REQUIRED` is never set to `ON`. Without it, if the installed arm-none-eabi toolchain does not support C++17, CMake silently falls back to an older standard rather than failing the configure step with an actionable error. The core headers use `std::string_view` (`parameter.h`) and `std::clamp` (`parameter.h`) which are C++17; a silent downgrade would produce cryptic undefined-symbol link errors rather than the clear "C++17 is required" message a `REQUIRED` flag would give. Fix: add `set(CMAKE_CXX_STANDARD_REQUIRED ON)` immediately after the `CMAKE_CXX_STANDARD` assignment.

---

### Misleading comment: standard is never auto-raised beyond 17

Finding-ID: AUDIT-BARRAGE-claude-02
Status:     open
Severity:   low
Surface:    cmake/toolchains/teensy.cmake:25-27

The comment at line 25 reads: *"Raised here if the installed toolchain supports more."* The code does not implement any such detection — it simply defaults to 17 and lets a user-supplied `ACFX_TEENSY_CXX_STANDARD` override it. No CMake logic probes the toolchain and raises the standard. The comment describes a capability that does not exist; a developer relying on it would expect C++20 concepts to be enabled automatically when available, but they will not be. The comment should either describe what the code actually does (accepts a user-supplied override, defaults to 17) or the code should implement the detection it promises.

---

### `AudioBlock::channel()` has no bounds assertion

Finding-ID: AUDIT-BARRAGE-claude-03
Status:     open
Severity:   medium
Surface:    core/dsp/audio-block.h:21

`channel(int ch)` returns `channels_[ch]` with no precondition check of any kind — not even a debug-mode `assert`. For the audio hot path, an `assert(ch >= 0 && ch < numChannels_)` is the correct tool: it compiles out in release (zero RT cost) while catching OOB access in test builds. As written, a caller passing a negative or out-of-range `ch` silently dereferences an invalid pointer, producing undefined behavior that manifests unpredictably (wrong output, crashes, or silent corruption) and is very hard to diagnose after the fact. The same gap applies to negative `numSamples_` or `numChannels_` being passed to the constructor. Add asserts at the constructor site and in `channel()`.

---

### `normalize()` linear branch silently returns NaN when `max == min`

Finding-ID: AUDIT-BARRAGE-claude-04
Status:     open
Severity:   medium
Surface:    core/dsp/parameter.h:95-97

The linear branch in `normalize` computes `(plain - d.min) / (d.max - d.min)`. When `d.max == d.min` this is `0/0`, which is NaN. The debug `assert` covering this case exists only in the logarithmic branch (line 93); the linear branch has none. `isValidDescriptor` requires `d.max > d.min`, and `SvfEffect`'s compile-time `static_assert` guards its own table — but `normalize` is a standalone `inline` free function with no stated preconditions, callable by any future adapter or test. A caller using an unguarded descriptor gets a silent NaN that propagates into any subsequent computation (e.g., a host parameter display or a round-trip check) without any compile- or run-time indication of the fault. Add an `assert(d.max > d.min)` to the linear branch for parity with the logarithmic one.

---

### `default:` fallthrough in kind/skew switches silently mismaps unknown enum values

Finding-ID: AUDIT-BARRAGE-claude-05
Status:     open
Severity:   low
Surface:    core/dsp/parameter.h:58-65, 77-83, 89-97

Both `denormalize` and `normalize` use `switch (d.kind)` and `switch (d.skew)` with `default:` cases that silently fall through to `continuous` / `linear` behavior respectively. If a new `ParamKind` or `ParamSkew` value is added and either switch is not updated, the wrong mapping is applied without any compile-time or run-time warning. The idiomatic guard here is to add `static_assert(false, "unhandled ParamKind")` (or `__builtin_unreachable()`/`assert(false)`) in the `default:` arm, converting silent wrong output into a visible build-or-runtime failure when the enum grows.

---

### `ProcessContext` has no `isValidContext()` guard analogous to `isValidDescriptor()`

Finding-ID: AUDIT-BARRAGE-claude-06
Status:     open
Severity:   medium
Surface:    core/dsp/process-context.h:8-14 and core/effects/svf/svf-effect.h:87-93

`ParameterDescriptor` has a `constexpr isValidDescriptor()` function enforced at compile time via `static_assert` in `SvfEffect`. `ProcessContext` has no equivalent. `SvfEffect::prepare()` accepts any `ProcessContext` silently: a negative `ctx.numChannels` sets `numChannels_` to the negative value (since `-1 < kMaxChannels` is true), causing every subsequent channel loop to execute zero iterations — the filter processes no audio with no error. A zero `ctx.sampleRate` causes `clampedCutoff()` to clamp all frequencies to 20 Hz (the `maxFreq = 0 * 0.32 = 0` branch). Both are silent wrong-behavior paths with no diagnostic. Adding a `constexpr bool isValidContext(const ProcessContext&)` checked by an assert (or static_assert where possible) in `prepare()` closes this gap consistently with how the rest of the API is defended.