I'll audit this chunk carefully. Let me walk each surface for correctness, RT-safety, fail-silent fallbacks, and documentation drift.

### Teensy toolchain comment claims automatic max-standard detection that does not exist

Finding-ID: AUDIT-BARRAGE-claude-01
Status:     open
Severity:   medium
Surface:    cmake/toolchains/teensy.cmake:5-8, 26-31

The header comment states "ACFX_TEENSY_CXX_STANDARD **is set to the highest standard that toolchain supports** (>= 17)." This describes an automatic capability-detection mechanism. The actual code contains none: `if(NOT DEFINED ACFX_TEENSY_CXX_STANDARD) set(ACFX_TEENSY_CXX_STANDARD 17)` hardcodes a default of 17, and the inline comment at line 27-29 contradicts the header — "default to C++17 … **Raised here if** the installed toolchain supports more" (i.e. a human/CI must pass `-DACFX_TEENSY_CXX_STANDARD=20` by hand). There is no probe of `arm-none-eabi-g++` anywhere in the file.

This is documentation drift of the same class the round-6 commit ("correct ARM overclaim") was meant to remove, so the shape regressed/persisted. Blast radius: an unattended agent reading this toolchain file to reason about portability will conclude the build auto-selects the maximum supported standard and that C++20 concepts activate automatically when the toolchain supports them. It will not — the duck-typed C++17 path is what actually compiles unless someone manually overrides the cache var. An agent could "verify" concept-path coverage that never runs. A reasonable fix is to delete the "is set to the highest standard that toolchain supports" clause and state plainly: default 17, override via the cache variable.

### prepare() silently truncates numChannels to kMaxChannels with no diagnostic (fail-silent fallback)

Finding-ID: AUDIT-BARRAGE-claude-02
Status:     open
Severity:   medium
Surface:    core/effects/svf/svf-effect.h:103-109, 117-126

`prepare()` does `numChannels_ = ctx.numChannels < kMaxChannels ? ctx.numChannels : kMaxChannels;` (line 105) and `process()` does `channels = io.numChannels() < numChannels_ ? io.numChannels() : numChannels_;` (line 118). If an adapter prepares with `ctx.numChannels > 8` (e.g. a 10- or 16-channel host bus) the effect silently caps at 8 and the surplus channels are passed through **completely dry** — unfiltered — with zero diagnostic. The filter is applied to channels 0–7 and channels 8+ bypass it entirely.

This is precisely the "fallback that hides a failure mode" the project guidelines and CLAUDE.md ("raise descriptive errors for missing functionality instead") prohibit. The hot path is `noexcept` so it cannot throw, but `prepare()` is the non-RT lifecycle entry point where a contract violation should be surfaced (assert in debug, or a documented precondition that the adapter must honor). Blast radius: a downstream adapter author wiring a multichannel bus gets a partially-processed signal that sounds plausible on a quick stereo check and ships a silent per-channel bug; an agent verifying "the SVF processes the prepared block" sees no error and concludes correctness. At minimum a debug `assert(ctx.numChannels <= kMaxChannels)` plus a doc-comment precondition on `prepare()` (mirroring the existing thread-ownership note) would convert the silent truncation into a caught contract breach.

### normalize() is not NaN-hardened, breaking the file's own stated NaN-safety invariant

Finding-ID: AUDIT-BARRAGE-claude-03
Status:     open
Severity:   low
Surface:    core/dsp/parameter.h:88-112 (vs. detail::clamp01 at 53-58)

`denormalize()` deliberately routes its `norm` input through `detail::clamp01`, whose comment block (lines 49-56) establishes NaN-neutralization as an explicit, load-bearing invariant: "NaN must map to 0, not pass through … a guard that fails open." Its inverse, `normalize()`, applies no such guard — it does `plain = std::clamp(plain, d.min, d.max)` (line 105), and `std::clamp(NaN, lo, hi)` returns NaN unchanged (both internal comparisons are false). So `normalize(d, NaN)` returns NaN for both linear (`(NaN-min)/(max-min)`) and logarithmic (`log(NaN/min)/…`) branches.

