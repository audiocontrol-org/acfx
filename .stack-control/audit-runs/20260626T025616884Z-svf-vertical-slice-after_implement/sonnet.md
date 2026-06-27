### Checkpoint commentary mislabels T027/T031 as the manual-acceptance tasks

Finding-ID: AUDIT-BARRAGE-claude-01
Status:     open
Severity:   medium
Surface:    specs/svf-vertical-slice/tasks.md (US1 checkpoint block, ~line 95; US2 checkpoint block, ~line 114)

T027 was redefined in this diff from "Run quickstart Scenario B end-to-end" to "Build + automated-verify the workbench." It is now the completed automated build check, marked `[X]`. However, the US1 checkpoint text still reads: "the live sketch-and-hear run … is the operator's manual acceptance **(T027)**, pending a machine with audio I/O." An identical pattern appears in the US2 checkpoint: "in-DAW … are the operator's manual acceptance **(T031)**."

The actual manual acceptance items are the `☐` bullets in the newly added "Manual acceptance" section, not T027/T031. Because T027 and T031 are already `[X]`, a reader tracking task completion by ID would see both of those IDs as closed and incorrectly conclude that the manual acceptance runs have been performed. A downstream agent building from this spec that checks task status by ID would make the same error. The Manual acceptance section's `☐` checkboxes are correct, but they are not cross-referenced from the checkpoint text — so the checkpoint text is the authoritative-looking anchor and it contradicts the section below.

Fix: remove the stale `(T027)` and `(T031)` parentheticals from the two checkpoint blocks and replace each with a pointer to the Manual acceptance section, e.g. "see Manual acceptance → US1 / Scenario B below."

---

### `reset()` is absent from the allocation-free invariant measurement window

Finding-ID: AUDIT-BARRAGE-claude-02
Status:     open
Severity:   medium
Surface:    tests/core/no-allocation-test.cpp:29–38 (measurement window of first TEST_CASE)

The no-allocation test measures `process()` and `setParameter()` but never calls `SvfEffect::reset()` inside the `AllocationSentinel` measurement window. `reset()` is a natural RT-callback candidate: a DAW host typically calls a plugin's reset when the transport stops or loops, and the Daisy/Teensy adapters would do the same on audio restart. If `reset()` ever touches the heap — for example by re-initialising an internal buffer through a path that isn't plain assignment — this test suite will not detect it.

The task description for T018 scopes the invariant to `process()` specifically ("no-heap-allocation-in-`process()`"), so this is not a task-scope violation, but it leaves a gap in RT safety coverage that the test name ("SvfEffect::process allocates nothing across block sizes") and the surrounding comment do not acknowledge. Because the gap is silent, a future developer who adds a reset-internal buffer sweep won't see any test go red.

Fix: add `fx.reset()` inside the measurement loop (or in a separate TEST_CASE) and capture its allocation count independently before any CHECK fires.

---

### T027 "controls auto-generate" claim overstates what automated build verification confirms

Finding-ID: AUDIT-BARRAGE-claude-03
Status:     open
Severity:   medium
Surface:    specs/svf-vertical-slice/tasks.md, T027 task body (~line 84)

T027 now reads: "controls auto-generate from `SvfEffect::parameters()`" as part of what the automated build step has verified. Control auto-generation is a runtime UI behaviour: the `parameter-view` code is exercised at application startup when JUCE constructs and lays out the component tree. Compilation + link only proves the code path exists and type-checks; it does not prove controls actually appear or respond correctly at runtime.

An agent reading T027 as a completed, build-verified task would understand "controls auto-generate from `SvfEffect::parameters()`" as a runtime-verified claim — especially because the sentence uses present tense ("auto-generate") with no qualifier like "compiles" or "code path exists." The manual acceptance section correctly defers the interactive UI run, but T027's phrasing creates a gap between what the task claims to have proven and what automated build verification can actually show.

Blast-radius: any downstream spec or audit that builds on T027's "verified" status will inherit a subtly wrong picture of what has been validated for the parameter-view surface.

Fix: qualify the phrasing to make the build-only scope explicit, e.g. "the parameter-view code that auto-generates controls from `SvfEffect::parameters()` compiles and links as part of the runnable app binary — runtime rendering is the manual acceptance checkpoint."

---

### `EffectNode` allocation test omits `setParameter` coverage

Finding-ID: AUDIT-BARRAGE-claude-04
Status:     open
Severity:   low
Surface:    tests/core/no-allocation-test.cpp:43–57 (second TEST_CASE)

The first test measures both `process()` and `setParameter()` on `SvfEffect` directly. The second test, covering `EffectNode<SvfEffect>`, only calls `processBlock()` and never calls any parameter-setter on the node. If `EffectNode` wraps `setParameter` with an indirection (e.g. a forwarding call through the `ProcessorNode` interface or an intermediate queue flush) that allocates, the second test will not catch it. The asymmetry is not commented on, so a reader comparing the two tests has no cue that the coverage is intentionally narrower.

Fix: either add `node.setParameter(...)` inside the second test's measurement loop to match the first, or add a comment explaining why `EffectNode`'s parameter path is not separately measured here (e.g. "setParameter on EffectNode delegates directly to the underlying effect — covered by TEST_CASE 1").

---

### Test suite forces C++20; C++17 concept-degradation path left behaviorally untested

Finding-ID: AUDIT-BARRAGE-claude-05
Status:     open
Severity:   low
Surface:    tests/CMakeLists.txt:17 (`target_compile_features(acfx_core_tests PRIVATE cxx_std_20)`)

The entire test binary is compiled at C++20. The spec emphasises a C++17 degradation path for the `Effect` concept (duck-typed fallback when `__cpp_concepts` is absent). Compilation of the C++17 path is exercised by the portability check script (`scripts/check-portability.sh`, referenced in the diff context), but **behaviour** under C++17 is not exercised: no test is built with `cxx_std_17` to run the actual test assertions through the degraded concept path. If the duck-typed fallback has a subtly different instantiation pattern (e.g., different template resolution), a behavioural divergence would go undetected.

This is low severity because C++17 vs C++20 behavioural divergence in the `Effect` duck-typed path is unlikely given the type-erasure design, and the portability script does provide compilation coverage. Worth noting for any future conformance-test extension.