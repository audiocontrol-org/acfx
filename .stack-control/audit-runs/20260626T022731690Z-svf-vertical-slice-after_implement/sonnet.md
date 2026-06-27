### C++17 duck-typed `Effect` fallback path never exercised by `ctest --preset test`

Finding-ID: AUDIT-BARRAGE-claude-01
Status:     open
Severity:   medium
Surface:    tests/CMakeLists.txt:17

`tests/CMakeLists.txt` pins the test binary to C++20 (`target_compile_features(acfx_core_tests PRIVATE cxx_std_20)`). The `Effect` concept in `core/dsp/effect.h` has an `__cpp_concepts`-guarded C++17 duck-typed fallback (spec T011). Because the test binary always compiles at C++20, the fallback code path is never compiled or run by `ctest --preset test` or CI. A regression that breaks the C++17 fallback (a missing duck-typed method, an incompatible defaulted template, a C++20-only construct leaking into a `#else` branch) will not be caught by any automated test. The MCU cross-build (T034/T035) provides the only compile-time guard for C++17, but that path is absent from the CI test run and was itself marked "compile-verify only" (no firmware link or test execution). The blast-radius: a CI-green C++17 regression could silently ship and only surface when building for Daisy or Teensy.

A minimal fix is a second `add_executable(acfx_core_tests_cxx17 ...)` target in `tests/CMakeLists.txt` using `target_compile_features(... PRIVATE cxx_std_17)` with the same sources, registered to a separate CTest label, and included in the CI matrix under the `test` preset. Alternatively, an explicit note in `CONTRIBUTING.md` acknowledging the gap and committing to MCU-CI coverage as the only C++17 guard.

---

### `EffectNode<SvfEffect>` allocation test omits the node's `setParameter` path

Finding-ID: AUDIT-BARRAGE-claude-02
Status:     open
Severity:   low
Surface:    tests/core/no-allocation-test.cpp:43–56

The second test case (`EffectNode<SvfEffect>::processBlock allocates nothing`) exercises only `node.processBlock(block)`. It never calls a `setParameter`-equivalent through the `EffectNode` boundary. If `EffectNode` (spec T019: "≤1 vcall/block") has a thread-safe parameter-delivery path — a lock-free queue entry, an atomic swap, or any forwarding mechanism — the allocation behaviour of that path is untested. The first test case (`SvfEffect::process`) explicitly tests `setParameter` under the comment "parameter changes on the audio thread must also be allocation-free," making the omission in the second case visible by contrast. Blast-radius: if the node's parameter path allocates on the audio thread, FR-014 is violated and the defect is invisible to the test suite and CI.

The fix is a single additional block in the second test case mirroring the first: call the node's parameter-setting method inside the measured loop after `processBlock`, and assert zero allocations.

---

### Manual-acceptance checkboxes use `☐` (Unicode) not GitHub task-list syntax

Finding-ID: AUDIT-BARRAGE-claude-03
Status:     open
Severity:   low
Surface:    specs/svf-vertical-slice/tasks.md (new "Manual acceptance" section, lines ~163–175)

The new "Manual acceptance" section uses the Unicode character `☐` as a checkbox. GitHub's task-list rendering only recognises `- [ ]` / `- [x]` (GFM spec §task-list-items) as interactive, trackable checkboxes. The `☐` bullets render as plain-text bullet points in all GitHub views (PR, issue, wiki); the item count will not appear in the sidebar, and the boxes cannot be ticked by clicking. All other tasks in the file use `- [X]` or `- [ ]` correctly. Blast-radius: the operator may not notice the pending acceptance items are untracked by GitHub's completion mechanism, and a future summary of "open checkboxes" (by tooling or by visual scan) will undercount the outstanding work.

---

### T027 / T031 / T035 definitions narrowed post-hoc; interactive acceptance remains unverified

Finding-ID: AUDIT-BARRAGE-claude-04
Status:     open
Severity:   informational
Surface:    specs/svf-vertical-slice/tasks.md (T027, T031, T035 hunks)

Each of T027 ("run Scenario B end-to-end and confirm all US1 acceptance scenarios"), T031 ("run Scenario C and confirm all US2 acceptance scenarios"), and T035 ("run Scenario D: build + link artifacts…") was redefined in this commit to match what was actually accomplished (build + automated verification) rather than what the original task required (interactive live run / DAW instantiation / flashed firmware). The honest "Manual acceptance" section is a positive addition, but the effect of the rewrite is that all tasks are now marked `[X]` complete while the original acceptance criteria for three of them remain unmet and are captured only as freeform `☐` operator checkpoints. A downstream consumer reading the task list as "all [X] means the feature is verified" would draw the wrong conclusion.

This is documented in good faith and the pending items are visible in context, but the pattern of retroactively narrowing task definitions to match execution-scope is worth noting: the feature's user-facing behaviour (audio sweep, DAW parity, flashed hardware) has not been verified end-to-end by any automated or manual run captured in this commit.