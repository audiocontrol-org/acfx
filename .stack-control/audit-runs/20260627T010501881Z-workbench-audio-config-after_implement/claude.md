I'm acting as an independent audit reviewer for chunk `cc36a7e4cc6d3feb` (tasks.md, tests/CMakeLists.txt, the new serde test). Here are my findings.

### Test for parse's half-populated-file guard routes through serialize, so it can't actually fail

Finding-ID: AUDIT-BARRAGE-claude-01
Status:     open
Severity:   medium
Surface:    tests/core/workbench-settings-test.cpp:44-52

The test named `"parse never returns half-populated file mode (file token, empty path -> live)"` claims to verify a `parse()` invariant, but its body calls `parse(serialize(SourceConfig{SourceMode::file, ""}))` — it feeds `parse` only what `serialize` chose to emit, never a raw file-token-with-empty-path string. If `serialize` normalizes an empty-path file config to live-mode output (a plausible and arguably correct implementation), then `parse` is handed a *live* serialization and the empty-path-file branch in `parse` is never exercised at all. The test would stay green even if `parse` had no half-populated guard whatsoever.

This is a test that doesn't test the contract it advertises. The contract under `contracts/source-config.md` is about what `parse` does with malformed *input*; to test it you must construct the raw malformed serialized form directly (e.g. `parse("mode=file\npath=")` or whatever the wire format is) and assert `== {live, ""}`, independent of `serialize`. Blast radius: an unattended agent extending this seam would trust the test as coverage of the `parse` guard, refactor `parse` to drop it, and ship a regression that lands the workbench in the exact broken empty-path file state the test claims to forbid (FR-009). Fix: add a direct `parse(<raw file-token, empty path>)` assertion alongside the round-trip one.

### No fixture for a file path containing the serde delimiter (newline / `=`), which verbatim emission forecloses fixing

Finding-ID: AUDIT-BARRAGE-claude-02
Status:     open
Severity:   medium
Surface:    tests/core/workbench-settings-test.cpp:24-33,54-57

The unicode/spaces fixture (lines 24-33) exercises non-ASCII and embedded spaces, and test 6 (lines 54-57) pins the format to emitting the path **verbatim** (`text.find(path) != npos`). Test 4 (`parse("\n\n") == def`) implies a newline-delimited key/value wire format. Put together, these lock in a format where the path field is emitted raw and the record is newline-delimited — but no fixture covers a path that contains the delimiter itself: a `\n` (newlines are legal in POSIX filenames) or, depending on the split rule, an `=`. Such a path serializes to something `parse` will truncate or mis-split, breaking the round-trip the whole seam exists to guarantee.

The reason this is worth flagging rather than waving off as exotic: test 6's verbatim-emission assertion actively *prevents* the natural fix (escaping/quoting the path), so the format choice and the missing fixture compound. Blast radius is bounded — an operator picking a file through `juce::FileChooser` is unlikely to produce a newline path — so medium, not high. A reasonable fix is one fixture with a `\n`-bearing path asserting round-trip, which will either prove the format is robust or force an escaping decision before the verbatim contract ossifies.

### acfx_core_tests gains the entire adapters/workbench directory on its include path — a JUCE adapter surface leaking into the JUCE-free core test target

Finding-ID: AUDIT-BARRAGE-claude-03
Status:     open
Severity:   medium
Surface:    tests/CMakeLists.txt:19-22

To resolve `#include "workbench-settings.h"` the change adds `target_include_directories(acfx_core_tests PRIVATE "${CMAKE_SOURCE_DIR}/adapters/workbench")`. That puts the *whole* adapter directory on the include path of the deliberately JUCE-free core test target, exposing `audio-settings.h`, `audio-source.h`, `source-bar.h`, and `workbench-persistence.h` — all JUCE-dependent headers — as bare-name includes any core test can now reach. The constitution's "platform-independent core, thin adapters; dependencies point only inward" boundary (CLAUDE.md) is the thing being protected, and this widens a hole right through it: a future core test that accidentally `#include "audio-source.h"` will pull JUCE into a target that has no JUCE, and the failure mode is a confusing build break rather than a clean layering error.

The single JUCE-free seam (`workbench-settings.h`) is being reached by broadening exposure to all of its JUCE-bound neighbors. A tighter fix keeps the seam reachable without the blast radius: relocate the JUCE-free `workbench-settings.{h,cpp}` into a location both targets already share (or factor it behind its own tiny interface target the test links), so the core test depends on exactly that contract and nothing else in the adapter folder. Medium — it's a latent discipline hazard, not an active failure, but it erodes the boundary the project treats as load-bearing.

### Acceptance-only tasks are checked [X] complete while their entire content is deferred ("deferred to graduation")

Finding-ID: AUDIT-BARRAGE-claude-04
Status:     open
Severity:   medium
Surface:    specs/workbench-audio-config/tasks.md:T008,T011,T014,T016,T019

T008, T011, T014, T016 are pure acceptance-run tasks ("Run quickstart Scenario B/C/D/E and confirm the US1–US4 acceptance scenarios"). Each is now marked `[X]` *and* annotated `*(manual acceptance — operator-owned, deferred to graduation)*` — i.e. the checkbox says done while the task's entire body (actually running the scenario) is explicitly not done. T019 is the same shape for Scenario F. The machine-readable signal in this file is the `[X]`: a downstream reader (or an unattended agent harvesting completion state) tallying tasks.md sees every box checked and concludes the feature is fully verified end-to-end, including the in/out device routing, file-source switching, persistence-restore, and MIDI-input acceptance — none of which were executed.

Per this audit's hard constraint, "deferred to graduation" is a deferral phrase in the diff and is surfaced as such. I note the disposition itself appears deliberate (commit 92a865c, and a prior governance note on operator-owned manual acceptance), so I'm not re-litigating *whether* to defer interactive scenarios — that's an accepted boundary. The finding is narrower: an acceptance task whose only deliverable is "run the scenario" should not carry `[X]` when the scenario wasn't run, because the checkbox contradicts the annotation and the checkbox is what gets counted. Medium: a careful human reader resolves it from the inline note, but the `[X]`/annotation contradiction is exactly the kind of quietly-wrong completion signal an unattended consumer reads the wrong way. Fix: represent deferred-acceptance as a distinct state (e.g. leave unchecked with the operator-owned annotation, or a `[~]`/`[ ]` + "blocked on operator graduation") rather than `[X]`.