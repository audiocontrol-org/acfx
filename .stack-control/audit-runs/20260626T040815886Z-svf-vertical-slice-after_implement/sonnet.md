### Missing `CMAKE_CXX_STANDARD_REQUIRED` lets CMake silently downgrade the C++ standard

Finding-ID: AUDIT-BARRAGE-claude-01
Status:     open
Severity:   medium
Surface:    cmake/toolchains/teensy.cmake:28-32

`teensy.cmake` sets `CMAKE_CXX_STANDARD` to `ACFX_TEENSY_CXX_STANDARD` (defaulting to 17) but never sets `CMAKE_CXX_STANDARD_REQUIRED ON`. Without that flag, CMake's default behavior is to silently compile with a lower standard if the installed arm-none-eabi toolchain doesn't fully advertise support for the requested one. The consequence is a stealth degradation: constexpr lambdas in `static_assert` (svf-effect.h's parameter-table guard), the `is_effect` detection trait in effect.h, and every `std::void_t` usage are all C++17 features. If the toolchain silently falls back to C++14 the build either produces a cryptic template error deep in instantiation or — worse — silently compiles a subtly broken binary. The fix is one line after line 32: `set(CMAKE_CXX_STANDARD_REQUIRED ON)`.

---

### Preprocessor `-D` defines bleed into `CMAKE_EXE_LINKER_FLAGS_INIT`

Finding-ID: AUDIT-BARRAGE-claude-02
Status:     open
Severity:   low
Surface:    cmake/toolchains/teensy.cmake:34-36

`_teensy_cpu_flags` is defined as a string containing both ABI flags (`-mcpu`, `-mthumb`, `-mfpu`, `-mfloat-abi`) and preprocessor defines (`-DF_CPU=600000000 -D__IMXRT1062__`). The ABI flags are legitimately needed in `CMAKE_EXE_LINKER_FLAGS_INIT` to drive library variant selection; the `-D` defines are not. Passing `-DF_CPU=600000000` to `arm-none-eabi-ld` is a category error: the linker has no concept of preprocessor macros. Most versions of ld silently ignore unknown `-D` flags, but some toolchain versions emit a warning, and with LTO enabled the linker may re-invoke the compiler with the combined flag set, causing double-definition diagnostics. The fix is to split `_teensy_cpu_flags` into an ABI-flags variable (suitable for both compiler and linker) and a separate `_teensy_cpp_defines` variable used only in `CMAKE_{C,CXX}_FLAGS_INIT`.

---

### `prepare()` stores negative `numChannels_` for invalid `ProcessContext::numChannels`

Finding-ID: AUDIT-BARRAGE-claude-03
Status:     open
Severity:   medium
Surface:    core/effects/svf/svf-effect.h:111-112

`prepare()` computes `numChannels_` with:
```cpp
numChannels_ = ctx.numChannels < kMaxChannels ? ctx.numChannels : kMaxChannels;
```
If `ctx.numChannels` is zero or negative (an adapter bug, but not one the core guards against), `numChannels_` receives that value because `0 < 8` and `-1 < 8` are both true. The initialisation loop then runs `for (int ch = 0; ch < 0; ...)` — zero iterations — leaving every `SvfPrimitive` in its default-constructed state (uninitialized coefficients). In `process()` the channel clamp:
```cpp
const int channels = io.numChannels() < numChannels_ ? io.numChannels() : numChannels_;
```
evaluates to `numChannels_` when `io.numChannels()` (a positive value) is NOT less than a negative `numChannels_`, yielding a negative `channels`. The processing loop then does not execute and audio passes through silently, unfiltered. There is no debug `assert` to catch this at development time. A minimal fix is `assert(ctx.numChannels > 0 && ctx.maxBlockSize > 0)` at the top of `prepare()`; a more robust fix also clamps `numChannels_` to `max(0, ...)` to prevent the negative-storage footgun even in release builds where the assert compiles out.

---

### `AudioBlock::channel()` has no bounds check — not even under debug builds

Finding-ID: AUDIT-BARRAGE-claude-04
Status:     open
Severity:   low
Surface:    core/dsp/audio-block.h:20-21

```cpp
float* channel(int ch) const noexcept { return channels_[ch]; }
```
`channels_` is a `float* const*` (pointer-to-pointer). An out-of-range `ch` produces undefined behaviour: an unchecked dereference of an out-of-bounds pointer. The rest of the codebase applies `assert` for debug-build precondition checking (e.g. the logarithmic parameter assertions in parameter.h). `channel()` has no equivalent guard: `assert(ch >= 0 && ch < numChannels_)` would make misuse visible at development time without any RT overhead in release builds. The risk is real: `process()` in svf-effect.h drives its channel index from `io.numChannels()` vs. `numChannels_` min-of-two; a mismatch there is the primary path to a wild channel access.

---

### `dsp/span.h` included in `audio-block.h` but never used

Finding-ID: AUDIT-BARRAGE-claude-05
Status:     open
Severity:   informational
Surface:    core/dsp/audio-block.h:3

`#include "dsp/span.h"` appears at line 3 of `audio-block.h`. The `span` type is not referenced anywhere in the file; `AudioBlock` stores raw pointer-plus-count members. The include is dead. On C++20 the include transitively pulls in `<span>` (which in turn pulls platform headers); on C++17 it pulls in `<cstddef>` and `<utility>` via the polyfill. Neither causes a bug today, but the spurious dependency widens the header's include closure unnecessarily and will confuse a future reader trying to understand why `span` touches the audio-block interface.

---

### New `ParamKind` or `ParamSkew` enum values fall through to the wrong branch silently

Finding-ID: AUDIT-BARRAGE-claude-06
Status:     open
Severity:   low
Surface:    core/dsp/parameter.h:69-90 (denormalize), 97-115 (normalize)

Both `denormalize()` and `normalize()` use a two-level switch structure:

```cpp
case ParamKind::continuous:
default:
    break;
// ...
case ParamSkew::linear:
default:
    return d.min + norm * (d.max - d.min);
```

The `default:` arms mean that any future `ParamKind` or `ParamSkew` enumerator added to `param-id.h` is silently treated as `continuous`/`linear` respectively. Because the parameter system is intended to be the single source of truth consumed by all adapters (FR-003), a silently mishandled new kind would produce wrong audio output without any compile-time or runtime diagnostic. Adding `[[nodiscard]]` to the functions won't help here; the fix is to replace the `default` arms with explicit cases and let the compiler's `-Wswitch` (or `-Wswitch-enum`) flag missing enumerators, which will catch a new value being added without updating these mapping functions.