The blast radius is bounded — `normalize()` runs off the audio thread (adapter/UI/host-metadata direction) and any NaN it returns is re-clamped at the `denormalize` boundary if fed back via `setParameter`, so filter state is not poisoned. That containment is why this is low, not higher. But the file declares NaN-safety as an invariant for this mapping pair and then satisfies it on only one side; a corrupted-preset plain value would surface a NaN in host/UI parameter display. The symmetric fix is to neutralize non-finite `plain` the same way (e.g. `if (!(plain == plain)) plain = d.min;` before clamping) so the documented invariant holds for both directions.

### clampedCutoff() hardcodes 20.0f, silently duplicating the descriptor minimum

Finding-ID: AUDIT-BARRAGE-claude-04
Status:     open
Severity:   low
Surface:    core/effects/svf/svf-effect.h:153-161

`clampedCutoff()` lower-bounds the cutoff with a bare `if (f < 20.0f) f = 20.0f;` (lines 157-158). That `20.0f` is the same value as `kParams[kCutoff].min` (line 76: `20.0f, 20000.0f`), but the two are unconnected literals. The single-source-of-truth claim for the parameter table (SC-006, "the single source of parameter truth") is undercut: if the cutoff descriptor min is ever retuned (say to 30 Hz), the descriptor changes but the runtime clamp floor silently stays at 20, and the two definitions of "minimum cutoff" diverge.

Blast radius is low — both values agree today and the consequence of drift is a slightly-wrong clamp floor, not a crash or NaN. But it is a magic number that should reference its source: `kParams[kCutoff].min` is `constexpr` and usable directly here, eliminating the duplicate. The upper bound `sampleRate_ * 0.32f` (line 154) is the legitimately-derived DaisySP `< sr/3` constraint and is fine as-is; only the lower literal duplicates declared data.

### teensy.cmake sets CMAKE_CXX_STANDARD without CMAKE_CXX_STANDARD_REQUIRED

Finding-ID: AUDIT-BARRAGE-claude-05
Status:     open
Severity:   low
Surface:    cmake/toolchains/teensy.cmake:30-31

`set(CMAKE_CXX_STANDARD ${ACFX_TEENSY_CXX_STANDARD})` is set without `set(CMAKE_CXX_STANDARD_REQUIRED ON)`. By CMake's documented default (`CMAKE_CXX_STANDARD_REQUIRED` OFF), a requested standard the compiler cannot honor is treated as a "decay-to-latest-supported" request rather than a hard error. Combined with finding-01 (a human may pass `-DACFX_TEENSY_CXX_STANDARD=20`), this means an over-request silently decays instead of failing loudly.

The blast radius is limited because the C++17 source paths use C++17-only facilities (`std::void_t` in effect.h, `<span>` guarding in span.h), so a decay *below* 17 would surface as a compile error rather than a silent misbuild — that bound is why this is low. The downside that actually bites is a `=20` request silently decaying to 17, which quietly disables the `__cpp_concepts` path with no signal, so an operator believes concepts were exercised on the embedded target when only the duck-typed shape compiled. Adding `set(CMAKE_CXX_STANDARD_REQUIRED ON)` makes the requested standard a hard requirement and removes the silent-decay ambiguity.

---

I checked the atomic parameter-handoff protocol (`setParameter` relaxed-bits-then-release-dirty paired with `applyPending`'s acquire-exchange-then-relaxed-bits) and it is a correct, lock-free, RT-safe single-consumer handoff — the release/acquire on `pendingDirty_` properly publishes the sequenced-before relaxed `pendingBits_` write, last-writer-wins, no torn or lost-beyond-coalesced updates. The `clamp01` NaN logic, the discrete-mode quantization/round-trip, the `isValidDescriptor` compile-time guard and `static_assert` lambda, and the `is_always_lock_free` guard are all sound. The five findings above are the surfaces worth triaging.