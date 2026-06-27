### Test file placed in adapter-layer directory labeled as core

Finding-ID: AUDIT-BARRAGE-claude-01
Status:     open
Severity:   medium
Surface:    tests/core/workbench-settings-test.cpp:1–57, tests/CMakeLists.txt:11

`workbench-settings-test.cpp` lives under `tests/core/` and is registered alongside `parameter-test.cpp`, `svf-test.cpp`, and `no-allocation-test.cpp` — tests that exercise true core DSP primitives. Yet the subject under test (`workbench-settings.h`, `workbench-settings.cpp`) lives under `adapters/workbench/`. To make it compile, `tests/CMakeLists.txt` must add `adapters/workbench` to the include path of the entire `acfx_core_tests` target (line 18–19 of the diff). The test's location in `tests/core/` implies a layering invariant ("this is a test of a core concern") that is false: it is a test of an adapter seam. A future maintainer browsing `tests/core/` will either conclude that serde lives in core (incorrect mental model) or will silently add adapter-layer `#include`s to other files in that directory without triggering any build-time complaint, because the include path is already open. The appropriate fix is a new `tests/adapters/workbench/` (or similar) directory for this and future adapter-side tests, keeping `tests/core/` governed by the core-only invariant.

---

### `adapters/workbench` include path bleeds into every core test

Finding-ID: AUDIT-BARRAGE-claude-02
Status:     open
Severity:   low
Surface:    tests/CMakeLists.txt:18–19

```cmake
target_include_directories(acfx_core_tests PRIVATE "${CMAKE_SOURCE_DIR}/adapters/workbench")
```

This is added to `acfx_core_tests`, so every test in that target — present and future — can now `#include "workbench-settings.h"` or any other JUCE-free adapter header without any warning. The blast radius: if a core test accidentally includes an adapter header, the architectural layer guarantee ("the DSP core knows nothing of adapters") is silently violated at the test level. The only guard is that JUCE-bearing headers would fail to compile, but JUCE-free adapter headers (this one included) pass right through. The fix is colocated with Finding-01: a separate test target for adapter-side tests would make this `target_include_directories` scoped to only the adapter tests.

---

### Deferral phrases added to five task entries

Finding-ID: AUDIT-BARRAGE-claude-03
Status:     open
Severity:   low
Surface:    specs/workbench-audio-config/tasks.md — T008, T011, T014, T016, T019

The diff adds the phrase "deferred to graduation" to five task entries (T008, T011, T014, T016, T019). Per audit rules, deferral phrases in a diff are a required finding regardless of context. The framing "operator-owned, deferred to graduation" is a softer variant of "fix later" — it names a future phase without specifying a concrete criterion for when the gate is met. If "graduation" is not defined elsewhere in the spec, an agent re-reading the task file has no way to determine what constitutes graduation or who decides it. The operative risk is that these acceptance scenarios — the ones that verify audio actually routes correctly, files loop, persistence restores, and MIDI filtering is selective — never get a formal close signal, and the feature ships without any human having run them. The mitigation is to define "graduation" as a named milestone in the spec and add a single T020 that tracks the completion of all deferred acceptance scenarios as an explicit gate.

---

### T019 mixes execution-state status into the specification text

Finding-ID: AUDIT-BARRAGE-claude-04
Status:     open
Severity:   low
Surface:    specs/workbench-audio-config/tasks.md — T019

The T019 entry now reads (paraphrased): `Run Scenario A and Scenario F ... update README — **Scenario A passing (17/17 host tests) and README updated; Scenario F is interactive manual acceptance (operator-owned, deferred to graduation)** (in-UI device/source/MIDI selection + persistence; ...)`. The bolded status report is embedded mid-sentence inside the original task specification text, after a long dash. The result is that the specification and execution record are conflated. An agent re-reading the task file cannot distinguish "what was the original requirement" from "what happened when it ran." This is a documentation-hygiene issue: status belongs in a separate `Status:` field or a changelog entry, not woven into the spec sentence. It is low severity because a human reader can parse it, but for an unattended agent re-reading the tasks file, the most natural reading of the sentence is unclear.

---

### Serde contract tests omit newline-in-path edge case

Finding-ID: AUDIT-BARRAGE-claude-05
Status:     open
Severity:   informational
Surface:    tests/core/workbench-settings-test.cpp:30–38

The unicode/spaces round-trip test covers spaces, multi-byte characters, and non-ASCII symbols. It does not cover a path containing a newline character (`\n`). If the serialization format is newline-delimited (a common choice for key-value text formats), a path containing `\n` would silently truncate or misparse. The "serialize emits the file path verbatim" test (line 51–56) only checks that the path appears as a substring; it does not check that the path appears as a single contiguous token. The blast radius is low — paths with embedded newlines are illegal on most target filesystems — but the contract in `contracts/source-config.md` (referenced in the diff comments) should explicitly state whether newlines in paths are in or out of scope, so future implementors of the format know what invariant they must preserve.