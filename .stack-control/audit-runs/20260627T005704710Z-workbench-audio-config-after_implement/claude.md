I'm acting as the `claude` lane of the audit barrage. I walked the chunk in scope (`tasks.md`, `tests/CMakeLists.txt`, `tests/core/workbench-settings-test.cpp`) carefully. The implementation `.cpp/.h` files are in other chunks (file-lists only), so my findings are anchored to the test contract, the build wiring, and the task-ledger semantics I can actually see.

### Tasks marked `[X]` complete while their bodies say acceptance was never run ("deferred to graduation")

Finding-ID: AUDIT-BARRAGE-claude-01
Status:     open
Severity:   medium
Surface:    specs/workbench-audio-config/tasks.md (T008, T011, T014, T016, T019)

T008, T011, T014, T016 and the Scenario F clause of T019 are all flipped from `[ ]` to `[X]` — the machine-readable "done" signal — while their bodies are simultaneously edited to read *"(manual acceptance — operator-owned, deferred to graduation)"*. The checkbox and the annotation contradict each other: `[X]` says the acceptance scenario passed; the prose says it was never run and is deferred. An unattended downstream agent (or the promote-findings / convergence tooling) keys on the `[X]` checkbox, not the parenthetical, and will conclude every US1–US4 acceptance scenario was validated when none of the interactive ones were. The project hard-constraint explicitly flags "deferred to ..." as a deferral phrase to surface; here it is load-bearing on five tasks at once.

The honest encoding is to leave these tasks `[ ]` (or introduce a distinct `[~]`/"operator-owned" marker the tooling understands) rather than mark them `[X]`. As written, the ledger over-reports completion — the one signal an unattended build trusts. Blast radius: a consumer treats interactive acceptance as passed and ships/graduates on that false premise.

### T019 marks RT-safety Scenario F `[X]` while deferring the only test of a constitutional invariant

Finding-ID: AUDIT-BARRAGE-claude-02
Status:     open
Severity:   high
Surface:    specs/workbench-audio-config/tasks.md (T019)

