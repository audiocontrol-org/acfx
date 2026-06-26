### C++17 effect diagnostics use `std::declval` without including `<utility>`

Finding-ID: AUDIT-BARRAGE-codex-01
Status:     open
Severity:   high
Surface:    core/dsp/effect.h:36-54

The C++17 path includes only `<type_traits>` but uses `std::declval` in the `is_effect` trait. `std::declval` is declared by `<utility>`, so a conforming or lean embedded standard library can fail the Teensy/C++17 build at this header. That directly hits the feature’s portability goal because the Teensy path is the one forced through this branch.

The blast radius is high: an adopter compiling the core on a C++17 embedded toolchain can hit a hard compile failure before any SVF code runs. The reasonable fix is to include `<utility>` in the C++17 branch, or unconditionally near the top of the header.

### `span` feature detection can select the polyfill on C++20 toolchains

Finding-ID: AUDIT-BARRAGE-codex-02
Status:     open
Severity:   medium
Surface:    core/dsp/span.h:11-19

`span.h` checks `__cpp_lib_span` before including `<span>` or `<version>`. Library feature-test macros are guaranteed after including the relevant header or `<version>`, not after `<cstddef>`, so C++20 desktop/Daisy builds can take the polyfill branch even though `std::span` exists. That contradicts the stated contract on lines 3-5 that C++20 toolchains use “exactly std::span.”

The blast radius is medium: current core code may still compile against the subset polyfill, but downstream code relying on `acfx::span` being an alias to `std::span` or on non-subset `std::span` behavior will compile differently from the documented surface. Include `<version>` before the feature-test check, or include `<span>` under a C++20/library-availability guard before testing `__cpp_lib_span`.
