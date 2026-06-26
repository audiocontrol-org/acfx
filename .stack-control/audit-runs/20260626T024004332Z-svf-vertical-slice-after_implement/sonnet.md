### `CMAKE_CXX_STANDARD` in teensy toolchain is advisory, not enforced — comment implies auto-probing that doesn't exist

Finding-ID: AUDIT-BARRAGE-claude-01
Status:     open
Severity:   medium
Surface:    cmake/toolchains/teensy.cmake:26-32

Lines 26–27 read: "Raised here if the installed toolchain supports more." No probing logic follows — the only code is a guarded default-to-17. Nothing queries the installed compiler's actual C++ dialect support. The comment asserts behavior that the file doesn't implement.

More importantly, `CMAKE_CXX_STANDARD` set in a toolchain file is overridable: if the project's root `CMakeLists.txt` calls `set(CMAKE_CXX_STANDARD 20)` or a target uses `target_compile_features(... cxx_std_20)`, CMake silently overwrites the toolchain's value. The Teensy arm-none-eabi toolchain at C++17 will then receive a `-std=c++20` flag it may not support, producing cryptic template errors rather than a clean "toolchain capability exceeded" message. The defensive move is to append `-std=c++17` directly to `CMAKE_CXX_FLAGS_INIT` (which is authoritative) rather than relying on `CMAKE_CXX_STANDARD` (which is project-overridable). A companion `set(CMAKE_CXX_STANDARD_REQUIRED ON)` would also help CMake produce a cleaner error if the project tries to exceed the declared standard.

Blast-radius: a toolchain misconfiguration surfaces as build failures only when a Teensy build is actually attempted; until then it silently carries the wrong claim about auto-probing, which misleads any adapter author who reads the file.

---

### `prepare()` silently clamps channel count with no assertion or error surface

Finding-ID: AUDIT-BARRAGE-claude-02
Status:     open
Severity:   medium
Surface:    core/effects/svf/svf-effect.h:96

```cpp
numChannels_ = ctx.numChannels < kMaxChannels ? ctx.numChannels : kMaxChannels;
```

If `ctx.numChannels` exceeds `kMaxChannels` (8), this silently caps to 8 and continues. In `process()`, the channel loop bound is `min(io.numChannels(), numChannels_)`, so any channels above index 7 pass through dry — unfiltered — with no indication to the caller. For a mono/stereo workbench this never fires, but the 8-channel cap is exactly 7.1 surround; a 7.1.4 Atmos bed (12 channels) would silently half-filter. An `assert(ctx.numChannels <= kMaxChannels)` in debug builds would surface this early. The cap itself may be intentional policy, but policy that fails silently is a trap for adapter authors.

Blast-radius: an adapter running >8 channels ships with a partial wet signal and no diagnostic; the defect is audible only on the extra channels, which may not be monitored during development.

---

### `normalize()` discrete path: `static_cast<int>(plain)` is UB for values > INT_MAX

Finding-ID: AUDIT-BARRAGE-claude-03
Status:     open
Severity:   low
Surface:    core/dsp/parameter.h:89-94

```cpp
int idx = static_cast<int>(plain);
idx = std::clamp(idx, 0, count - 1);
```

`static_cast<int>(float)` is undefined behavior in C++ when the float value is outside the range representable by `int` (C++ [conv.fpint]). The guard after the cast (`std::clamp`) cannot rescue an already-UB result. `denormalize()` for discrete params also has this cast but is preceded by `clamp01(norm)`, which bounds `norm` to [0,1]; the resulting `idx` is at most `count − 1` and well within `int` range. `normalize()` takes `plain` unconstrained; a caller passing an invalid bucket index of, say, `3e10f` triggers UB before the clamp can fire. An `assert(plain >= 0.0f && plain < static_cast<float>(count))` or a pre-cast clamp to `[0, static_cast<float>(count − 1)]` would close this.

Blast-radius: in the current codebase all callers should pass valid bucket indices, so this is unlikely to trigger in practice; however, UB allows the compiler to eliminate the downstream clamp as unreachable, turning a guarded function into one that isn't.

---

### C++17 `is_effect` trait is structurally dead code — no call site enforces it

Finding-ID: AUDIT-BARRAGE-claude-04
Status:     open
Severity:   low
Surface:    core/dsp/effect.h:44-63

In the C++17 path `ACFX_EFFECT_CONCEPT` expands to `typename`, so every `template <ACFX_EFFECT_CONCEPT T>` declaration is unconstrained. The `is_effect<T>` / `is_effect_v<T>` trait is defined but never `static_assert`-ed anywhere visible in this diff. The comment at line 51 — "Template instantiation still enforces the real member signatures" — is accurate only if the template body calls every required member function; a conforming type that satisfies a subset of the contract can slip through on C++17 if none of the unused members are called in a given instantiation. The trait should either be `static_assert`-ed at each instantiation site (e.g., in adapter constructors), or the comment should be tightened to state "enforcement is call-site-dependent, not declaration-site."

---

### `setParameter()` silently no-ops on out-of-range id with no debug assertion

Finding-ID: AUDIT-BARRAGE-claude-05
Status:     open
Severity:   low
Surface:    core/effects/svf/svf-effect.h:141-143

```cpp
if (i >= kNumParams)
    return; // out-of-range id: a programming error; no silent state change
```

The comment correctly labels this a programming error, but in a debug build it remains silent — there is no `assert`. The RT constraint rightly prohibits logging on the audio thread, but `setParameter` is documented as callable from any thread, including the UI thread, where an assert is appropriate. An `assert(i < kNumParams)` would fire during development without introducing any RT-unsafe call. As-written, a stale `ParamId` (e.g., from a refactor that renumbers params) produces no feedback and the parameter change is silently lost.