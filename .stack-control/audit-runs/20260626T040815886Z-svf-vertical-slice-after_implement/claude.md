### Compile-time descriptor validator omits the `defaultValue ∈ [min,max]` invariant it claims to guarantee

Finding-ID: AUDIT-BARRAGE-claude-01
Status:     open
Severity:   medium
Surface:    core/dsp/parameter.h:18-22 (struct field), core/dsp/parameter.h:30-39 (`isValidDescriptor`), core/effects/svf/svf-effect.h:62-72 (`static_assert`)

`ParameterDescriptor::defaultValue` is documented as "plain units, within [min, max]" (parameter.h:22), and `SvfEffect` wraps the table in a `static_assert` whose message promises "a malformed entry — e.g. a log param with min<=0 — fails compilation, not the audio path" (svf-effect.h:55-58, 62-72). But `isValidDescriptor` (parameter.h:30-39) checks only three things: `max > min`, logarithmic ⇒ `min > 0`, and discrete ⇒ `discreteCount >= 2`. It never checks `min <= defaultValue <= max`. So a default outside the declared range is a malformed descriptor by the struct's own contract, yet it compiles clean.

The blast radius is direct, not theoretical: `prepare()`/the field initializers consume `defaultValue` *raw*, not through `normalize()`/`denormalize()` — `cutoffHz_ = kParams[kCutoff].defaultValue` and `resonance_ = kParams[kResonance].defaultValue` (svf-effect.h:201-203). For cutoff an out-of-range default is later caught by `clampedCutoff()`, but `resonance_` is pushed straight into `setRes(resonance_)` (`applyResonance`, svf-effect.h:175-178) with no clamp. An unattended agent adding a second effect with a default of, say, `1.5` on a `[0,1]` resonance gets no compile error and a silently out-of-range initial coefficient. Fix: add `if (!(d.defaultValue >= d.min && d.defaultValue <= d.max)) return false;` to `isValidDescriptor` so the advertised "build error, not runtime" guarantee actually covers the field the struct says it covers.

### teensy.cmake comment describes dynamic C++-standard detection that the code does not perform

Finding-ID: AUDIT-BARRAGE-claude-02
Status:     open
Severity:   medium
Surface:    cmake/toolchains/teensy.cmake:4-8, 27-32

The header comment states `ACFX_TEENSY_CXX_STANDARD` "is set to the highest standard that toolchain supports (>= 17)" and "verified against the installed Teensy toolchain during implementation," and the inline comment says "Raised here if the installed toolchain supports more." The actual code does none of this: it is a static `if(NOT DEFINED ACFX_TEENSY_CXX_STANDARD) set(ACFX_TEENSY_CXX_STANDARD 17)` (lines 29-31). The only way it is ever "raised" is an external `-D` override; the toolchain never probes `arm-none-eabi-g++`, never queries `__cplusplus`, never negotiates the highest supported standard. The prose claims an active detection/verification mechanism; the implementation is a hardcoded constant.

Blast radius: a consumer or agent reading this file reasonably concludes the build auto-selects the best available standard on whatever toolchain is installed, and may depend on C++20 features being enabled automatically when a newer ARM toolchain is present. They will instead silently stay pinned at C++17 (degraded span polyfill + concept fallback path), with no signal that the "highest supported" selection never ran. Given prior rounds explicitly closed "correct ARM overclaim" and "live-input evidence" findings (commits d183a18, f1dcf84), this is the same overclaim class re-surfacing in a sibling file. Fix: either implement an actual `try_compile`/feature probe that sets the standard, or rewrite the comment to state plainly "hardcoded to C++17; override with `-DACFX_TEENSY_CXX_STANDARD=N`" and drop the "verified against the installed toolchain" / "highest standard that toolchain supports" language.

### `clampedCutoff()` hardcodes the 20 Hz floor instead of reading the descriptor minimum

Finding-ID: AUDIT-BARRAGE-claude-03
Status:     open
Severity:   low
Surface:    core/effects/svf/svf-effect.h:153-161

`clampedCutoff()` clamps with a literal `20.0f` floor (`if (f < 20.0f) f = 20.0f;`). That value silently duplicates `kParams[kCutoff].min` (`20.0f`, svf-effect.h:42). The descriptor is advertised as "the single source of parameter truth (SC-006)" (svf-effect.h:38), but this floor is a second, disconnected source. If the cutoff descriptor's `min` is ever retuned (e.g. to 10 Hz for a sub-bass build), `denormalize` would correctly produce values down to the new min while `clampedCutoff` would keep flooring them at 20 Hz — a quiet behavioral divergence from the declared range with no compile-time link to catch it.

Blast radius is small (one effect, one filter, audible only at the very bottom of the range), hence low — but it is a textbook "configuration that should be data ending up as a magic number." Fix: derive the floor from the descriptor, e.g. `const float lo = kParams[kCutoff].min;` and clamp against that, so the single-source-of-truth claim holds for the lower bound too.

### `normalize()` is not NaN-safe, unlike its `denormalize()` counterpart

Finding-ID: AUDIT-BARRAGE-claude-04
Status:     open
Severity:   low
Surface:    core/dsp/parameter.h:80-112

`denormalize` opens with the deliberately NaN-neutralizing `detail::clamp01` (parameter.h:55-62, 79), whose comment warns at length that a naive clamp "would return NaN unchanged and poison the filter state irrecoverably (a guard that fails open)." Its inverse, `normalize`, has no equivalent guard: for continuous params it does `plain = std::clamp(plain, d.min, d.max)` (parameter.h:108), and `std::clamp(NaN, lo, hi)` returns NaN (every comparison is false). A NaN `plain` therefore flows through to `std::log(NaN)/…` or `(NaN-min)/(max-min)` and returns NaN.

The audio path is ultimately protected because any NaN that round-trips back through `setParameter`→`denormalize` is caught by `clamp01`, which is why this is low and not higher. But the asymmetry contradicts the module's own stated NaN-safety discipline: a UI/host adapter that calls `normalize` to compute a displayed or persisted normalized value will get and store a NaN, with none of the defense the neighboring function is built around. Fix: mirror the `clamp01` intent on the input — e.g. neutralize a non-finite `plain` to `d.min` before the `std::clamp`, so both directions of the mapping fail closed rather than one failing open and one failing silent.