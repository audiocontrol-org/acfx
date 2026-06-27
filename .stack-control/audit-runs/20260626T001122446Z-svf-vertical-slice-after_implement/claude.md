I'll audit chunk `b04c878073d5d626` of the `svf-vertical-slice` feature — the CMake bootstrap/toolchain layer plus the core DSP contract headers (`audio-block.h`, `effect.h`, `param-id.h`, `parameter.h`, `process-context.h`). I read each diff line-by-line, cross-checking the toolchain flags against the documented SoCs, the parameter math against its own round-trip claims, and the dependency pins against the honesty narrative in the comments.

### Daisy toolchain sets no C++ standard while the core requires C++17, asymmetric with the Teensy toolchain

Finding-ID: AUDIT-BARRAGE-claude-01
Status:     open
Severity:   medium
Surface:    cmake/toolchains/daisy.cmake:1-35 (vs. cmake/toolchains/teensy.cmake:27-31); core/dsp/parameter.h:7-9

`cmake/toolchains/teensy.cmake` explicitly pins `CMAKE_CXX_STANDARD` (lines 27-31), but `cmake/toolchains/daisy.cmake` never sets it — it configures CPU flags and `-fno-exceptions -fno-rtti` and stops. The core, however, hard-requires C++17: `core/dsp/parameter.h` includes `<string_view>` and uses `std::string_view name;` (line ~21) and `std::clamp` (lines ~72,~82), and `core/dsp/effect.h` uses `std::void_t`/`std::declval` (C++17). The Daisy build therefore depends entirely on the `arm-none-eabi-g++` *compiler default* standard. If an adopter's installed cross-compiler defaults to anything below gnu++17, the core fails to compile with cryptic `string_view`/`clamp` errors — directly breaking the feature's headline US3 claim ("core proven ARM-portable").

The asymmetry is itself the signal: the fact that the Teensy toolchain bothers to set the standard implies the build is *not* relying on a global `CMAKE_CXX_STANDARD` set elsewhere — otherwise the Teensy setting would be redundant. **Cross-file caveat:** `CMakeLists.txt` lives in another chunk (`952c352c…`) and is not visible here; if it sets `CMAKE_CXX_STANDARD 17` globally after `project()`, this is moot for both targets and the finding downgrades to a redundancy in `teensy.cmake`. A reasonable fix: set `CMAKE_CXX_STANDARD 17` (and `CMAKE_CXX_STANDARD_REQUIRED ON`) in `daisy.cmake` to match, or hoist it to the top-level `CMakeLists` and drop the per-toolchain setting.

### Teensy toolchain comment claims auto-detection of the C++ standard that the code does not implement

Finding-ID: AUDIT-BARRAGE-claude-02
Status:     open
Severity:   medium
Surface:    cmake/toolchains/teensy.cmake:5-9, 23-31

The header comment states `ACFX_TEENSY_CXX_STANDARD is set to the highest standard that toolchain supports (>= 17)` (lines 6-7), and the inline comment says `Raised here if the installed toolchain supports more` (line 25). The actual code does no detection at all — it hardcodes `17` unless an external `-DACFX_TEENSY_CXX_STANDARD=…` is passed:

```
if(NOT DEFINED ACFX_TEENSY_CXX_STANDARD)
  set(ACFX_TEENSY_CXX_STANDARD 17)
endif()
```

There is no probe of the toolchain and nothing that "raises" the value. This is documentation drift that an unattended agent (or a developer extending the target) would act on wrongly: trusting the comment, they would assume C++20 features are available on Teensy and that the `Effect` *concept* path in `core/dsp/effect.h` (gated on `__cpp_concepts`, line 15) engages. In reality the standard is pinned at 17, `__cpp_concepts` stays undefined, and the C++17 duck-typed degradation path *always* runs on Teensy — so C++20 code an author adds silently won't compile. Fix: either implement an actual `check_cxx_compiler_flag`/`try_compile` probe that matches the narrative, or correct the comment to state plainly "pinned to C++17; override manually if your toolchain supports more."

### normalize()/denormalize() emit NaN/Inf for degenerate parameter descriptors with no guard

Finding-ID: AUDIT-BARRAGE-claude-03
Status:     open
Severity:   medium
Surface:    core/dsp/parameter.h:48-52, 66-70, 79-84

The pure mapping functions trust the descriptor's ranges without validating them, and they are `noexcept` (so they cannot signal the error per the project's "raise descriptive errors" rule). Three concrete NaN/Inf paths:

1. Logarithmic `denormalize` (lines ~49-51): `d.min * std::pow(d.max / d.min, norm)` — if `d.min <= 0` (e.g. a gain-in-dB control whose min is 0 or negative), this produces NaN/garbage. The only protection is a code comment "Requires min, max > 0."
2. Logarithmic `normalize` (lines ~80-81): `std::log(plain / d.min) / std::log(d.max / d.min)` — NaN if `min <= 0`, and a divide-by-zero if `max == min`.
3. Linear `normalize` (line ~84): `(plain - d.min) / (d.max - d.min)` — divide-by-zero → ±Inf/NaN when `max == min`.

A NaN coefficient flowing from `setParameter` into the SVF state poisons the filter (silence or denormal blowup) and is invisible at the call site. Because these are author-supplied constexpr descriptors, a malformed table is a programming error that is *never* surfaced. A reasonable fix: add a `static_assert`-able / debug-build `assert` on descriptor validity at construction, and in release clamp the denominator / guard `min>0` so the function degrades to a defined value rather than NaN. At minimum, the `max == min` divide must be guarded.

### JUCE pin `8.0.14` is unverified and, if nonexistent, fails the desktop/plugin configure

Finding-ID: AUDIT-BARRAGE-claude-04
Status:     open
Severity:   low
Surface:    cmake/dependencies.cmake:13-16, 62-66

The comment block is commendably honest: it segregates pins "verified by an in-session fetch+build" (DaisySP, doctest) from pins "captured from the upstream repos (real refs)" whose "first-fetch verification happens the first time each target's preset is configured" (lines 13-22). JUCE `8.0.14` falls in the *unverified* bucket. Unlike the other unverified pins (which are full 40-char SHAs that either resolve or don't), `8.0.14` is a *tag name* — and if that tag does not exist upstream, `CPMAddPackage(... GIT_TAG 8.0.14)` fails the configure for both `ACFX_BUILD_DESKTOP` targets (workbench + plugin) with a git-checkout error, blocking US1/US2 entirely on a fresh machine.

Because the diff itself flags this pin as unverified, this is largely dispositioned-by-honesty rather than a hidden defect — hence low severity, not high. But the blast radius if the tag is wrong is a total desktop-build break, so it is worth an operator confirming `8.0.14` resolves before relying on the desktop presets. The other capture-only pins (clap-juce, libDaisy, teensy cores/audio) are immutable SHAs and carry the same unverified caveat but cannot be a *typo'd-tag* failure mode the way a version string can. Fix: resolve the JUCE tag to its commit SHA once verified, matching the SHA-pin discipline used for every other dependency, so the pin can't drift or be a phantom tag.