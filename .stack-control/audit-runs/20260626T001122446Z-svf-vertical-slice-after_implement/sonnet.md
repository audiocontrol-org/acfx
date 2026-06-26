I'll carefully audit the diff for the `svf-vertical-slice` feature across the files in this chunk.

---

### Logarithmic parameter math: missing precondition guard produces silent NaN in audio path

Finding-ID: AUDIT-BARRAGE-claude-01
Status:     open
Severity:   high
Surface:    core/dsp/parameter.h:57-60 (denormalize), core/dsp/parameter.h:77-79 (normalize)

`denormalize` for `ParamSkew::logarithmic` computes `d.min * std::pow(d.max / d.min, norm)`. When `d.min == 0`, this evaluates to `0 * pow(inf, norm)` which is `NaN`. When `d.min == d.max`, `pow(1.0, norm) = 1.0` but the matching `normalize` path divides by `std::log(d.max / d.min) = std::log(1.0) = 0`, producing `NaN` on the round-trip. Both failures are silent: no assertion fires, no error is thrown, and `NaN` propagates through every subsequent `process()` call without any visible indicator until the adapter detects silence or distortion.

`ParameterDescriptor` is a plain aggregate with no constructor validation, no `static_assert` constraints, and no `isValid()` utility. A future effect that accidentally sets `min = 0.0f` with `skew = logarithmic` (a plausible mistake for a cutoff frequency where "minimum" might naively be set to 0 Hz) will silently corrupt the audio stream. The comment on line 55 says "Requires min, max > 0" but this is documentation, not an enforced invariant. A debug-mode `assert(d.min > 0.0f && d.max > d.min)` at the top of the logarithmic branches (both functions) would catch this early without violating real-time constraints. Alternatively, a `static_assert`-backed `ParameterDescriptor::validate()` called in `prepare()` would push the failure to a detectable phase.

---

### `file(DOWNLOAD)` in CPM.cmake has no timeout — configure can hang indefinitely

Finding-ID: AUDIT-BARRAGE-claude-02
Status:     open
Severity:   medium
Surface:    cmake/CPM.cmake:19-23

The `file(DOWNLOAD ...)` call that fetches `CPM_${CPM_DOWNLOAD_VERSION}.cmake` from GitHub has no `TIMEOUT` option. If GitHub is unreachable, rate-limiting, or the network is flaky (common in CI environments behind proxies), the configure step will block indefinitely until the CI job's global timeout kills it. That global kill produces a generic timeout failure with no hint that the root cause was a stalled download.

CMake's `file(DOWNLOAD)` accepts a `TIMEOUT <seconds>` argument. Adding `TIMEOUT 30` (or a CI-env-tunable value) converts a silent hang into a fast, informative `file(DOWNLOAD)` status failure. The `INACTIVITY_TIMEOUT` option is also available for additional protection. This is a straightforward one-line fix.

---

### `--specs=nosys.specs` in daisy toolchain likely conflicts with libDaisy syscalls at link time

Finding-ID: AUDIT-BARRAGE-claude-03
Status:     open
Severity:   medium
Surface:    cmake/toolchains/daisy.cmake:30, cmake/dependencies.cmake:57-63

`CMAKE_EXE_LINKER_FLAGS_INIT` in `daisy.cmake` includes `--specs=nosys.specs`. This spec file provides stub implementations of POSIX syscalls (`_write`, `_sbrk`, `_read`, etc.) via `arm-none-eabi-gcc`'s bundled `libnosys`. libDaisy also ships its own `syscalls.c` implementing the same weak symbols for its STM32 HAL integration. When the Daisy adapter links against libDaisy sources (which it must, since `DOWNLOAD_ONLY YES` means the adapter's CMakeLists.txt manually adds libDaisy source files), this will produce multiply-defined symbol linker errors.

The canonical bare-metal libDaisy CMake pattern omits `nosys.specs` precisely because libDaisy provides these stubs. The `adapters/daisy/CMakeLists.txt` diff is not in this chunk, so the exact link-time behavior cannot be fully assessed here; however, the flag is set at toolchain init level and would need to be explicitly stripped per-target if the adapter's CMakeLists.txt knows to do so — a fragile arrangement. The fix is to remove `--specs=nosys.specs` from `CMAKE_EXE_LINKER_FLAGS_INIT` and let libDaisy's own stubs be the sole provider.

---

### `AudioBlock::channel()` has no bounds check and no debug assertion

Finding-ID: AUDIT-BARRAGE-claude-04
Status:     open
Severity:   medium
Surface:    core/dsp/audio-block.h:21

