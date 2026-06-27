I'll audit this chunk directly — the prompt specifies the task and output format. Let me walk the three files (`tasks.md`, `tests/CMakeLists.txt`, `tests/core/no-allocation-test.cpp`) and emit anchored findings.

### no-allocation test verifies the RT-safety invariant only on the cutoff path — the discrete `mode` switch is never exercised

Finding-ID: AUDIT-BARRAGE-claude-01
Status:     open
Severity:   medium
Surface:    tests/core/no-allocation-test.cpp:18-36

This test is the sole automated guardian of FR-014 / Constitution V ("no heap allocation in any `process()` / audio-callback path"). Inside the measured region (lines 27-32) it calls `fx.process(block)` and `fx.setParameter(ParamId{SvfEffect::kCutoff}, …)` only — alternating cutoff between `0.25f`/`0.75f`. It never drives the `resonance` parameter and, more importantly, never drives the discrete `mode` parameter (the `discrete×3` mode-select knob from T017). Mode switching changes the *filter type* (LP/HP/BP) and is exactly the kind of reconfiguration branch most likely to allocate (rebuilding state, swapping a function object, resizing). Because the sentinel only sees the cutoff path, a `setParameter(kMode, …)` that allocates would pass this test green.

Blast radius: an unattended downstream consumer (or CI) reads "no-allocation test green" as "RT-safe, full stop" and ships the effect into an audio-callback context. The most-common path (audio + cutoff automation) is genuinely covered, which is why this is medium rather than high — but the constitution's hardest invariant has an untested branch precisely where the discrete-param code diverges. Fix: extend the in-loop sweep to also call `setParameter` for `kResonance` and `kMode` (cycle through all three mode values) so every `setParameter` branch is inside the measured region.

### `EffectNode<SvfEffect>` allocation test never calls `setParameter` through the host boundary

Finding-ID: AUDIT-BARRAGE-claude-02
Status:     open
Severity:   medium
Surface:    tests/core/no-allocation-test.cpp:39-58

The second test case measures allocations for `node.processBlock(block)` across 100 iterations (lines 50-54) but, unlike the bare-effect case, never calls `setParameter` through the `EffectNode<SvfEffect>` boundary. The `ProcessorNode` contract (T019, ≤1 vcall/block, desktop host boundary) is where the host thread injects parameter changes that must reach the effect allocation-free — if `EffectNode` forwards parameter writes via any mechanism that could allocate (queue growth, deferred apply, virtual-dispatch boxing), this test will not catch it. The bare-`SvfEffect` case exercises `setParameter`; the node case, which is the actual host-facing surface a plugin/workbench drives, does not.

Blast radius: the host boundary is the surface US1/US2 adapters depend on, so a missed allocation there is the one that reaches a real DAW callback. Medium because `processBlock` itself is covered; the gap is the parameter-write path specifically. Fix: add a `node.setParameter(...)` call inside the measured loop (lines 51-53), covering cutoff/resonance/mode through the node.

### `tasks.md`: the pending manual run is labeled "(T027)" while T027 itself is marked `[X]` and explicitly excludes that run

Finding-ID: AUDIT-BARRAGE-claude-03
Status:     open
Severity:   low
Surface:    specs/svf-vertical-slice/tasks.md (T027/T031/T035 rows + their phase checkpoints)

T027 is marked `[X]` and its body states the interactive run "is an operator checkpoint in **Manual acceptance** below, **not part of this automated task**." Yet the US1 checkpoint immediately below it attributes the still-pending manual run to the same ID: "the live sketch-and-hear run … is the operator's manual acceptance **(T027)**, pending a machine with audio I/O." The same double-use of an ID that is simultaneously `[X]`-complete and "pending" repeats for T031 and T035. A reader can resolve this (the `[X]` covers the build half; the manual half is the separate `☐` bullet), which is why it is low — but the overloaded cross-reference momentarily reads as "T027 is both done and not done."

Blast radius: low. An automation that keys off `[X]` status to decide "feature accepted" would treat T027 as fully done and skip the manual checkpoint — which is arguably the intended split, so no wrong build results. Fix: in the checkpoint prose, refer to the manual bullets by their `☐` Scenario name (e.g. "manual acceptance — US1 / Scenario B") rather than re-using the completed task ID "(T027)", so a single ID never denotes both a done and a pending unit.

### `tests/CMakeLists.txt`: silent degradation from per-case to single-binary test registration when the doctest CMake helper is absent

Finding-ID: AUDIT-BARRAGE-claude-04
Status:     open
Severity:   low
Surface:    tests/CMakeLists.txt:20-27

The discovery block keys on `EXISTS "${doctest_SOURCE_DIR}/scripts/cmake/doctest.cmake"`; if `doctest_SOURCE_DIR` is empty/undefined (doctest resolved via `find_package` rather than the CPM fetch that defines that variable) the path becomes effectively bogus, `EXISTS` is false, and the `else()` branch silently registers the whole binary as a single ctest entry. Functionally CI still goes red on any failing assertion, so this is low — but the degradation is invisible: there's no `message(WARNING …)` distinguishing "discovered N cases" from "registered 1 opaque blob," so a regression in dependency resolution would quietly collapse per-case reporting (and the per-case parallelism) with no signal.

Blast radius: low — correctness of the pass/fail signal is preserved; only granularity is lost, and only under a dependency-provisioning change. Fix: emit a `message(STATUS …)` (or `WARNING`) in the `else()` branch so the fallback is observable, and/or assert `doctest_SOURCE_DIR` is set since `dependencies.cmake` is expected to fetch doctest via CPM.

---

I focused on the test-correctness surface (the allocation invariant is the load-bearing one here) and the spec-consistency surface. I did **not** flag the doctest `CHECK_MESSAGE(cond, "…", var, …)` comma-variadic usage (lines 35, 57) — that is valid doctest message syntax, not Catch2's `<<` form. I also did **not** flag the honest build-vs-manual acceptance split itself (the `☐` Manual-acceptance section and the de-escalated checkpoints from "independently shippable" to "build-complete" are an accurate correction of prior overclaims, not a deferral-phrase bug-factory). The `cxx_std_20`-only test target does not contradict the dual-standard claim, since the C++17 compile is verified by the portability script in another chunk.