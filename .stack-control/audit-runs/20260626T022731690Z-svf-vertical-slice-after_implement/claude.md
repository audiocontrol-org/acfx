### T035 claims Cortex-M7 C++ compile-verification with a toolchain it simultaneously describes as C-only

Finding-ID: AUDIT-BARRAGE-claude-01
Status:     open
Severity:   high
Surface:    specs/svf-vertical-slice/tasks.md (T035 task line + Phase 5 checkpoint)

T035 (now marked `[X]`) asserts two things in one breath: "the identical `core/effects/svf` cross-compiles for Cortex-M7 at both C++17 (concept degraded) and C++20 (named concept), the lock-free `is_always_lock_free` static_assert holds on-target" — and then, as the reason the *link* is deferred, "blocked in this environment by a C-only `arm-none-eabi-gcc` with no libstdc++." These contradict each other. A `static_assert(... is_always_lock_free ...)` is a *compile-time* gate inside a C++ translation unit; for it to "hold on-target" the C++ source must have been compiled by an ARM C++ frontend with `<atomic>` available. A genuinely "C-only `arm-none-eabi-gcc` with no libstdc++" cannot compile `core/effects/svf` at all — `#include <atomic>`/`<cstdint>` fail at the compile step, not the link step. So either the compile happened (and the toolchain is not C-only) or it didn't (and the "compile-verifies at C++17 and C++20" + "static_assert holds on-target" claims are unsupported).

Blast radius: an agent or adopter reading the Phase 5 checkpoint ("US3 compile-verified — the identical core cross-compiles for Cortex-M7 at both C++17 and C++20") will treat the MCU portability claim (SC-007, the central thesis of the feature) as machine-proven and build downstream work on it. The artifact's own toolchain description says that verification was impossible here, so the agent inherits an unverified claim presented as verified. A reasonable fix: state precisely *what compiler actually performed the compile-verify* (e.g. a host `clang --target=arm-none-eabi -nostdlib` syntax/semantic check, or a different g++), and if no ARM C++ compile actually ran, downgrade the claim from "compile-verified for Cortex-M7" to "host-compiled with ARM-target flags" or mark it unverified.

### `[X]` on T027/T031/T035 now silently means "build-complete," not "accepted," with no inline marker on the task

Finding-ID: AUDIT-BARRAGE-claude-02
Status:     open
Severity:   medium
Surface:    specs/svf-vertical-slice/tasks.md (T027, T031, T035, Phase checkpoints, new "Manual acceptance" section)

The three acceptance tasks were rewritten in place: T027 went from "Run quickstart Scenario B end-to-end and confirm all US1 acceptance scenarios" to "Build + automated-verify the workbench…"; T031 and T035 likewise. All three are now `[X]`. The actual acceptance (live audio sweep, in-DAW instantiation, on-hardware flash) was relocated to a new "Manual acceptance" section as unchecked `☐` items. The separate section + `☐` glyph is an honest gesture, but inside the Phase task lists a reader sees `[X] T027 … Scenario B build` and `[X] T031 … Scenario C build` with no inline token distinguishing "this checkbox means built+compiled" from the fully-accepted `[X]` tasks (T001–T026, T028–T030, etc.) around them.

Blast radius: an agent that scans tasks.md for completion state (a very common pattern — "are all tasks done? yes, all `[X]`") concludes US1/US2/US3 are accepted and shippable, when the feature's stated independent-test criteria (quickstart Scenarios B/C/D, and any SC-00x in spec.md that demands the interactive run) remain unsatisfied. This is a cross-artifact drift risk: verify the success criteria in `spec.md` are not left materially unmet while their tasks read done. A cleaner shape would tag the narrowed tasks inline (e.g. `[X] (build-only)` or split each into a done `[X]` build subtask + a distinct `☐` acceptance subtask) so completion-scanning cannot over-read them.

### C++17 concept-degradation path has no executing verification — host suite is pinned to C++20

Finding-ID: AUDIT-BARRAGE-claude-03
Status:     open
Severity:   medium
Surface:    tests/CMakeLists.txt:15 (`target_compile_features(acfx_core_tests PRIVATE cxx_std_20)`)