T019 is marked `[X]` with the note that "Scenario F is interactive manual acceptance (operator-owned, deferred to graduation)." Scenario F is *"RT-safety under ~20× rapid device/source switches — no glitch/stall/crash"* — i.e. the validation of Constitution VI (no heap alloc / no locks in the audio callback path) under exactly the reconfigure-storm this feature introduces (T004's release→configure→prepare lifecycle, `restartAudio()`, source switching). This feature's whole risk surface is RT-safety under rapid reconfigure, and the task that verifies it is checked off without being executed. Unlike the device/file-picker scenarios, RT-safety is a hard non-negotiable invariant, not a UX nicety — a regression here is a real-time fault (audio dropout/stall/crash), not a cosmetic miss.

The memory note for this project ("Govern before manual acceptance — don't defer the govern pass behind manual/DAW/hardware acceptance") is satisfied for the govern pass (this audit), but the RT-safety *behavioral* check has no automated proxy in the diff — `no-allocation-test.cpp` exists in the core test target but I see nothing wiring the workbench reconfigure path into an allocation-sentinel test. Marking T019 `[X]` reads as "RT-safety verified." It was not. A reasonable fix: add an automated reconfigure-storm allocation-sentinel test for the workbench source lifecycle (the sentinel harness already exists at `support/allocation-sentinel.cpp`), and leave the interactive Scenario F unchecked until run.

### Test "parse never returns half-populated file mode" verifies the round-trip, not `parse`

Finding-ID: AUDIT-BARRAGE-claude-03
Status:     open
Severity:   medium
Surface:    tests/core/workbench-settings-test.cpp:44-50

The test case named *"parse never returns half-populated file mode (file token, empty path -> live)"* asserts on `parse(serialize(SourceConfig{SourceMode::file, ""}))`. That composes serialize and parse, so the `{file, ""} → live` normalization could be enforced entirely inside `serialize` (which may emit `mode=live` for an empty path), and `parse` would never be exercised on a real "file token + empty path" string. A `parse()` implementation that does *not* defend against a half-populated file record would still pass this test, because `serialize` already cleaned the input. The test therefore does not test the contract its name claims (`parse` never returns half-populated file mode).

Nothing in the suite feeds `parse` a hand-crafted file-mode-with-no-path string directly (e.g. `parse("mode=file")` or `parse("mode=file\npath=")`). That is the input that actually arrives from a corrupted/truncated settings file at launch — the FR-009 "corrupt settings → safe defaults" path. A correct fix: add a direct `CHECK(parse("mode=file") == SourceConfig{live, ""})` (and the empty-path-line variant) so `parse`'s own invariant is pinned independent of `serialize`.

### Embedded-delimiter (newline) file paths are untested, and the "verbatim emit" test forecloses the only safe fix

Finding-ID: AUDIT-BARRAGE-claude-04
Status:     open
Severity:   medium
Surface:    tests/core/workbench-settings-test.cpp:25-34, 52-57

The garbage-input cases (`parse("\n\n")`, `parse("mode=banana")`) imply a newline/`key=value` line-oriented serialization format. The "spaces and unicode" round-trip test (lines 25-34) deliberately exercises awkward paths but never includes a path containing the format's own delimiter — most importantly an embedded newline, which is a legal character in a POSIX filename. If the format is line-delimited, a path like `"/tmp/a\nb.wav"` will not round-trip: `parse` reads only up to the newline. This is precisely the kind of file `juce::FileChooser` can return, and the feature persists arbitrary user-chosen paths.

Compounding it, the last test (lines 52-57) asserts `serialize` emits the path *verbatim* (`text.find(path) != npos`). That actively forbids the standard fix for delimiter injection — escaping/quoting the path on the way out — so the test contract locks in the unsafe behavior while providing no test that would catch the resulting broken round-trip. A reasonable fix: add a round-trip case for a path containing a newline (and the delimiter char, whatever it is) and relax the verbatim-emit assertion to permit an escaped encoding, so the implementation is free to be delimiter-safe.

### Adding `adapters/workbench` to the whole core-test include path is a layering leak; the test is also misfiled under `tests/core/`

Finding-ID: AUDIT-BARRAGE-claude-05
Status:     open
Severity:   low
Surface:    tests/CMakeLists.txt:18-22; tests/core/workbench-settings-test.cpp:1

`target_include_directories(acfx_core_tests PRIVATE .../adapters/workbench)` is applied to the entire `acfx_core_tests` executable, so every core test TU (`parameter-test`, `svf-test`, `no-allocation-test`, …) can now `#include "workbench-settings.h"` and other adapter headers. The platform-independent-core / thin-adapter rule says dependencies point only inward; a core test accidentally reaching into an adapter header would now compile silently rather than fail. The serde seam is genuinely JUCE-free, so linking the one TU is fine — but the include grant is broader than the single test that needs it, and the file lives at `tests/core/workbench-settings-test.cpp` despite testing a workbench *adapter* unit (the CMake comment itself calls it "the workbench seam").

Lower blast radius than the above — it is test-only and compiles correctly today — but it erodes the layering boundary the project leans on. A cleaner shape: a small dedicated `acfx_workbench_serde_tests` target (or scoping the include to the single TU), and placing the file under `tests/adapters/workbench/` to match what it covers.

---

Net: the build wiring and the happy-path round-trips are sound, but two structural signals stand out for the operator — (1) the task ledger over-reports completion by checking `[X]` on acceptance/RT-safety scenarios that were explicitly *not* run (AUDIT-BARRAGE-claude-01/02), and (2) two of the serde tests don't pin the contract their names claim, leaving the corrupt-settings and delimiter-in-path edges — the exact failure modes FR-009 cares about — uncovered (AUDIT-BARRAGE-claude-03/04).