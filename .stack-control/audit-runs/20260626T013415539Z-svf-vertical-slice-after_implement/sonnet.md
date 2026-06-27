### Missing `sizeof(float) == sizeof(std::uint32_t)` static assertion

Finding-ID: AUDIT-BARRAGE-claude-01
Status:     open
Severity:   low
Surface:    core/effects/svf/svf-effect.h:124-134

The `floatBits`/`bitsFloat` helpers use `std::memcpy` between `float` and `std::uint32_t`, implicitly requiring `sizeof(float) == sizeof(std::uint32_t)`. The code already has a neighboring `static_assert` for lock-free atomics (line 196). IEEE 754 single-precision is 32 bits on every target this project realistically runs on, but the C++ standard does not require it. If ever compiled on a platform with 64-bit `float` (some DSPs) or 16-bit `float`, the copy silently truncates or reads garbage, producing wrong parameter values with no build-time warning. The fix is one line adjacent to the existing assert: `static_assert(sizeof(float) == sizeof(std::uint32_t), "float must be 32 bits for atomic pending-parameter storage");` — cheap insurance, consistent with the defensive style already present.

---

### `normalize()` discrete branch: UB when `plain` is a large or NaN float

Finding-ID: AUDIT-BARRAGE-claude-02
Status:     open
Severity:   low
Surface:    core/dsp/parameter.h:65-68

```cpp
int idx = static_cast<int>(plain);
idx = std::clamp(idx, 0, count - 1);
```

`static_cast<int>(float)` is undefined behaviour when the float value is outside the representable range of `int` (C++ [conv.fpint]), including `+inf`, `-inf`, and `NaN`. The subsequent `std::clamp` does not rescue the UB because it executes after the cast. Blast-radius: in practice, callers pass small integral floats (0.0f, 1.0f, 2.0f) so this never fires; however, `normalize` is a public utility and an errant caller passing a host-provided `plain` value that arrived corrupted (e.g., an uninitialized parameter state) could trigger UB. The fix is to clamp the float before casting: `plain = std::clamp(plain, 0.0f, static_cast<float>(count - 1));` then cast.

---

### `normalize()` linear branch: silent division-by-zero with no assert

Finding-ID: AUDIT-BARRAGE-claude-03
Status:     open
Severity:   low
Surface:    core/dsp/parameter.h:82-83

```cpp
case ParamSkew::linear:
default:
    return (plain - d.min) / (d.max - d.min);
```

If `d.max == d.min` (a degenerate descriptor), this divides by zero, producing `NaN` or `Inf`. The logarithmic branch four lines above has an explicit `assert` for its precondition (`0 < min < max`). The linear branch has no equivalent guard. The asymmetry means a malformed linear descriptor fails silently while a malformed logarithmic descriptor fails loudly in debug builds. The fix is to add `assert(d.max > d.min && "linear parameter requires max > min")` before the division, matching the pattern already established for the logarithmic case.

---

### `SvfEffect::process()` silently leaves extra channels unfiltered — undocumented contract

Finding-ID: AUDIT-BARRAGE-claude-04
Status:     open
Severity:   medium
Surface:    core/effects/svf/svf-effect.h:85-92

```cpp
const int channels = io.numChannels() < numChannels_ ? io.numChannels() : numChannels_;
```

When the `AudioBlock` carries more channels than `prepare()` was told (`io.numChannels() > numChannels_`), `channels = numChannels_` and the extra channels pass through the block completely unfiltered. For stereo-prepared + quad-block, output channels 3 and 4 contain dry audio while channels 1 and 2 are filtered. There is no assert, no log, and no documentation in the function's thread-ownership comment that this is the intended behaviour. An adapter that changes channel count without re-calling `prepare()` — or that is passed a block with a different width than the prepared count — will silently produce mixed dry/wet output that is hard to debug. Either add `assert(io.numChannels() <= numChannels_)` to enforce the contract, or explicitly document "extra channels are passed through unchanged" in the function header.

---

### `setParameter()` silently discards out-of-range `ParamId` in release builds

Finding-ID: AUDIT-BARRAGE-claude-05
Status:     open
Severity:   medium
Surface:    core/effects/svf/svf-effect.h:107-110

```cpp
if (i >= kNumParams)
    return; // out-of-range id: a programming error; no silent state change
```

The comment correctly identifies this as a programming error, but the action in release builds is to return silently — nothing fires, no state changes, and the caller receives no signal that the edit was dropped. The project's stated principle (CLAUDE.md) is "Never implement fallbacks… Throw errors with a description of the missing functionality." `setParameter` is `noexcept` (audio-thread callable), so a thrown exception is not viable, but a debug-mode `assert(i < kNumParams && "setParameter: ParamId out of range")` would catch the error during development and integration testing. Without it, a mis-wired adapter that passes the wrong ID silently no-ops for the full session, only surfacing as a baffling "parameter has no effect" report. The fix is to add the assert immediately before or after the bounds check.

---

### `ProcessorNode::prepare()` and `reset()` lack the stream-stopped precondition comment present in `SvfEffect`

Finding-ID: AUDIT-BARRAGE-claude-06
Status:     open
Severity:   low
Surface:    host/processor-node/processor-node.h:20-22

`SvfEffect` has a detailed thread-ownership block explaining that `prepare()` and `reset()` must be called only while the audio stream is stopped (lines 32-40 of `svf-effect.h`). `ProcessorNode`'s virtual declarations carry no such annotation — an adapter author implementing a new `ProcessorNode` or wiring the existing one sees only `virtual void prepare(...)` and `virtual void reset()` with no hint that they are unsafe to call concurrently with `processBlock`. The interface is the public contract visible to adapter writers; the precondition belongs there. A one-line comment matching `SvfEffect`'s wording suffices.

---

### C++17 build: `SvfEffect` never verified against the `is_effect` trait

Finding-ID: AUDIT-BARRAGE-claude-07
Status:     open
Severity:   informational
Surface:    core/dsp/effect.h:46-60, core/effects/svf/svf-effect.h

On C++20, the `ACFX_EFFECT_CONCEPT` named concept gates every `EffectNode<T>` instantiation. On C++17 (Teensy), the concept is unavailable, `ACFX_EFFECT_CONCEPT = typename`, and the `is_effect_v<T>` detection trait is generated but never used in a `static_assert`. A future effect type that is missing one required method (e.g., omits `reset()`) on a C++17 toolchain gets a cryptic deep-template instantiation error rather than a clean "T does not satisfy is_effect" message. Adding `static_assert(acfx::is_effect_v<SvfEffect>, "SvfEffect must satisfy the Effect contract");` at the bottom of `svf-effect.h` (guarded by `#ifndef __cpp_concepts` or unconditionally, since it compiles on both) provides clear diagnostics on C++17 builds and documents intent.