The host test binary is compiled at `cxx_std_20`, so every test (parameter-test, svf-test, no-allocation-test) exercises only the C++20, named-`Effect`-concept code path. The `__cpp_concepts`-guarded C++17 "duck-typed fallback" defined in `core/dsp/effect.h` (T011) is dead code under C++20 and is therefore never compiled or run by the host suite. The only place the C++17 path is claimed to be verified is the MCU cross-build (T034/T035), which — per finding -01 — is described as blocked by a C-only ARM toolchain. The net is that the C++17 fallback the feature explicitly promises to support has no green, executing verification anywhere in this diff.

Blast radius: an adopter who trusts the "C++17 supported (concept degraded)" claim and builds `SvfEffect` into a C++17 target (the Teensy story's stated reason for the fallback existing) can hit a compile or semantic failure in a branch no test ever touched. A reasonable fix is to add a second test target (or a CI matrix axis) that builds the same `tests/core/*` at `cxx_std_17`, forcing the fallback path to compile and pass the same reference vectors — that is the only thing that makes the "one source, both standards" claim load-bearing host-side.

### no-allocation-test failure message uses comma args, not doctest's `<<` form — interpolated values are likely dropped

Finding-ID: AUDIT-BARRAGE-claude-04
Status:     open
Severity:   low
Surface:    tests/core/no-allocation-test.cpp:33, tests/core/no-allocation-test.cpp:57

Both assertions build their message with comma-separated arguments — `CHECK_MESSAGE(allocations == 0, "block size ", blockSize, " allocated ", allocations)` (line 33) and `CHECK_MESSAGE(allocations == 0, "EffectNode processBlock allocated ", allocations)` (line 57, this one is fine, two args). doctest's documented message idiom is stream-chained, e.g. `CHECK_MESSAGE(cond, "block size " << blockSize << " allocated " << allocations)`. With the comma form on line 33, the macro receives the message tokens as `"block size ", blockSize, " allocated ", allocations`; when the macro feeds them past a `<<` they collapse under the comma operator and only the leading string literal `"block size "` is captured — `blockSize` and `allocations` are silently discarded. (The condition itself is unaffected, which is why the suite still passes/builds.)

Blast radius: bounded — the message only renders when an allocation regression actually fires, and at that moment the diagnostic degrades to "block size " with no size and no count, defeating the author's intent to pinpoint *which* block size regressed and by how much. The fix is mechanical: switch to `<<`-chained interpolation. (If, contrary to the above, the local doctest version rejects the extra comma args at compile time, this is instead a build break and should be re-rated blocking — worth a 30-second confirm that the file actually compiles under the pinned doctest tag.)

### no-allocation invariant only flips `kCutoff`; mode/resonance and the EffectNode setParameter path are unmeasured

Finding-ID: AUDIT-BARRAGE-claude-05
Status:     open
Severity:   low
Surface:    tests/core/no-allocation-test.cpp:30-31, tests/core/no-allocation-test.cpp:50-55

The first test's measured loop calls `fx.setParameter(ParamId{SvfEffect::kCutoff}, …)` only — it never drives `resonance` or the discrete `mode` parameter inside the sentinel region. Mode is the one parameter whose change could plausibly re-touch internal SVF state in a way that allocates; the test that claims to prove FR-014 ("no heap allocation in `process()`") for on-audio-thread parameter changes leaves the highest-risk parameter (`mode`) out of the measured window, so a reallocating mode-switch would pass this test falsely. Separately, the `EffectNode<SvfEffect>` case (lines 50-55) calls only `processBlock` and never `setParameter` at all, so the host-boundary parameter path is unmeasured for allocation.

Blast radius: low and forward-looking — today's `SvfEffect` likely doesn't allocate on a mode switch, so this is a coverage gap rather than a live defect, but it weakens the invariant the test advertises: a future change that allocates on mode change ships green. A reasonable fix is to cycle `mode` (and `resonance`) inside the same measured loop, and to exercise `EffectNode::setParameter` in the second case, so the no-allocation contract covers every audio-thread mutation the API exposes.