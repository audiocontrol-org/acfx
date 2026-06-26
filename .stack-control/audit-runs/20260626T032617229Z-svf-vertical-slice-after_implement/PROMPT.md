# Audit-barrage — multi-model audit prompt template

You are an **independent audit reviewer** firing as part of a multi-model audit barrage. Your siblings (other CLIs running this same prompt in parallel) emit their own findings independently; the operator triages all of your outputs side-by-side after every model has settled. Your job is to surface the kinds of defects listed under **What to look for** below, in the work product captured under **Under audit**.

You are NOT collaborating with the other models. You write what you see. The cross-model genetic diversity comes from each of you reporting independently.

## Feature under audit

svf-vertical-slice

## Feature scope (workplan / PRD summary)



## Commit subjects in the audited range

8a4c5d6 Address round-7 govern findings: T038 wording, atomic configured_, mode-knob evidence
d183a18 Address round-6 govern findings: correct ARM overclaim, README, source hygiene
9ae4b58 Address round-5 govern findings: compile-time validation, RT alloc, CI, source path
3262fb3 Address round-4 govern findings: NaN-safe clamp, channel consistency, honest ledger
2406235 Address round-3 govern findings: precise contracts, lock-free atomics, enforced precondition
2fef393 Address round-2 govern findings: RT-safety, error surfacing, adapter races
bd79479 Address govern findings: RT-safety, thread ownership, doc drift
8e0e37b Replace vendored CPM.cmake with pinned auto-download bootstrap
f88525b Close acceptance tasks with honest verified/manual split (T027/T031/T035)
60b4523 Fix desktop build integration + JUCE-API bugs caught by real compilation
ee53b33 Phase 6 (polish): CI, explicit portability gates, README
ae69f91 Phase 5 (US3): Daisy + Teensy adapters; core proven ARM-portable
e74b0db Phase 4 (US2): DAW plugin (VST3 / AU / CLAP)
e27e832 Phase 3 (US1): desktop sketch-and-hear workbench (JUCE)
0ebd7d3 Phase 2 (foundational): core spine + host-side tests, all green
1b05595 Phase 1 (setup): monorepo skeleton + CMake build system


## Recent audit-log excerpt (prior findings on this feature)

Use this to avoid re-reporting findings that have already been triaged. If a finding was previously dispositioned (`closed`, `won't-fix`, `accepted-trade-off`), don't re-litigate the disposition; only surface a new instance if the underlying shape regressed.



## Under audit

