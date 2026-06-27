### Acceptance tasks marked `[X]` while containing deferral phrase "deferred to graduation"

Finding-ID: AUDIT-BARRAGE-claude-01
Status:     open
Severity:   medium
Surface:    specs/workbench-audio-config/tasks.md (lines touched at T008, T011, T014, T016, T019 in diff)

The project guidelines hard-ban deferral phrases and require surfacing any found in the diff: "If you spot a deferral phrase IN the diff, surface it as a finding." The phrase "deferred to graduation" appears five times across T008, T011, T014, T016, and T019. More critically, each affected task is simultaneously marked `[X]` (complete). T008, T011, T014, and T016 were originally acceptance tasks of the form "Run quickstart Scenario X and confirm the YZ acceptance scenarios" — none of those scenarios have been executed; only the implementation tasks that precede them were done. T019 is a two-part task (Scenario A + Scenario F); Scenario F is deferred but the whole task is marked `[X]`. The blast radius: anyone checking task-list status to judge feature readiness will conclude the feature's acceptance criteria are satisfied, when five scenarios that gate user-story acceptance have not been run. This is the class of slip that causes unverified behavior to reach graduation with no subsequent checkpoint.

A clean disposition would either split each acceptance item into its own task and leave it `[ ]`, or use an explicit marker (e.g., `[~]` / `[P]`) that the project defines as "implementation done, manual acceptance pending," with a corresponding graduation-gate checklist somewhere that carries the open items forward. The current state is ambiguous to any reader who hasn't read this audit.

---

### Test suite does not probe `parse("mode=file")` — missing path key vs. empty path value

Finding-ID: AUDIT-BARRAGE-claude-02
Status:     open
Severity:   medium
Surface:    tests/core/workbench-settings-test.cpp:40–50

The test "parse never returns half-populated file mode" (lines ~40–50) exercises the case `parse(serialize(SourceConfig{file, ""}))` — serializing a file-mode config with an empty path, then parsing the result. But this probes the round-trip of an already-normalized representation; it does not probe a string where the path *key is absent entirely*. The serialized form is almost certainly `mode=file\npath=<value>` (inferred from the `parse("\n\n")` → safe-default test and the `mode=banana` key-value probe). A corrupt or truncated settings file could produce `mode=file\n` with no `path=` line at all — a distinct input from `mode=file\npath=`.

If `parse` branches on "found mode key, did not find path key" differently from "found mode key, found path key with empty value," the missing-key case is untested and could return `SourceConfig{file, ""}` — the broken empty-path file state that FR-009 and the task spec explicitly forbid. The blast radius is a settings file that is partially written (disk-full, crash mid-flush, or upgrade from a hypothetical older format that didn't persist `path=`) causing the workbench to launch into an invalid source state. Adding `CHECK(parse("mode=file") == def)` and `CHECK(parse("mode=file\n") == def)` would close this gap with zero implementation change required.

---

### Paths with embedded newlines are not tested; serialization format likely newline-delimited

Finding-ID: AUDIT-BARRAGE-claude-03
Status:     open
Severity:   low
Surface:    tests/core/workbench-settings-test.cpp:29–37

The unicode/spaces round-trip test (lines ~29–37) covers spaces and multibyte unicode characters, but not `\n` in a path. On macOS and Linux, filenames may legally contain newline bytes. If the serialization format is newline-delimited (strongly implied by `parse("\n\n")` → safe default), a path like `/Users/me/my\nloop.wav` would corrupt the serialized form: the embedded newline would be read as a field separator, splitting the path across two lines. The parser would then either return a garbage file path or — if the residual fragment doesn't match a known key — fall back to the safe default, silently discarding a user-selected file. The fix is one additional path entry in the loop at line ~32: `std::string("/tmp/two\nlines.wav")`. If `serialize`/`parse` don't handle it cleanly, the implementation needs escaping at that boundary.

---

### No CMake-level enforcement of the JUCE-free boundary on the test target's include path

Finding-ID: AUDIT-BARRAGE-claude-04
Status:     open
Severity:   informational
Surface:    tests/CMakeLists.txt:19–21

`target_include_directories(acfx_core_tests PRIVATE "${CMAKE_SOURCE_DIR}/adapters/workbench")` places the full adapter directory on the JUCE-free test target's include path. The JUCE-free invariant for `workbench-settings.h` (and its `.cpp`) is enforced only by convention: if any future change adds a JUCE type to `workbench-settings.h` — directly or via a transitively included header — the test target will silently acquire a dependency on JUCE headers it cannot link. The failure mode is a cryptic compile error at an unexpected location rather than a clear boundary-violation message. No immediate defect exists today, but the invariant is load-bearing (it is the reason the serde seam can be tested at all without a JUCE build), and there is currently no mechanism that enforces it. A `INTERFACE_COMPILE_DEFINITIONS` constraint or a static-assert check in the header would make a future violation fail loudly at the right layer.