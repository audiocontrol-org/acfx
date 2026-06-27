### No C++17 compatibility path exercised by the automated test suite

Finding-ID: AUDIT-BARRAGE-claude-01
Status:     open
Severity:   medium
Surface:    tests/CMakeLists.txt:17

`tests/CMakeLists.txt` forces the test binary to compile at C++20 with `target_compile_features(acfx_core_tests PRIVATE cxx_std_20)`. The `Effect` concept in `core/dsp/effect.h` and the `SvfEffect` implementation both have a C++17 fallback path guarded by `__cpp_concepts`. Because the test binary always builds at C++20, the C++17 duck-typed fallback is never compiled under `ctest --preset test`. If a future commit adds a C++20-only construct to `core/effects/svf/svf-effect.h` without the `__cpp_concepts` guard, CI will stay green (the tests build at C++20) while the MCU adapters — which may encounter a C++17-only toolchain — would silently break.

T035 (now marked `[X]`) acknowledges that the C++17 host compile was verified as a one-time check, but that check is not part of the recurring CI pipeline. The cross-standard portability claim is therefore not continuously validated. A reasonable fix is a second CMake test target (or a CMake preset variant) that builds the same test sources at C++17, so the `__cpp_concepts`-guarded fallback path gets compiled on every CI run.

---

### Test case title claims "process allocates nothing" but body also measures `setParameter`

Finding-ID: AUDIT-BARRAGE-claude-02
Status:     open
Severity:   low
Surface:    tests/core/no-allocation-test.cpp:21,40

The first test case is named `"SvfEffect::process allocates nothing across block sizes"` (line 21). However, the measured region (lines 37–41) also calls `fx.setParameter(ParamId{SvfEffect::kCutoff}, ...)` inside every loop iteration. If `setParameter` were the source of an allocation, the test would fail with a diagnostic message stating that `process` allocated, sending the debugging effort in the wrong direction. The comment on line 41 — "parameter changes on the audio thread must also be allocation-free" — confirms that the intent is to cover both paths, so the title should say so: "SvfEffect::process and setParameter allocate nothing across block sizes". This is a name/body mismatch that makes the test less useful as diagnostic output.

---

### `EffectNode` parameter-update path not covered by the no-allocation test

Finding-ID: AUDIT-BARRAGE-claude-03
Status:     open
Severity:   medium
Surface:    tests/core/no-allocation-test.cpp:45-58

The second test case (`EffectNode<SvfEffect>::processBlock allocates nothing`, lines 45–58) only calls `node.processBlock(block)` in its measured loop (line 55). It does not exercise the `setParameter` path through `EffectNode`. The comment in the first test case explicitly states that "parameter changes on the audio thread must also be allocation-free", and the first test validates this for the raw `SvfEffect`. But `EffectNode` is a host boundary wrapper (likely with a virtual dispatch or a lock-free queue for parameter delivery) that could allocate on its own. If `EffectNode::setParameter` or its internal bridging mechanism allocates, this test will not catch it, and the feature's real-time safety claim for the plugin and workbench adapters (which go through `EffectNode`) would be unverified.

---

### T035 and T038 marked `[X]` with scope-narrowed descriptions — completion state is misleading for hardware build

Finding-ID: AUDIT-BARRAGE-claude-04
Status:     open
Severity:   medium
Surface:    specs/svf-vertical-slice/tasks.md:133-150 (T035), 155-158 (T038)

T035 and T038 were originally specified to verify the actual cross-platform MCU build (running the `daisy` and `teensy` CMake presets, linking firmware ELFs). Both are now marked `[X]` with rewritten descriptions that explicitly acknowledge the on-target `arm-none-eabi` compile was NOT performed because the installed toolchain is C-only (no `libstdc++`). The scope-narrowing is transparent within the task bodies, but the `[X]` completion markers and the US3 checkpoint heading ("US3 standard-portability verified") may mislead a reader — human or automated — performing a quick status scan. The original checkpoint read "the cross-platform claim is real (build + link on both MCUs)"; the new one is "standard-portability verified" — a materially weaker statement. An agent reading task completion state without parsing every body paragraph would conclude US3 is fully done when the embedded compile, link, and flash have not been attempted.

The Manual Acceptance section does call out the hardware checkpoint explicitly as pending (`☐ US3 / Scenario D`), but that section uses a different symbol (`☐` U+2610 ballot box rather than `- [ ]` GitHub task-list syntax) and appears below the completed task list, making it easy to miss. A reasonable fix is to leave T035 / T038 with a partial-completion marker (e.g., `[~]` or a note in the task body reading "automated portion complete; hardware acceptance pending") so the delta between "build verified" and "cross-platform claim real" is visible at task-list-scan granularity.

---

### Manual acceptance items use non-standard `☐` symbol — not trackable as GitHub task list items

Finding-ID: AUDIT-BARRAGE-claude-05
Status:     open
Severity:   low
Surface:    specs/svf-vertical-slice/tasks.md (Manual acceptance section, lines ~165-175 approximately)

The three Manual Acceptance checkpoints use `☐` (U+2610 BALLOT BOX) rather than `- [ ]` GitHub Flavored Markdown task-list syntax. GitHub renders `- [ ]` as interactive checkboxes and counts them in the issue/PR task-progress indicator. The `☐` character renders as a Unicode glyph with no interactivity and is excluded from GitHub's task tracking. If this spec lives in a GitHub issue or PR, the three pending hardware acceptance items will be invisible to the progress counter, and there is no mechanism to check them off programmatically. The fix is to use `- [ ]` for all pending items, consistent with the rest of the document.