The actual code under review. Read it carefully. The findings you emit must be anchored to specific files + line ranges in this diff (or call out a missing surface that should be in the diff but isn't).

Governance pass over the just-implemented work for feature 'svf-vertical-slice', diffed against ff3426a. The differentiated back half audits a plan it did not author or execute.
## Other chunks (file lists only — context for cross-file dependencies this chunk cannot see):
- 31c30149ec9faef5: tests/core/parameter-test.cpp, tests/core/svf-test.cpp, tests/core/test-main.cpp, tests/support/allocation-sentinel.cpp, tests/support/allocation-sentinel.h, tests/support/svf-reference.h
- 4cfb00d5b3480886: adapters/workbench/midi-binding.h, adapters/workbench/parameter-view.cpp, adapters/workbench/parameter-view.h, adapters/workbench/workbench-app.cpp, cmake/CPM.cmake, cmake/dependencies.cmake, cmake/toolchains/daisy.cmake
- 6a56babffbf5b038: .clang-format, .editorconfig, .github/workflows/ci.yml, .gitignore, .stack-control/govern/convergence/impl__design-feature-svf-vertical-slice.json, CMakeLists.txt, CMakePresets.json, README.md, adapters/daisy/CMakeLists.txt, adapters/daisy/daisy-main.cpp, adapters/plugin/CMakeLists.txt
- a05731de7f2cca0b: core/primitives/svf-primitive.h, external/.gitkeep, host/processor-node/CMakeLists.txt, host/processor-node/processor-node.h, scripts/check-portability.sh
- a70ef633c8635ce4: adapters/plugin/plugin-parameters.cpp, adapters/plugin/plugin-parameters.h, adapters/plugin/plugin-processor.cpp, adapters/plugin/plugin-processor.h, adapters/teensy/CMakeLists.txt, adapters/teensy/teensy-main.cpp, adapters/workbench/CMakeLists.txt, adapters/workbench/audio-source.cpp, adapters/workbench/audio-source.h
- e7b284327d06692a: cmake/toolchains/teensy.cmake, core/dsp/audio-block.h, core/dsp/effect.h, core/dsp/param-id.h, core/dsp/parameter.h, core/dsp/process-context.h, core/dsp/span.h, core/effects/svf/svf-effect.h

## Chunk 66d76089d898899c
Files in scope: specs/svf-vertical-slice/tasks.md, tests/CMakeLists.txt, tests/core/no-allocation-test.cpp

## Diffs

### specs/svf-vertical-slice/tasks.md
diff --git a/specs/svf-vertical-slice/tasks.md b/specs/svf-vertical-slice/tasks.md
index 7e87b9f..f08ecac 100644
--- a/specs/svf-vertical-slice/tasks.md
+++ b/specs/svf-vertical-slice/tasks.md
@@ -36,12 +36,12 @@ Cross-platform C++ monorepo (per plan.md): `core/`, `host/`, `adapters/*`, `test
 
 ## Phase 1: Setup (build skeleton)
 
-- [ ] T001 Create the monorepo directory skeleton with placeholders (`core/dsp/`, `core/primitives/`, `core/effects/svf/`, `host/processor-node/`, `adapters/workbench/`, `adapters/plugin/`, `adapters/daisy/`, `adapters/teensy/`, `tests/core/`, `tests/support/`, `cmake/`, `external/`) per `plan.md` Project Structure
-- [ ] T002 Add top-level `CMakeLists.txt` defining the `acfx_core` interface/library target and orchestrating subdirectories, with C++17/C++20 handling (concept checks guarded by `__cpp_concepts`)
-- [ ] T003 [P] Add `cmake/CPM.cmake` and `cmake/dependencies.cmake` declaring CPM-pinned deps (JUCE 8, clap-juce-extensions, DaisySP, libDaisy, Teensy core+Audio Library, doctest); capture each exact tag in the declaration when first fetched (research.md §4)
-- [ ] T004 [P] Add `CMakePresets.json` with presets `desktop`, `daisy`, `teensy`, `test`
-- [ ] T005 [P] Add `cmake/toolchains/daisy.cmake` (arm-none-eabi-gcc) and `cmake/toolchains/teensy.cmake` toolchain files
-- [ ] T006 [P] Add `.clang-format` and `.editorconfig` aligned with strict typing (no implicit narrowing) at repo root
+- [X] T001 Create the monorepo directory skeleton with placeholders (`core/dsp/`, `core/primitives/`, `core/effects/svf/`, `host/processor-node/`, `adapters/workbench/`, `adapters/plugin/`, `adapters/daisy/`, `adapters/teensy/`, `tests/core/`, `tests/support/`, `cmake/`, `external/`) per `plan.md` Project Structure
+- [X] T002 Add top-level `CMakeLists.txt` defining the `acfx_core` interface/library target and orchestrating subdirectories, with C++17/C++20 handling (concept checks guarded by `__cpp_concepts`)
+- [X] T003 [P] Add `cmake/CPM.cmake` and `cmake/dependencies.cmake` declaring CPM-pinned deps (JUCE 8, clap-juce-extensions, DaisySP, libDaisy, Teensy core+Audio Library, doctest); capture each exact tag in the declaration when first fetched (research.md §4)
+- [X] T004 [P] Add `CMakePresets.json` with presets `desktop`, `daisy`, `teensy`, `test`
+- [X] T005 [P] Add `cmake/toolchains/daisy.cmake` (arm-none-eabi-gcc) and `cmake/toolchains/teensy.cmake` toolchain files
+- [X] T006 [P] Add `.clang-format` and `.editorconfig` aligned with strict typing (no implicit narrowing) at repo root
 
 **Checkpoint**: `cmake --preset test` configures cleanly (empty test target ok).
 
@@ -52,20 +52,20 @@ Cross-platform C++ monorepo (per plan.md): `core/`, `host/`, `adapters/*`, `test
 The platform-independent spine every story reuses. No JUCE/libDaisy/Teensy headers
 in any `core/` file (Constitution IV).
 
-- [ ] T007 [P] Implement `ParamId` + `ParamUnit`/`ParamSkew`/`ParamKind` enums in `core/dsp/param-id.h` (per contracts/parameter-model.md)
-- [ ] T008 [P] Implement `ParameterDescriptor` + allocation-free `normalize`/`denormalize` (linear, logarithmic, discrete) in `core/dsp/parameter.h` (per contracts/parameter-model.md)
-- [ ] T009 [P] Implement `ProcessContext` (sampleRate, maxBlockSize, numChannels) in `core/dsp/process-context.h`
-- [ ] T010 [P] Implement `AudioBlock` (fixed-size, non-owning, non-allocating view) in `core/dsp/audio-block.h`
-- [ ] T011 Define the `Effect` concept (C++20, `__cpp_concepts`-guarded; C++17 duck-typed fallback) in `core/dsp/effect.h` (per contracts/effect-concept.md) — depends on T007–T010
-- [ ] T012 [P] Implement the allocation sentinel (global `operator new`/`delete` counter, thread-local) in `tests/support/allocation-sentinel.h` + `tests/support/allocation-sentinel.cpp`
-- [ ] T013 [P] Capture known-good SVF frequency-response reference vectors in `tests/support/svf-reference.h`
-- [ ] T014 Write parameter scaling/skew tests in `tests/core/parameter-test.cpp` (assert linear/log/discrete mapping at min/mid/max + bounds) — fails until T008
-- [ ] T015 Implement the DaisySP `Svf` wrapper (allocation-free, mode-selectable) in `core/primitives/svf-primitive.h` (research.md §1) — depends on T003
-- [ ] T016 Write SVF effect tests (impulse/frequency response per mode vs T013 references, NaN/denormal stability at high resonance) in `tests/core/svf-test.cpp` — fails until T017
-- [ ] T017 Implement `SvfEffect` (constexpr param table cutoff[log]/resonance[linear]/mode[discrete×3]; `prepare`/`process`/`reset`/`setParameter`) in `core/effects/svf/svf-effect.h` (+ `.cpp` if needed) satisfying `Effect` — makes T014/T016 pass
-- [ ] T018 Write + pass the no-heap-allocation-in-`process()` invariant test in `tests/core/no-allocation-test.cpp` using the T012 sentinel across several block sizes (FR-014) — depends on T017
-- [ ] T019 Implement `ProcessorNode` interface + `EffectNode<T>` template in `host/processor-node/processor-node.h` (per contracts/processor-node.md; desktop-only, ≤1 vcall/block) — depends on T011
-- [ ] T020 Wire the `test` preset target so `ctest --preset test` builds + runs all `tests/core/*` (FR-013)
+- [X] T007 [P] Implement `ParamId` + `ParamUnit`/`ParamSkew`/`ParamKind` enums in `core/dsp/param-id.h` (per contracts/parameter-model.md)
+- [X] T008 [P] Implement `ParameterDescriptor` + allocation-free `normalize`/`denormalize` (linear, logarithmic, discrete) in `core/dsp/parameter.h` (per contracts/parameter-model.md)
+- [X] T009 [P] Implement `ProcessContext` (sampleRate, maxBlockSize, numChannels) in `core/dsp/process-context.h`
+- [X] T010 [P] Implement `AudioBlock` (fixed-size, non-owning, non-allocating view) in `core/dsp/audio-block.h`
+- [X] T011 Define the `Effect` concept (C++20, `__cpp_concepts`-guarded; C++17 duck-typed fallback) in `core/dsp/effect.h` (per contracts/effect-concept.md) — depends on T007–T010
+- [X] T012 [P] Implement the allocation sentinel (global `operator new`/`delete` counter, thread-local) in `tests/support/allocation-sentinel.h` + `tests/support/allocation-sentinel.cpp`
+- [X] T013 [P] Capture known-good SVF frequency-response reference vectors in `tests/support/svf-reference.h`
+- [X] T014 Write parameter scaling/skew tests in `tests/core/parameter-test.cpp` (assert linear/log/discrete mapping at min/mid/max + bounds) — fails until T008
+- [X] T015 Implement the DaisySP `Svf` wrapper (allocation-free, mode-selectable) in `core/primitives/svf-primitive.h` (research.md §1) — depends on T003
+- [X] T016 Write SVF effect tests (impulse/frequency response per mode vs T013 references, NaN/denormal stability at high resonance) in `tests/core/svf-test.cpp` — fails until T017
+- [X] T017 Implement `SvfEffect` (constexpr param table cutoff[log]/resonance[linear]/mode[discrete×3]; `prepare`/`process`/`reset`/`setParameter`) in `core/effects/svf/svf-effect.h` (+ `.cpp` if needed) satisfying `Effect` — makes T014/T016 pass
+- [X] T018 Write + pass the no-heap-allocation-in-`process()` invariant test in `tests/core/no-allocation-test.cpp` using the T012 sentinel across several block sizes (FR-014) — depends on T017
+- [X] T019 Implement `ProcessorNode` interface + `EffectNode<T>` template in `host/processor-node/processor-node.h` (per contracts/processor-node.md; desktop-only, ≤1 vcall/block) — depends on T011
+- [X] T020 Wire the `test` preset target so `ctest --preset test` builds + runs all `tests/core/*` (FR-013)
 
 **Checkpoint**: `cmake --build --preset test && ctest --preset test` is green — the
 core spine + SVF are proven host-side. User stories can now proceed in any order.
@@ -79,15 +79,18 @@ core spine + SVF are proven host-side. User stories can now proceed in any order
 **Independent test**: quickstart Scenario B — build + launch the workbench, route audio,
 sweep cutoff/resonance/mode, drive a bound MIDI CC, toggle A/B; edit + rebuild + relaunch.
 
-- [ ] T021 [US1] Add the JUCE standalone workbench target in `adapters/workbench/CMakeLists.txt` (under the `desktop` preset)
-- [ ] T022 [US1] Implement the workbench app holding `std::unique_ptr<ProcessorNode>` = `EffectNode<SvfEffect>` and the audio device callback in `adapters/workbench/workbench-app.cpp` — depends on T019
-- [ ] T023 [P] [US1] Auto-render a control per descriptor from `SvfEffect::parameters()` in `adapters/workbench/parameter-view.cpp`
-- [ ] T024 [P] [US1] Bind MIDI CCs → `setParameter(id, normalized)` in `adapters/workbench/midi-binding.cpp`
-- [ ] T025 [P] [US1] Implement the audio source (built-in loop/file player + live input device selection; descriptive error if neither available — Constitution V) in `adapters/workbench/audio-source.cpp`
-- [ ] T026 [US1] Implement the dry/processed A/B toggle in the workbench signal path in `adapters/workbench/workbench-app.cpp`
-- [ ] T027 [US1] Run quickstart Scenario B end-to-end and confirm all US1 acceptance scenarios
+- [X] T021 [US1] Add the JUCE standalone workbench target in `adapters/workbench/CMakeLists.txt` (under the `desktop` preset)
+- [X] T022 [US1] Implement the workbench app holding `std::unique_ptr<ProcessorNode>` = `EffectNode<SvfEffect>` and the audio device callback in `adapters/workbench/workbench-app.cpp` — depends on T019
+- [X] T023 [P] [US1] Auto-render a control per descriptor from `SvfEffect::parameters()` in `adapters/workbench/parameter-view.cpp`
+- [X] T024 [P] [US1] Bind MIDI CCs → `setParameter(id, normalized)` in `adapters/workbench/midi-binding.cpp`
+- [X] T025 [P] [US1] Implement the audio source (built-in loop/file player + live input device selection; descriptive error if neither available — Constitution V) in `adapters/workbench/audio-source.cpp`
+- [X] T026 [US1] Implement the dry/processed A/B toggle in the workbench signal path in `adapters/workbench/workbench-app.cpp`
+- [X] T027 [US1] Build + automated-verify the workbench (Scenario B build): the JUCE workbench compiles + links into a runnable `acfx Workbench.app` (arm64), controls auto-generate from `SvfEffect::parameters()`, and the core path is test-green. (The interactive end-to-end run — live sweep / MIDI / A-B listening — is an operator checkpoint in **Manual acceptance** below, not part of this automated task.)
 
-**Checkpoint**: US1 is independently shippable — a working sketch-and-hear workbench.
+**Checkpoint**: US1 build-complete — the workbench compiles + links into a runnable
+`acfx Workbench.app` with auto-generated controls; the live sketch-and-hear run
+(audio sweep, MIDI CC, dry/processed A/B listening) is the operator's manual
+acceptance (T027), pending a machine with audio I/O.
 
 ---
 
@@ -98,12 +101,15 @@ sweep cutoff/resonance/mode, drive a bound MIDI CC, toggle A/B; edit + rebuild +
 **Independent test**: quickstart Scenario C — load each format in a host, automate cutoff,
 confirm parity with the workbench.
 
-- [ ] T028 [US2] Add the JUCE plugin target exporting VST3 + AU + CLAP (CLAP via clap-juce-extensions) in `adapters/plugin/CMakeLists.txt`
-- [ ] T029 [US2] Implement the plugin `AudioProcessor` wrapping the same `EffectNode<SvfEffect>` in `adapters/plugin/plugin-processor.cpp` — depends on T019
-- [ ] T030 [US2] Generate host-automation parameters from `SvfEffect::parameters()` (name/range/default/skew) in `adapters/plugin/plugin-parameters.cpp`
-- [ ] T031 [US2] Run quickstart Scenario C and confirm all US2 acceptance scenarios (formats instantiate; params correct; parity with workbench)
+- [X] T028 [US2] Add the JUCE plugin target exporting VST3 + AU + CLAP (CLAP via clap-juce-extensions) in `adapters/plugin/CMakeLists.txt`
+- [X] T029 [US2] Implement the plugin `AudioProcessor` wrapping the same `EffectNode<SvfEffect>` in `adapters/plugin/plugin-processor.cpp` — depends on T019
+- [X] T030 [US2] Generate host-automation parameters from `SvfEffect::parameters()` (name/range/default/skew) in `adapters/plugin/plugin-parameters.cpp`
+- [X] T031 [US2] Build + automated-verify the plugin (Scenario C build): the plugin compiles + links into VST3, AU (`.component`), and CLAP bundles (all Mach-O arm64); host-automation params are generated from the same `SvfEffect::parameters()` table the workbench uses (SC-006 by construction). (The interactive in-DAW run — per-format instantiation, cutoff automation, audible parity — is an operator checkpoint in **Manual acceptance** below, not part of this automated task.)
 
-**Checkpoint**: US2 is independently shippable — the plugin, sharing the core with US1.
+**Checkpoint**: US2 build-complete — the plugin compiles + links into VST3, AU, and
+CLAP bundles (arm64) from the shared core; in-DAW instantiation, automation, and
+audible parity with the workbench are the operator's manual acceptance (T031),
+pending a plugin host.
 
 ---
 
@@ -115,24 +121,47 @@ mapping; no JUCE in the MCU builds.
 **Independent test**: quickstart Scenario D — `daisy` and `teensy` presets build & link
 the same `core/effects/svf`; each dependency graph shows core + adapter only.
 
-- [ ] T032 [P] [US3] Implement the Daisy adapter (libDaisy audio callback → `effect.process`; ADC/encoder → `setParameter`) in `adapters/daisy/daisy-main.cpp`
-- [ ] T033 [P] [US3] Implement the Teensy adapter (Teensy `AudioStream` node → `effect.process`; analog/MIDI → `setParameter`) in `adapters/teensy/teensy-main.cpp`
-- [ ] T034 [US3] Verify the installed Teensy toolchain's C++ standard (research.md §3 open item); set Teensy to the highest supported (≥C++17) in `cmake/toolchains/teensy.cmake` and confirm the concept-degradation path compiles the same `SvfEffect`
-- [ ] T035 [US3] Run quickstart Scenario D: build `daisy` + `teensy` presets, confirm linked artifacts and that neither dependency graph includes JUCE or desktop-only stubs (SC-007)
+- [X] T032 [P] [US3] Implement the Daisy adapter (libDaisy audio callback → `effect.process`; ADC/encoder → `setParameter`) in `adapters/daisy/daisy-main.cpp`
+- [X] T033 [P] [US3] Implement the Teensy adapter (Teensy `AudioStream` node → `effect.process`; analog/MIDI → `setParameter`) in `adapters/teensy/teensy-main.cpp`
+- [X] T034 [US3] Verify the installed Teensy toolchain's C++ standard (research.md §3 open item); set Teensy to the highest supported (≥C++17) in `cmake/toolchains/teensy.cmake` and confirm the concept-degradation path compiles the same `SvfEffect`
+- [X] T035 [US3] Standard-portability + no-JUCE verification (Scenario D core): the identical `core/effects/svf` compiles at both C++17 (concept degraded) **and** C++20 (named concept) — verified on the **host** toolchain, which proves the source is standard-portable (the Teensy concept-degradation concern, T034) and that the lock-free `is_always_lock_free` static_assert holds; `core/` + both MCU adapters reference no JUCE / ProcessorNode (portability gate green). **Not verified here:** the actual `arm-none-eabi` Cortex-M7 compile and the firmware ELF link — the installed `arm-none-eabi-gcc` is C-only (no libstdc++), so the on-target compile, link, and flashing are the on-hardware checkpoint in **Manual acceptance** below.
 
-**Checkpoint**: US3 done — the cross-platform claim is real (build + link on both MCUs).
+**Checkpoint**: US3 standard-portability verified — the identical `core/effects/svf`
+compiles at both C++17 and C++20 on the host (proving the cross-standard claim) with
+no JUCE in either MCU adapter's surface. The on-target `arm-none-eabi` compile + the
+firmware ELF link + flashing are NOT done here (the installed toolchain is C-only,
+no libstdc++); they are the on-hardware checkpoint (T035, Manual acceptance below).
 
 ---
 
 ## Phase 6: Polish & cross-cutting concerns
 
-- [ ] T036 [P] Add a CI workflow building + testing the `test` and `desktop` presets on every change (quickstart Scenario A + desktop build), as explicit steps — NOT a git hook (Constitution II, FR-015)
-- [ ] T037 [P] Add explicit script/CI checks for the file-size budget (~300–500 lines, Constitution VII) and "no JUCE in MCU dependency graph" (SC-007) — visible steps, not hooks
-- [ ] T038 [P] Confirm the one-source-many-targets invariant (quickstart Scenario E): the identical `core/effects/svf` built all four targets with no per-target `#ifdef` forks of the effect (SC-001, SC-005)
-- [ ] T039 Update `README.md` with build/run instructions referencing `quickstart.md`
+- [X] T036 [P] Add a CI workflow building + testing the `test` and `desktop` presets on every change (quickstart Scenario A + desktop build), as explicit steps — NOT a git hook (Constitution II, FR-015)
+- [X] T037 [P] Add explicit script/CI checks for the file-size budget (~300–500 lines, Constitution VII) and "no JUCE in MCU dependency graph" (SC-007) — visible steps, not hooks
+- [X] T038 [P] Confirm the one-source-many-targets invariant (quickstart Scenario E): the identical `core/effects/svf` (no per-target `#ifdef` forks of the effect) built the two desktop targets here and was host-compile-verified at C++17/C++20 for the two MCU targets; the on-target MCU build is the Manual-acceptance checkpoint (SC-001, SC-005)
+- [X] T039 Update `README.md` with build/run instructions referencing `quickstart.md`
 
 ---
 
+## Manual acceptance (operator-run — outside automated/CI scope)
+
+The interactive/hardware acceptance runs from `quickstart.md`. They require a
+display + audio device, a plugin host, and a physical board with a full ARM
+toolchain — none available to the automated/CI pipeline — so they are tracked here
+as explicit operator checkpoints, **not** as automated task checkboxes. The build
++ automated verification each depends on (T027 / T031 / T035) is done and green;
+these confirm the human-in-the-loop behaviour on top of that.
+
+- ☐ **US1 / Scenario B** — launch the workbench, route audio (built-in player or
+  live input), sweep cutoff/resonance/mode, drive a bound MIDI CC, toggle
+  dry/processed A/B; edit a DSP constant, rebuild, relaunch within a few seconds.
+- ☐ **US2 / Scenario C** — load the plugin as VST3, AU, and CLAP in a host;
+  automate cutoff; confirm audible parity with the workbench for identical settings.
+- ☐ **US3 / Scenario D** — on a machine with a full ARM embedded toolchain (with
+  libstdc++), run the actual on-target `arm-none-eabi` compile, build + link the
+  `daisy` and `teensy` presets, and flash + listen on a physical board. (Only the
+  host dual-standard compile + no-JUCE check are done in the automated pipeline.)
+
 ## Dependencies & completion order
 
 - **Setup (Phase 1)** → **Foundational (Phase 2)** must complete before any user story.


### tests/CMakeLists.txt
diff --git a/tests/CMakeLists.txt b/tests/CMakeLists.txt
new file mode 100644
index 0000000..6e12236
--- /dev/null
+++ b/tests/CMakeLists.txt
@@ -0,0 +1,27 @@
+# Host-side doctest suite for the platform-independent core (Constitution VIII,
+# FR-013). Builds one binary from all tests/core/* sources + the allocation
+# sentinel, links the core spine + the host boundary, and registers with ctest so
+# `ctest --preset test` runs everything.
+
+add_executable(acfx_core_tests
+  core/test-main.cpp
+  core/parameter-test.cpp
+  core/svf-test.cpp
+  core/no-allocation-test.cpp
+  support/allocation-sentinel.cpp
+)
+
+# tests/ as an include root so test files write #include "support/...".
+target_include_directories(acfx_core_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
+target_link_libraries(acfx_core_tests PRIVATE acfx_core acfx_host doctest::doctest)
+target_compile_features(acfx_core_tests PRIVATE cxx_std_20)
+
+# Per-test-case ctest registration when doctest ships its CMake helper; otherwise
+# register the whole binary as a single test (still satisfies the test preset).
+set(_doctest_module "${doctest_SOURCE_DIR}/scripts/cmake/doctest.cmake")
+if(EXISTS "${_doctest_module}")
+  include("${_doctest_module}")
+  doctest_discover_tests(acfx_core_tests)
+else()
+  add_test(NAME acfx_core_tests COMMAND acfx_core_tests)
+endif()


### tests/core/no-allocation-test.cpp
diff --git a/tests/core/no-allocation-test.cpp b/tests/core/no-allocation-test.cpp
new file mode 100644
index 0000000..60137e9
--- /dev/null
+++ b/tests/core/no-allocation-test.cpp
@@ -0,0 +1,59 @@
+#include <doctest/doctest.h>
+
+#include <vector>
+
+#include "dsp/audio-block.h"
+#include "dsp/param-id.h"
+#include "dsp/process-context.h"
+#include "effects/svf/svf-effect.h"
+#include "processor-node/processor-node.h"
+#include "support/allocation-sentinel.h"
+
+// T018 — the no-heap-allocation-in-process() invariant (FR-014). Uses the
+// thread-local allocation sentinel across several block sizes, on both the bare
+// SvfEffect and the EffectNode<SvfEffect> host boundary. The count is captured
+// out of the measured region before any assertion macro runs.
+
+using namespace acfx;
+using acfx::test::AllocationSentinel;
+
+TEST_CASE("SvfEffect::process allocates nothing across block sizes") {
+    for (int blockSize : {16, 64, 256, 512}) {
+        SvfEffect fx;
+        fx.prepare(ProcessContext{48000.0, blockSize, 2});
+
+        std::vector<float> left(static_cast<std::size_t>(blockSize), 0.1f);
+        std::vector<float> right(static_cast<std::size_t>(blockSize), 0.1f);
+        float* chans[2] = {left.data(), right.data()};
+
+        AllocationSentinel::reset();
+        for (int i = 0; i < 100; ++i) {
+            AudioBlock block(chans, 2, blockSize);
+            fx.process(block);
+            // parameter changes on the audio thread must also be allocation-free
+            fx.setParameter(ParamId{SvfEffect::kCutoff}, (i % 2 == 0) ? 0.25f : 0.75f);
+        }
+        const std::size_t allocations = AllocationSentinel::allocations();
+
+        CHECK_MESSAGE(allocations == 0, "block size ", blockSize, " allocated ", allocations);
+    }
+}
+
+TEST_CASE("EffectNode<SvfEffect>::processBlock allocates nothing") {
+    EffectNode<SvfEffect> node;
+    const int blockSize = 256;
+    node.prepare(ProcessContext{48000.0, blockSize, 2});
+
+    std::vector<float> left(static_cast<std::size_t>(blockSize), 0.1f);
+    std::vector<float> right(static_cast<std::size_t>(blockSize), 0.1f);
+    float* chans[2] = {left.data(), right.data()};
+
+    AllocationSentinel::reset();
+    for (int i = 0; i < 100; ++i) {
+        AudioBlock block(chans, 2, blockSize);
+        node.processBlock(block);
+    }
+    const std::size_t allocations = AllocationSentinel::allocations();
+
+    CHECK_MESSAGE(allocations == 0, "EffectNode processBlock allocated ", allocations);
+}


## What to look for

- **Correctness bugs** — logic errors, off-by-one, null/undefined paths, race conditions, missing error handling, swallowed exceptions.
- **Design issues** — coupling between layers that should be independent, leaking abstractions, primitives that should compose but don't, configuration that should be data ending up as code.
- **Missed edge cases** — what happens with empty input? Maximum input? Concurrent calls? Partial failure? Network unavailability? Operator interrupt mid-operation? What is the behavior on a fresh install vs. an upgrade?
- **Code-quality concerns** — files growing past a reasonable cap, names that don't reveal intent, dead code, duplicated logic, magic numbers without explanation, tests that don't test the contract they claim to test.
- **Cross-cutting impact** — does this diff touch a surface that other surfaces depend on? Are those other surfaces updated? Are migrations needed? Are doctor rules / schemas / validators updated to match the new shape?
- **Documentation drift** — does the README / SKILL.md / PRD describe the behavior the code actually implements? If the spec changed, did the implementation? If the implementation changed, did the spec?
- **Operator-discipline traps** — placeholder comments, swallowed errors, hardcoded paths/values that should be configurable, fallbacks that hide failure modes, mock data outside test code. These are bug-factories per project guidelines.

## Process drivers (029 US8 / FR-029)

These codify the structural drivers of myopic convergence (TASK-60), so the loop converges in fewer rounds with less fix-induced surface growth. The first three (channel-enumeration, invariant-first boundary, round-0 self-red-team) are **fix-review** drivers — apply them when the work under audit is a fix for a prior finding. The last two (fleet-degradation pricing, severity-rubric anchoring) are **general** controls that apply to every round:

- **Channel-enumeration.** When a fix ADDS to an allowlist/surface (a new flag, a new accepted value, a new parser branch, a new fold path), do not accept it on the one example it fixes — enumerate the channels it opens: the **value** channel (other inputs now accepted), the **state** channel (new reachable states), the **multiline / composition** channel (how it composes with adjacent surfaces). Flag any opened channel that lacks a fixture.
- **Invariant-first boundary.** When a finding is dispositioned as a scope boundary, state the boundary as the **mechanism's invariant plus an in-scope exception**, NOT as the exclusion of the one counterexample. "We exclude X" is a smell; "the invariant is I, and X is the in-scope exception because…" is the disposition.
- **Round-0 self-red-team.** When the work under audit is itself a FIX for a prior finding, audit the **fix diff as a fresh surface in its own right** — do not assume it is correct merely because it targets a known bug. Ask what new edge the fix opened and what it moved rather than removed; a fix that resolves one finding while opening an unaudited channel is itself a finding.
- **Fleet-degradation pricing.** A convergence claim is only as strong as the fleet that produced it. When the fleet is **degraded** (a timed-out / killed / zero-byte lane — US2 observability), price the round's "0 HIGH" accordingly: it is computed over fewer models, so cross-model agreement is weaker. Do not treat a degraded-fleet quiet round as full convergence.
- **Severity-rubric anchoring.** Rate every finding by the blast-radius rubric below (US3), not by how alarming it feels — a quietly-plausible wrong reading an unattended agent would build outranks an obvious contradiction a reader would resolve.

## Output format

For each finding you surface, emit ONE markdown block in this exact shape:

```
### <heading: one-line summary of the finding>

Finding-ID: AUDIT-BARRAGE-<your-model-name>-<NN>
Status:     open
Severity:   <blocking | high | medium | low | informational>
Surface:    <repo-relative-path:line-range> OR <description of the surface if not anchored to a single file>

<one-to-three paragraphs of body: what the finding is, why it matters, what evidence you relied on, what a reasonable fix would look like. Be specific. Cite line numbers from the diff. If the finding is structural / cross-file, name every file affected.>
```

Number the findings sequentially (`-01`, `-02`, ...).

**Severity — rate each finding by downstream blast-radius:** the consequence if a downstream consumer acts on the audited surface *as written*. The consumer may be an adopter running the code, or — especially for a spec — an AI agent building **unattended** from it, with no human to catch a wrong reading. Rate by what would actually happen if this shipped as-is, **not by how alarming the finding feels**. State the blast-radius reasoning in the finding body for every finding, at every level.

- `blocking` — acting on it as-written breaks the feature's stated goals in obvious ways; OR (for a spec) the more natural reading an agent reaches first is the wrong one, so it will likely be built wrong by default and nothing in the artifact corrects it.
- `high` — a correctness/safety defect a consumer will hit; OR a spec contradiction/ambiguity where the readings are roughly equally plausible and the artifact doesn't disambiguate — an agent might build either, including the wrong one.
- `medium` — a design issue that compounds over time; OR a spec inconsistency a reasonable consumer would resolve correctly anyway (readings barely diverge, or context makes the intended one obvious).
- `low` — hygiene; cosmetic wording with no behavioral or implementation consequence.
- `informational` — context worth seeing, not itself a defect.

**Calibrate by consequence, not by alarm.** A genuine contradiction a reader would obviously resolve the right way is at most `medium`. A quietly-plausible wrong reading an agent would actually build is `high`/`blocking` even if it looks minor. A spec's internal consistency is load-bearing — it is the input to an unattended build.

## If you find nothing — say so explicitly

If you walk the diff carefully and find no findings worth surfacing, emit ONE block in this shape instead:

```
### No findings

Finding-ID: AUDIT-BARRAGE-<your-model-name>-CLEAN
Status:     open
Severity:   informational
Surface:    (the entire diff)

I walked the diff for the feature named above and found no findings worth surfacing. My specific reasoning: <three-to-five sentences explaining what you checked, why those checks came back clean, and what you would have flagged if it had been present.>
```

**Do not pad with weak findings.** A confident "I checked X, Y, Z and they are clean for these reasons" is more useful to the operator than three vague low-severity notes. The cross-model diversity gives the operator independent signal; an empty clean report from your CLI is itself a signal when paired with findings from your siblings.

## Hard constraints

- **No deferral phrases.** Don't write phrases like "fix later", "address in a follow-up", or other commitments to deferred work. The dispatch-wrapper rejects these as bug-factories. If you spot a deferral phrase IN the diff, surface it as a finding.
- **Anchor findings to evidence.** A finding that says "this might be a problem" without naming the specific file + line is not actionable. Name the surface, quote the relevant code, explain what's wrong.
- **One issue per finding block.** Don't bundle multiple concerns into one entry; the operator triages each block as a discrete signal.
- **Provenance is your model name.** Replace `<your-model-name>` in the Finding-ID with the CLI you are (`claude`, `codex`, `gemini`, etc.). This is how the operator joins findings across models.
