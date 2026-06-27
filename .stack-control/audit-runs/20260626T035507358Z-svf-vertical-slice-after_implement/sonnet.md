### `CHECK_MESSAGE` receives more than two positional macro arguments

Finding-ID: AUDIT-BARRAGE-claude-01
Status:     open
Severity:   medium
Surface:    tests/core/no-allocation-test.cpp:39, tests/core/no-allocation-test.cpp:58

`CHECK_MESSAGE` is invoked with comma-separated positional arguments after the condition at both call sites:

```cpp
CHECK_MESSAGE(allocations == 0, "block size ", blockSize, " allocated ", allocations);
// line 39

CHECK_MESSAGE(allocations == 0, "EffectNode processBlock allocated ", allocations);
// line 58 — three args here, consistent with above
```

In the C preprocessor, each comma is an argument separator unless the macro is declared variadic (`CHECK_MESSAGE(cond, ...)`). In doctest 2.x, `DOCTEST_CHECK_MESSAGE` is defined as a two-argument macro `(cond, msg)`. Passing five comma-separated tokens as arguments 2–5 would produce a "too many arguments" preprocessor error and prevent the test binary from compiling. If the `test` preset CI build exercises these new test sources, CI goes red. If it doesn't (the new file was added after the checkpoint run), the regression is silent. The typical idiom in doctest for multi-value messages uses `INFO()` with `<<` chaining before the `CHECK`, or a single string constructed with `std::string` before the assertion. Either would fix it without needing a variadic macro.

---

### `EffectNode` no-allocation test does not cover `setParameter`

Finding-ID: AUDIT-BARRAGE-claude-02
Status:     open
Severity:   medium
Surface:    tests/core/no-allocation-test.cpp:46–58

The `SvfEffect` test case (lines 22–42) exercises both `process()` and `setParameter()` inside the sentinel window, with the comment "parameter changes on the audio thread must also be allocation-free." The `EffectNode<SvfEffect>` test case (lines 46–58) only calls `processBlock(block)` inside the sentinel window; `setParameter` (or whatever delegation method `EffectNode` exposes) is never called while the sentinel is active. The host boundary (`EffectNode`) wraps the inner effect; if its delegation path adds a lock, std::function call, or internal parameter queue that allocates, this test would pass silently while a real-time thread that does param changes through `EffectNode` would allocate on the audio thread. This is a gap in coverage for the invariant the test is named to verify (FR-014). The fix is to add `setParameter` calls (or equivalent) inside the `EffectNode` measurement loop, mirroring the pattern already used for the bare-effect test.

---

### Task `[X]` marks were applied after acceptance criteria were quietly narrowed

Finding-ID: AUDIT-BARRAGE-claude-03
Status:     open
Severity:   medium
Surface:    specs/svf-vertical-slice/tasks.md (lines 89–107, 112–125, 131–154, and the new Manual acceptance section)

T027, T031, T035, and T038 were each marked `[X]` (complete), but the diffs show their task body was rewritten to a lower bar before the mark was applied:

- T027 originally: "Run quickstart Scenario B end-to-end and confirm all US1 acceptance scenarios" (live audio, MIDI, A/B toggle). Rewritten to: compile + link only.
- T031 originally: "Run quickstart Scenario C and confirm all US2 acceptance scenarios." Rewritten to: build + link of bundles only.
- T035 originally: "build `daisy` + `teensy` presets, confirm linked artifacts." Rewritten to: host-side dual-standard compile only; the actual ARM toolchain and ELF link are deferred.
- T038 originally: "Confirm the one-source-many-targets invariant." Rewritten to: host compile only.

The deferral is disclosed via the new Manual acceptance section with `☐` bullets, and the reasoning (no audio I/O, no full ARM toolchain) is honest. But the structural consequence is that the spec's task list, read as a whole by any downstream consumer (human or AI), shows a fully-green `[X]` surface while the original end-to-end acceptance criteria have not been met. The Manual acceptance section uses a non-standard `☐` that is not machine-trackable and appears outside the `- [ ]` / `- [X]` convention used everywhere else. Any tool or agent that queries the tasks for done/not-done would report the feature complete. A firmer approach is to leave the original tasks with their original bodies unchecked and note the build-only sub-result inside the body, reserving the `[X]` for when the operator checkpoint is actually run.

---

### C++17 runtime behavior never exercised — only compile-checked

Finding-ID: AUDIT-BARRAGE-claude-04
Status:     open
Severity:   low
Surface:    tests/CMakeLists.txt:17

```cmake
target_compile_features(acfx_core_tests PRIVATE cxx_std_20)
```

The entire test suite is compiled at C++20. The `Effect` concept in `core/dsp/effect.h` has a `__cpp_concepts`-guarded C++17 duck-typed fallback path, and T034/T035 specifically concern whether `SvfEffect` compiles under that degraded path. The portability script and CI appear to perform a separate host-side C++17 compile to verify the degraded path compiles — but the test binary (`acfx_core_tests`) is never built at C++17. If the C++17 fallback path has a runtime difference (e.g., a constexpr annotation that is only valid under C++20 semantics, or an `is_always_lock_free` static_assert that behaves differently under C++17), no test run would catch it. A low-cost fix is to add a second `ctest` target (or a separate CMake `add_test`) that compiles the same test sources at C++17 and runs them — this would close the gap between "it compiled under C++17" and "it behaves identically under C++17."

---

### Manual acceptance `☐` bullets use a non-standard checkbox format

Finding-ID: AUDIT-BARRAGE-claude-05
Status:     open
Severity:   informational
Surface:    specs/svf-vertical-slice/tasks.md (Manual acceptance section, ~line 140–153)

The new "Manual acceptance" section uses Unicode `☐` characters rather than the `- [ ]` GFM task-list syntax used throughout the rest of tasks.md. `☐` renders as a checkbox in many environments but is not a GFM task-list item — it is not interactive in GitHub's UI, is not togglable, and would not be parsed by any tool that queries the document for open/closed tasks. This means operator checkpoints tracked with `☐` are invisible to any automation that scans for `[ ]`/`[X]` task state. If the intent is for these to be tracked with the same tooling as the rest of the task list, they should use `- [ ]` syntax. If they are intentionally outside automated tracking, that decision should be stated explicitly in the section header so a reader doesn't assume the `☐` carries the same weight as an unchecked `[ ]`.