`float* channel(int ch) const noexcept { return channels_[ch]; }` performs no bounds check. An out-of-range `ch` (negative or `>= numChannels_`) is silent undefined behavior: it reads from arbitrary memory within the adapter's buffer array, or beyond it. In the real-time path, runtime exceptions are inappropriate, but a `assert(ch >= 0 && ch < numChannels_)` guarded by `NDEBUG` (i.e., active only in debug builds) carries zero cost in release and catches the class of effect bugs where a stereo effect is run in a mono context without the effect checking `numChannels()`.

The `ProcessContext` already carries `numChannels` as an upper bound; an effect that ignores `ctx.numChannels` on `prepare()` and calls `block.channel(1)` on a mono stream will compile, link, and run silently, only manifesting as corrupted audio or a crash under specific buffer-alignment conditions.

---

### `CMAKE_CXX_STANDARD` set in teensy toolchain instead of `CMAKE_CXX_STANDARD_INIT`

Finding-ID: AUDIT-BARRAGE-claude-05
Status:     open
Severity:   medium
Surface:    cmake/toolchains/teensy.cmake:30

```cmake
set(CMAKE_CXX_STANDARD ${ACFX_TEENSY_CXX_STANDARD})
```

CMake toolchain files are loaded during the initial cache setup pass, before the main `CMakeLists.txt` runs. Setting `CMAKE_CXX_STANDARD` (a cache-or-normal variable) in the toolchain establishes a value that the main `CMakeLists.txt`'s own `set(CMAKE_CXX_STANDARD ...)` will silently override if it appears there. The canonical toolchain-level pattern is `set(CMAKE_CXX_STANDARD_INIT <value>)`, which acts as the default for the `CMAKE_CXX_STANDARD` cache entry but yields gracefully to any explicit value set by the project. The current code could silently produce a C++17 build even if the main `CMakeLists.txt` sets C++20, or a C++20 build if the main file sets it before the toolchain's variable is visible, depending on CMake version and generator behavior. The intent is clear (cap Teensy at 17); the mechanism is fragile.

---

### Dead `#include "dsp/span.h"` in `audio-block.h`

Finding-ID: AUDIT-BARRAGE-claude-06
Status:     open
Severity:   low
Surface:    core/dsp/audio-block.h:3

`#include "dsp/span.h"` appears in `audio-block.h` but `span` (or `acfx::span`) is not referenced anywhere in the file. `AudioBlock` stores raw `float* const*` and `int` members without using any span type. The include is either a leftover from an earlier design or premature inclusion. It adds a header dependency that could cause transitive rebuild cascades and will confuse readers who look for where `span` is used in the class.

---

### Silent `default:` fallthrough in `denormalize`/`normalize` for unrecognized enum values

Finding-ID: AUDIT-BARRAGE-claude-07
Status:     open
Severity:   low
Surface:    core/dsp/parameter.h:44-45, core/dsp/parameter.h:65-66, core/dsp/parameter.h:73-74, core/dsp/parameter.h:84-86

Both `denormalize` and `normalize` use `default:` in their `ParamKind` and `ParamSkew` switches. An unrecognized `ParamKind` silently falls through to the continuous-skew path; an unrecognized `ParamSkew` silently produces a linear mapping. This pattern creates a quiet correctness trap for any future author who adds a new enum variant without updating these functions: their new kind/skew will silently behave as linear-continuous rather than producing a compile warning or runtime assertion. Adding a `static_assert(false)` or `assert(false && "unhandled ParamKind")` in the `default:` branches, or using `-Wswitch` / `-Wswitch-enum` compile flags (and removing `default:` to let the compiler warn on unhandled cases), converts silent misbehavior into a build-time or debug-time signal.

---

### `workbench-app.cpp` listed in chunk scope but absent from diffs

Finding-ID: AUDIT-BARRAGE-claude-08
Status:     open
Severity:   informational
Surface:    adapters/workbench/workbench-app.cpp (missing from diff)

`adapters/workbench/workbench-app.cpp` is named in this chunk's file scope, but no diff is present for it. All other files in this chunk are `new file mode` additions. If `workbench-app.cpp` is also a new file (which the workbench feature implies), its absence from the diff is an audit coverage gap — the top-level JUCE `JUCEApplication` subclass, which wires together the audio source, parameter views, and MIDI bindings, cannot be audited for correct use of `AudioBlock`, `Effect`, or `ProcessContext`. If the file exists and was not diffed, the operator should confirm whether it falls under a prior diff range; if it does not exist yet, the adapter is incomplete.