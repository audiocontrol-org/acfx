I was dispatched to audit chunk `b74f59c0c4fc198b` (tasks.md status flips + the new host test wiring). I walked the four files closely. Findings below.

### T035 closed as [X] while its own body documents the MCU firmware link as blocked — the adjacent Phase 5 checkpoint still claims "build + link on both MCUs"

Finding-ID: AUDIT-BARRAGE-claude-01
Status:     open
Severity:   medium
Surface:    specs/svf-vertical-slice/tasks.md:118-121 (T035 + unchanged Phase 5 checkpoint at line 125)

T035 flips to `[X]` with a body that is candid: "**Blocked / on-board checkpoint:** full firmware ELF link — the installed `arm-none-eabi-gcc` is C-only (ships no libstdc++), so linking + flashing is the proper-toolchain/hardware checkpoint." So the link step did **not** happen. But the Phase 5 checkpoint line immediately below (unchanged context) still reads "**Checkpoint**: US3 done — the cross-platform claim is real (build + link on both MCUs)." The implementer updated the task body to be honest but left the summarizing checkpoint asserting that link succeeded. A reader who trusts the checkbox + checkpoint summary (the two most skimmable signals) concludes MCU linking is verified; only the dense body paragraph corrects that. This is documentation drift: the checkpoint should have been rewritten to "build (compile) on both MCUs; link is the on-hardware checkpoint" when the C-only-toolchain blocker was discovered.

Blast radius: a downstream agent grafting onto the "cross-platform claim is real" line would treat MCU link/flash as proven and skip re-validating it on a real toolchain, shipping an unlinkable firmware path as "done." The body does disclose the blocker in-place, so an attentive reader resolves it — hence medium, not high. A reasonable fix is to edit the checkpoint to match T035's body (compile-verified, link/flash deferred to hardware) so the checkbox, checkpoint, and body all agree.

### The doctest behavioral suite is built only at C++20 (`cxx_std_20`); the C++17 concept-degradation path gets no behavioral test coverage

Finding-ID: AUDIT-BARRAGE-claude-02
Status:     open
Severity:   low
Surface:    tests/CMakeLists.txt:15 (`target_compile_features(acfx_core_tests PRIVATE cxx_std_20)`)

The headline portability claim of this feature is "the identical `SvfEffect` compiles under both C++17 (concept degraded) and C++20 (named concept)." T034/T035 establish that C++17 *compilation* is checked via the Cortex-M7 cross-compile, which is fine for compile-time. But the only thing that actually exercises behavior — parameter math, the no-allocation invariant, SVF response — is `acfx_core_tests`, and it is pinned to `cxx_std_20`. So the parameter normalize/denormalize logic and the no-heap invariant are never run under the C++17 duck-typed fallback. If the `__cpp_concepts`-guarded code in `effect.h`/`svf-effect.h` has a divergent C++17 branch (a different overload resolution, a fallback that silently behaves differently), no host test would catch it — the MCU build proves it compiles, not that it computes the same answer.

Blast radius: a latent C++17-only behavioral divergence would ship undetected because CI is green and the MCU build links nothing testable. Low because the surface area of `#ifdef __cpp_concepts` divergence here is small and compile-coverage exists. A cheap mitigation is a second test target (or a CI matrix leg) building the same `tests/core/*` at `cxx_std_17` so the degraded path runs the same assertions.

### EffectNode no-allocation test omits the `setParameter` path that the bare-effect test deliberately includes

Finding-ID: AUDIT-BARRAGE-claude-03
Status:     open
Severity:   low
Surface:    tests/core/no-allocation-test.cpp:48-57 (EffectNode loop) vs. 27-36 (SvfEffect loop)

The `SvfEffect` case interleaves `fx.setParameter(...)` inside the measured loop, with a comment explicitly justifying it: "parameter changes on the audio thread must also be allocation-free." The `EffectNode<SvfEffect>` case (lines 48-57) measures only `node.processBlock(block)` — it never calls `setParameter` inside the sentinel region. The host boundary (`ProcessorNode`/`EffectNode`) is exactly the layer most likely to introduce a parameter-forwarding mechanism (a queue, a lock-free FIFO, a `std::function`) that could allocate; that is the path this test is best positioned to guard, and it is the one left uncovered.

Blast radius: a future change adding an allocating parameter-dispatch to `EffectNode` would pass this test and violate FR-014 on the host boundary silently. Low because the bare-effect path is covered and `EffectNode` currently forwards directly. The fix is one line: call `node.setParameter(...)` inside the measured EffectNode loop, mirroring the bare-effect case.

### parameter-test re-declares `ParameterDescriptor` via positional aggregate-init, silently brittle to any field reorder in parameter.h

Finding-ID: AUDIT-BARRAGE-claude-04
Status:     open
Severity:   low
Surface:    tests/core/parameter-test.cpp:11-26 (the three `constexpr ParameterDescriptor` literals)

All three descriptors are built with positional brace-init: `{ParamId{1}, "resonance", ParamUnit::none, 0.0f, 1.0f, 0.0f, ParamSkew::linear, ParamKind::continuous, 0}`. The `min`/`max`/`default` triple is three adjacent `float`s and `ParamSkew`/`ParamKind` are two adjacent enums. `parameter.h` (the struct definition) is in a different chunk, so this test is coupled to its exact field order with no compile-time guard. If a later edit reorders, say, `default` ahead of `max`, or swaps skew/kind, several of these symmetric cases (e.g. `linearRes` with min=0/max=1/default=0) would still compile and could still pass while validating the wrong contract — the test would no longer mean what its name claims.

Blast radius: low — it does not break today, but it is a quietly-decaying test whose green status could mask a descriptor-layout regression. Designated initializers (`{.id = ParamId{1}, .min = 0.0f, .max = 1.0f, ...}`) would make the binding explicit and fail loud on a field rename/reorder. Worth doing while the descriptor shape is still young.