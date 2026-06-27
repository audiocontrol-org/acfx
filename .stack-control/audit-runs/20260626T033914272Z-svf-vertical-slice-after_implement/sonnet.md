I'll walk the diff carefully across all eight files and report findings independently.

---

### `AudioBlock::channel(int ch)` has no bounds check — silent UB on out-of-range access

Finding-ID: AUDIT-BARRAGE-claude-01
Status:     open
Severity:   medium
Surface:    core/dsp/audio-block.h:20-21

`channel(int ch)` performs `channels_[ch]` with no guard. If `ch < 0` or `ch >= numChannels_`, the result is undefined behaviour — out-of-range pointer arithmetic on `float* const*`. `SvfEffect::process()` mitigates this at its call site by computing `min(io.numChannels(), numChannels_)`, but `AudioBlock` is a public API and any adapter that calls `channel()` with an unchecked index will corrupt memory silently in the audio callback. A one-line guard (`if (ch < 0 || ch >= numChannels_) return nullptr;`) or a debug `assert` would contain the blast radius without violating the RT-safety constraint.

The blast radius is medium rather than high because all current callers clamp first and the corruption only manifests in callers that fail to do so — but the API as written offers no protection against this class of caller bug.

---

### `normalize()` does not sanitize NaN input — UB for discrete params, silent poison for continuous

Finding-ID: AUDIT-BARRAGE-claude-02
Status:     open
Severity:   medium
Surface:    core/dsp/parameter.h:80-113

`denormalize()` explicitly neutralises non-finite input via `detail::clamp01` (line 55–56, with a clear comment about NaN-hostile comparisons). `normalize()` has no equivalent guard.

For **discrete** params (line 82): `static_cast<int>(NaN)` is undefined behaviour in C++ — the float-to-integer conversion is only defined when the value is representable. On x86 this yields `INT_MIN`; on ARM (a primary target) it yields 0 per AAPCS, but neither is guaranteed.

For **continuous** params (line 93): `std::clamp(NaN, d.min, d.max)` is implementation-defined. All comparison predicates with NaN are false, so most implementations return NaN unchanged, which then propagates through the `log`/linear formula and out of `normalize`.

The downstream effect is that a NaN returned by `normalize` lands in `setParameter → floatBits → pendingBits_`, and is consumed in `applyPending → pendingValue → denormalize`. There, `clamp01(NaN)` clamps it to 0, so the audio thread applies the descriptor minimum (e.g. 20 Hz cutoff) rather than crashing. The harm is incorrect audio output rather than a crash, but the discrete-case UB and the silent wrong-value path are both worth closing. Adding a `clamp01` call at the top of `normalize`'s continuous branch, and a `std::clamp(static_cast<int>(plain), 0, count-1)` guard before the int cast in the discrete branch, removes both issues.

---

### Triple coupling between `Param` enum, `ParamId.value`, and array index has no compile-time guard

Finding-ID: AUDIT-BARRAGE-claude-03
Status:     open
Severity:   medium
Surface:    core/effects/svf/svf-effect.h:40-56, 120-140

`SvfEffect` relies silently on three values being equal for each parameter:

1. The `Param` enum member (`kCutoff = 0`, `kResonance = 1`, `kMode = 2`)
2. `kParams[i].id.value` (the `ParamId` embedded in the descriptor)
3. The index `i` into `pendingBits_[]` / `pendingDirty_[]`

In `setParameter` (line 120), the incoming `id.value` is used to index `pendingBits_[i]` and `pendingDirty_[i]`. In `applyPending` (line 126), those arrays are read back by the bare enum value `kCutoff`, `kResonance`, `kMode`. If these three numbers ever diverge — for instance, a new parameter is inserted before kResonance and only the `kParams` array is updated — the wrong dirty slot is set and the wrong parameter value is applied, silently and without any assertion in release builds.

The existing `static_assert` (lines 74-79) validates descriptor invariants but says nothing about index-value alignment. Three additional lines would close this:

```cpp
static_assert(kParams[kCutoff].id.value   == kCutoff,   "param id/index mismatch");
static_assert(kParams[kResonance].id.value == kResonance, "param id/index mismatch");
static_assert(kParams[kMode].id.value      == kMode,      "param id/index mismatch");
```

The severity is medium rather than high because the code is currently correct; the risk is latent and only activates on future edits — but an unattended agent extending the parameter table would hit it with no warning.

---

### Teensy cmake: comment claims C++ standard auto-detection but no detection code exists

Finding-ID: AUDIT-BARRAGE-claude-04
Status:     open
Severity:   low
Surface:    cmake/toolchains/teensy.cmake:7-8, 27-30

The file header (lines 7-8) states: *"ACFX_TEENSY_CXX_STANDARD is set to the highest standard that toolchain supports (≥ 17). The same core/effects/svf source compiles here; where C++20 concepts are unavailable the Effect contract degrades…"*

The actual code (lines 27-30) simply sets `ACFX_TEENSY_CXX_STANDARD` to `17` unconditionally if it is not already defined, with a comment "Raised here if the installed toolchain supports more." There is no probe, no try-compile check, and no logic that raises the value. The comment says auto-detection happens; the code makes it a static default that a caller must override manually.

An operator reading the header may believe their C++20 toolchain will automatically get concept-constrained templates; it won't unless they pass `-DACFX_TEENSY_CXX_STANDARD=20` explicitly. Fix: either add a `try-compile` probe or reword the comment to say "defaults to C++17; override by passing `-DACFX_TEENSY_CXX_STANDARD=<N>` if your toolchain supports a higher standard."

---

### C++17 `is_effect` trait does not verify the return type of `parameters()`

Finding-ID: AUDIT-BARRAGE-claude-05
Status:     open
Severity:   informational
Surface:    core/dsp/effect.h:46-62

The C++17 `is_effect` trait (lines 46-62) checks that `T::parameters()` is a callable member but does not check that its return type is convertible to `span<const ParameterDescriptor>`. A type that satisfies all other members but returns `span<ParameterDescriptor>` (mutable, not const), or `std::vector<ParameterDescriptor>`, or any other type, passes `is_effect_v<T>` as `true`. The trait therefore does not fully diagnose the Effect contract it purports to represent; on a C++17 toolchain, the real enforcement comes from template instantiation elsewhere.

This is labelled "diagnostic only; never gates the build" so the blast radius is purely on diagnostic quality — a future author using `is_effect_v` in a `static_assert` to verify their implementation on a Teensy build will see a false positive. No runtime behaviour is affected. Adding `std::is_convertible_v<decltype(T::parameters()), span<const ParameterDescriptor>>` to the `void_t` pack would bring the trait in line with its name.