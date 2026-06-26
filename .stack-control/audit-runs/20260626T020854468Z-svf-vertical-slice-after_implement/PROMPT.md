# Audit-barrage — multi-model audit prompt template

You are an **independent audit reviewer** firing as part of a multi-model audit barrage. Your siblings (other CLIs running this same prompt in parallel) emit their own findings independently; the operator triages all of your outputs side-by-side after every model has settled. Your job is to surface the kinds of defects listed under **What to look for** below, in the work product captured under **Under audit**.

You are NOT collaborating with the other models. You write what you see. The cross-model genetic diversity comes from each of you reporting independently.

## Feature under audit

svf-vertical-slice

## Feature scope (workplan / PRD summary)



## Commit subjects in the audited range

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
- 1d366441c57c4606: scripts/check-portability.sh, specs/svf-vertical-slice/tasks.md, tests/CMakeLists.txt, tests/core/no-allocation-test.cpp
- 31c30149ec9faef5: tests/core/parameter-test.cpp, tests/core/svf-test.cpp, tests/core/test-main.cpp, tests/support/allocation-sentinel.cpp, tests/support/allocation-sentinel.h, tests/support/svf-reference.h
- 51a61c640621e280: core/dsp/effect.h, core/dsp/param-id.h, core/dsp/parameter.h, core/dsp/process-context.h, core/dsp/span.h, core/effects/svf/svf-effect.h, core/primitives/svf-primitive.h, external/.gitkeep, host/processor-node/CMakeLists.txt, host/processor-node/processor-node.h
- 561c01cdba330da9: adapters/workbench/parameter-view.cpp, adapters/workbench/parameter-view.h, adapters/workbench/workbench-app.cpp, cmake/CPM.cmake, cmake/dependencies.cmake, cmake/toolchains/daisy.cmake, cmake/toolchains/teensy.cmake, core/dsp/audio-block.h
- d58ba5050d21850a: adapters/plugin/plugin-parameters.cpp, adapters/plugin/plugin-parameters.h, adapters/plugin/plugin-processor.cpp, adapters/plugin/plugin-processor.h, adapters/teensy/CMakeLists.txt, adapters/teensy/teensy-main.cpp, adapters/workbench/CMakeLists.txt, adapters/workbench/audio-source.cpp, adapters/workbench/audio-source.h, adapters/workbench/midi-binding.h

## Chunk 6a56babffbf5b038
Files in scope: .clang-format, .editorconfig, .github/workflows/ci.yml, .gitignore, .stack-control/govern/convergence/impl__design-feature-svf-vertical-slice.json, CMakeLists.txt, CMakePresets.json, README.md, adapters/daisy/CMakeLists.txt, adapters/daisy/daisy-main.cpp, adapters/plugin/CMakeLists.txt

## Diffs

### .clang-format
diff --git a/.clang-format b/.clang-format
new file mode 100644
index 0000000..18b44bf
--- /dev/null
+++ b/.clang-format
@@ -0,0 +1,22 @@
+---
+# acfx C++ style — strict typing, readable modules (Constitution VII).
+Language: Cpp
+BasedOnStyle: LLVM
+Standard: c++20
+ColumnLimit: 100
+IndentWidth: 4
+TabWidth: 4
+UseTab: Never
+AccessModifierOffset: -4
+PointerAlignment: Left
+DerivePointerAlignment: false
+AllowShortFunctionsOnASingleLine: Inline
+AllowShortIfStatementsOnASingleLine: false
+AllowShortLoopsOnASingleLine: false
+BreakBeforeBraces: Attach
+SpaceAfterCStyleCast: true
+SortIncludes: CaseSensitive
+IncludeBlocks: Preserve
+NamespaceIndentation: None
+FixNamespaceComments: true
+...


### .editorconfig
diff --git a/.editorconfig b/.editorconfig
new file mode 100644
index 0000000..2687260
--- /dev/null
+++ b/.editorconfig
@@ -0,0 +1,25 @@
+root = true
+
+[*]
+charset = utf-8
+end_of_line = lf
+insert_final_newline = true
+trim_trailing_whitespace = true
+indent_style = space
+indent_size = 4
+
+[*.{c,cc,cpp,cxx,h,hh,hpp}]
+indent_size = 4
+max_line_length = 100
+
+[*.{cmake,txt}]
+indent_size = 2
+
+[CMakeLists.txt]
+indent_size = 2
+
+[*.{json,yml,yaml}]
+indent_size = 2
+
+[*.md]
+trim_trailing_whitespace = false


### .github/workflows/ci.yml
diff --git a/.github/workflows/ci.yml b/.github/workflows/ci.yml
new file mode 100644
index 0000000..ade25e7
--- /dev/null
+++ b/.github/workflows/ci.yml
@@ -0,0 +1,47 @@
+name: ci
+
+# Explicit, visible quality gates (Constitution II: NO git hooks). CI builds and
+# tests the host core on every change (quickstart Scenario A) and build-checks the
+# desktop targets (Scenario B/C build). Hardware presets are build-checked only
+# where ARM toolchains are provisioned — a deferred follow noted in quickstart.md,
+# so the boundary is visible rather than silently dropped (FR-015).
+
+on:
+  push:
+    branches: [platform-foundation, main]
+  pull_request:
+
+jobs:
+  core-tests:
+    name: Core host tests (Scenario A)
+    runs-on: macos-latest
+    steps:
+      - uses: actions/checkout@v4
+      - name: Configure (test preset)
+        run: cmake --preset test
+      - name: Build
+        run: cmake --build --preset test -j
+      - name: Test
+        run: ctest --preset test
+
+  portability-gate:
+    name: Portability gates
+    runs-on: ubuntu-latest
+    steps:
+      - uses: actions/checkout@v4
+      - name: Run explicit portability checks
+        run: ./scripts/check-portability.sh
+
+  desktop-build:
+    name: Desktop workbench + plugin build (Scenario B/C)
+    runs-on: macos-latest
+    steps:
+      - uses: actions/checkout@v4
+      - name: Install JUCE Linux/macOS build prerequisites
+        run: echo "macOS runner ships the required frameworks"
+      - name: Configure (desktop preset)
+        run: cmake --preset desktop
+      - name: Build workbench
+        run: cmake --build --preset desktop --target acfx_workbench -j
+      - name: Build plugin (VST3 / AU / CLAP)
+        run: cmake --build --preset desktop --target acfx_plugin -j


### .gitignore
diff --git a/.gitignore b/.gitignore
index 304c141..1acd783 100644
--- a/.gitignore
+++ b/.gitignore
@@ -1 +1,25 @@
 .stack-control/state/
+
+# Build output
+build/
+build-*/
+out/
+
+# CPM fetched dependencies + cache
+external/_deps/
+external/.cpm-cache/
+.cpmcache/
+
+# Compiled objects / artifacts
+*.o
+*.a
+*.so
+*.dll
+*.exe
+*.dylib
+
+# Editor / OS
+.DS_Store
+.idea/
+.vscode/
+*.swp


### .stack-control/govern/convergence/impl__design-feature-svf-vertical-slice.json
diff --git a/.stack-control/govern/convergence/impl__design-feature-svf-vertical-slice.json b/.stack-control/govern/convergence/impl__design-feature-svf-vertical-slice.json
new file mode 100644
index 0000000..b09da11
--- /dev/null
+++ b/.stack-control/govern/convergence/impl__design-feature-svf-vertical-slice.json
@@ -0,0 +1,110 @@
+{
+  "version": 1,
+  "mode": "impl",
+  "item": "design:feature/svf-vertical-slice",
+  "governedShaBase": "ff3426a",
+  "headSha": "HEAD",
+  "chunkIds": [
+    "1d366441c57c4606",
+    "31c30149ec9faef5",
+    "51a61c640621e280",
+    "561c01cdba330da9",
+    "6a56babffbf5b038",
+    "d58ba5050d21850a"
+  ],
+  "rounds": 1,
+  "liftedFindings": [
+    {
+      "id": "1d366441c57c4606::AUDIT-BARRAGE-codex-01",
+      "title": "Completed tasks still contain pending or blocked acceptance",
+      "severity": "high"
+    },
+    {
+      "id": "51a61c640621e280::AUDIT-BARRAGE-claude-01",
+      "title": "`clamp01` passes NaN/non-finite values straight through, poisoning RT filter state irrecoverably",
+      "severity": "high"
+    },
+    {
+      "id": "561c01cdba330da9::AUDIT-BARRAGE-claude-02",
+      "title": "ProcessContext is prepared for a hardcoded 2 channels but process() drives up to 8 channels from the live buffer",
+      "severity": "high"
+    },
+    {
+      "id": "d58ba5050d21850a::AUDIT-BARRAGE-claude-01",
+      "title": "Teensy mode-pin normalization produces `norm = 1.0`, potentially yielding out-of-range discrete index",
+      "severity": "high"
+    }
+  ],
+  "closedInLoopFindings": [],
+  "seamResult": {
+    "boundaryPairs": [
+      {
+        "a": "1d366441c57c4606",
+        "b": "31c30149ec9faef5"
+      },
+      {
+        "a": "1d366441c57c4606",
+        "b": "51a61c640621e280"
+      },
+      {
+        "a": "1d366441c57c4606",
+        "b": "561c01cdba330da9"
+      },
+      {
+        "a": "1d366441c57c4606",
+        "b": "6a56babffbf5b038"
+      },
+      {
+        "a": "1d366441c57c4606",
+        "b": "d58ba5050d21850a"
+      },
+      {
+        "a": "31c30149ec9faef5",
+        "b": "51a61c640621e280"
+      },
+      {
+        "a": "31c30149ec9faef5",
+        "b": "561c01cdba330da9"
+      },
+      {
+        "a": "31c30149ec9faef5",
+        "b": "6a56babffbf5b038"
+      },
+      {
+        "a": "31c30149ec9faef5",
+        "b": "d58ba5050d21850a"
+      },
+      {
+        "a": "51a61c640621e280",
+        "b": "561c01cdba330da9"
+      },
+      {
+        "a": "51a61c640621e280",
+        "b": "6a56babffbf5b038"
+      },
+      {
+        "a": "51a61c640621e280",
+        "b": "d58ba5050d21850a"
+      },
+      {
+        "a": "561c01cdba330da9",
+        "b": "6a56babffbf5b038"
+      },
+      {
+        "a": "561c01cdba330da9",
+        "b": "d58ba5050d21850a"
+      },
+      {
+        "a": "6a56babffbf5b038",
+        "b": "d58ba5050d21850a"
+      }
+    ],
+    "findings": [],
+    "suppressedCompatible": 0
+  },
+  "splitClusterRefs": [
+    "3a670837f5ad29e0"
+  ],
+  "outcome": "override-eligible",
+  "anchorRoot": "/Users/orion/work/acfx-work/platform-foundation"
+}


### CMakeLists.txt
diff --git a/CMakeLists.txt b/CMakeLists.txt
new file mode 100644
index 0000000..6deb126
--- /dev/null
+++ b/CMakeLists.txt
@@ -0,0 +1,75 @@
+cmake_minimum_required(VERSION 3.21)
+
+# acfx — cross-platform audio DSP platform.
+# One platform-independent core, four thin target adapters. Dependencies point
+# only inward (adapters -> core; core -> nothing platform-specific).
+project(acfx VERSION 0.1.0 LANGUAGES C CXX)
+
+# ---------------------------------------------------------------------------
+# Build-surface options. Each preset (CMakePresets.json) flips exactly the set
+# of targets it needs; dependencies are fetched lazily to match (see
+# cmake/dependencies.cmake), so the `test` preset stays lightweight.
+# ---------------------------------------------------------------------------
+option(ACFX_BUILD_TESTS   "Build the host-side doctest suite for core/"      OFF)
+option(ACFX_BUILD_DESKTOP "Build the JUCE workbench + plugin adapters"       OFF)
+option(ACFX_BUILD_DAISY   "Build the Daisy (libDaisy) adapter"               OFF)
+option(ACFX_BUILD_TEENSY  "Build the Teensy adapter"                         OFF)
+
+# Keep CPM's source cache inside the repo (gitignored) for reproducible fetches.
+if(NOT DEFINED CPM_SOURCE_CACHE)
+  set(CPM_SOURCE_CACHE "${CMAKE_SOURCE_DIR}/external/.cpm-cache")
+endif()
+
+# ---------------------------------------------------------------------------
+# C++ standard. The core compiles under both C++17 and C++20; the Effect/Generator
+# concept contract-checks are guarded by __cpp_concepts in the headers, so the
+# same source compiles either way. Desktop/test/Daisy build at C++20 (named
+# concept checks); Teensy may be pinned lower by its toolchain file.
+# ---------------------------------------------------------------------------
+if(NOT DEFINED CMAKE_CXX_STANDARD)
+  set(CMAKE_CXX_STANDARD 20)
+endif()
+set(CMAKE_CXX_STANDARD_REQUIRED ON)
+set(CMAKE_CXX_EXTENSIONS OFF)
+
+include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/dependencies.cmake)
+
+# ---------------------------------------------------------------------------
+# acfx_core — the platform-independent spine. Header-only (INTERFACE): the DSP
+# core, the Effect concept, the parameter model, and the SVF effect all live in
+# headers under core/. It links DaisySP because core/primitives wraps DaisySP's
+# Svf (a pure-DSP, platform-independent math library — Constitution IV permits
+# the inward wrap).
+# ---------------------------------------------------------------------------
+add_library(acfx_core INTERFACE)
+add_library(acfx::core ALIAS acfx_core)
+target_include_directories(acfx_core INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/core")
+target_compile_features(acfx_core INTERFACE cxx_std_17)
+if(TARGET DaisySP)
+  target_link_libraries(acfx_core INTERFACE DaisySP)
+endif()
+
+# ---------------------------------------------------------------------------
+# Subdirectories, gated by the build-surface options.
+# ---------------------------------------------------------------------------
+if(ACFX_BUILD_DESKTOP)
+  add_subdirectory(host/processor-node)
+  add_subdirectory(adapters/workbench)
+  add_subdirectory(adapters/plugin)
+endif()
+
+if(ACFX_BUILD_DAISY)
+  add_subdirectory(adapters/daisy)
+endif()
+
+if(ACFX_BUILD_TEENSY)
+  add_subdirectory(adapters/teensy)
+endif()
+
+if(ACFX_BUILD_TESTS)
+  enable_testing()
+  # host/processor-node is desktop-only host code, but it is pure C++ (no JUCE),
+  # so the test build includes it to exercise the EffectNode boundary too.
+  add_subdirectory(host/processor-node)
+  add_subdirectory(tests)
+endif()


### CMakePresets.json
diff --git a/CMakePresets.json b/CMakePresets.json
new file mode 100644
index 0000000..6cc56cb
--- /dev/null
+++ b/CMakePresets.json
@@ -0,0 +1,67 @@
+{
+  "version": 3,
+  "cmakeMinimumRequired": { "major": 3, "minor": 21, "patch": 0 },
+  "configurePresets": [
+    {
+      "name": "base",
+      "hidden": true,
+      "generator": "Unix Makefiles",
+      "binaryDir": "${sourceDir}/build/${presetName}",
+      "cacheVariables": {
+        "CMAKE_BUILD_TYPE": "RelWithDebInfo",
+        "CMAKE_EXPORT_COMPILE_COMMANDS": "ON"
+      }
+    },
+    {
+      "name": "test",
+      "inherits": "base",
+      "displayName": "Host tests (core correctness, no hardware)",
+      "cacheVariables": {
+        "ACFX_BUILD_TESTS": "ON",
+        "CMAKE_CXX_STANDARD": "20"
+      }
+    },
+    {
+      "name": "desktop",
+      "inherits": "base",
+      "displayName": "Desktop workbench + plugin (JUCE)",
+      "cacheVariables": {
+        "ACFX_BUILD_DESKTOP": "ON",
+        "CMAKE_CXX_STANDARD": "20"
+      }
+    },
+    {
+      "name": "daisy",
+      "inherits": "base",
+      "displayName": "Daisy firmware (arm-none-eabi-gcc)",
+      "toolchainFile": "${sourceDir}/cmake/toolchains/daisy.cmake",
+      "cacheVariables": {
+        "ACFX_BUILD_DAISY": "ON",
+        "CMAKE_CXX_STANDARD": "20"
+      }
+    },
+    {
+      "name": "teensy",
+      "inherits": "base",
+      "displayName": "Teensy firmware (Teensy ARM toolchain)",
+      "toolchainFile": "${sourceDir}/cmake/toolchains/teensy.cmake",
+      "cacheVariables": {
+        "ACFX_BUILD_TEENSY": "ON"
+      }
+    }
+  ],
+  "buildPresets": [
+    { "name": "test", "configurePreset": "test" },
+    { "name": "desktop", "configurePreset": "desktop" },
+    { "name": "daisy", "configurePreset": "daisy" },
+    { "name": "teensy", "configurePreset": "teensy" }
+  ],
+  "testPresets": [
+    {
+      "name": "test",
+      "configurePreset": "test",
+      "output": { "outputOnFailure": true },
+      "execution": { "noTestsAction": "error", "stopOnFailure": false }
+    }
+  ]
+}


### README.md
diff --git a/README.md b/README.md
new file mode 100644
index 0000000..76d746c
--- /dev/null
+++ b/README.md
@@ -0,0 +1,92 @@
+# acfx
+
+A cross-platform audio DSP platform: one platform-independent core, many thin
+target adapters. Effects are written once and run as a desktop standalone
+workbench, a DAW plugin (VST3 / AU / CLAP), and microcontroller firmware
+(Daisy, Teensy) — from the *same* source, with one parameter declaration driving
+every adapter.
+
+This repository's first vertical slice proves that spine end-to-end with a single
+effect: a State-Variable Filter (SVF). See
+[`specs/svf-vertical-slice/`](specs/svf-vertical-slice/) for the spec, plan, and
+the runnable validation guide ([`quickstart.md`](specs/svf-vertical-slice/quickstart.md)).
+
+## Layout
+
+```
+core/        platform-independent spine — NO JUCE/libDaisy/Teensy headers
+  dsp/         Effect concept, ProcessContext, AudioBlock, parameter model
+  primitives/  thin wrappers over DaisySP (the SVF wrapper)
+  effects/svf/ the SVF effect + its constexpr parameter table
+host/          desktop-only ProcessorNode boundary (<= 1 virtual call / block)
+adapters/      workbench (JUCE app), plugin (JUCE VST3/AU/CLAP), daisy, teensy
+tests/         host-side doctest suite (core correctness, no hardware)
+cmake/         CPM + pinned dependencies + ARM toolchain files
+```
+
+Dependencies point only inward (`adapters/* -> core/*`; `core/*` depends on
+nothing platform-specific). The same `core/effects/svf` source compiles into every
+target with no per-target `#ifdef` forks.
+
+## Build & run
+
+Builds use CMake presets. Each preset fetches only the dependencies it needs
+(CPM-pinned in [`cmake/dependencies.cmake`](cmake/dependencies.cmake)).
+
+### Host tests (no hardware) — quickstart Scenario A
+
+```bash
+cmake --preset test
+cmake --build --preset test
+ctest --preset test
+```
+
+Runs parameter scaling/skew checks, the per-mode SVF frequency-response check, the
+high-resonance stability guard, and the no-heap-allocation-in-`process()`
+invariant.
+
+### Desktop workbench (sketch-and-hear) — Scenario B
+
+```bash
+cmake --preset desktop
+cmake --build --preset desktop --target acfx_workbench
+```
+
+Launch the built app: auto-generated controls for cutoff / resonance / mode, a
+built-in player or live input, a bound MIDI CC, and a dry/processed A/B toggle.
+
+### Desktop plugin (VST3 / AU / CLAP) — Scenario C
+
+```bash
+cmake --preset desktop
+cmake --build --preset desktop --target acfx_plugin
+```
+
+### Hardware cross-compile — Scenario D
+
+```bash
+cmake --preset daisy  && cmake --build --preset daisy
+cmake --preset teensy && cmake --build --preset teensy
+```
+
+Requires an ARM embedded toolchain **with the C++ standard library** (ARM's
+gcc-arm-embedded or the vendor toolchain). Flashing and listening on a physical
+board is a separate checkpoint when hardware is in hand.
+
+## Quality gates
+
+Quality gates are **explicit, visible steps — never git hooks** (this repo uses
+zero hooks). Run them on purpose:
+
+```bash
+./scripts/check-portability.sh    # file-size budget, core platform-independence,
+                                  # no-JUCE-in-MCU, one-source-many-targets
+```
+
+CI ([`.github/workflows/ci.yml`](.github/workflows/ci.yml)) runs the host tests,
+the portability gates, and the desktop build on every change.
+
+## Standards
+
+See [`CLAUDE.md`](CLAUDE.md) and the full project constitution at
+[`.specify/memory/constitution.md`](.specify/memory/constitution.md).


### adapters/daisy/CMakeLists.txt
diff --git a/adapters/daisy/CMakeLists.txt b/adapters/daisy/CMakeLists.txt
new file mode 100644
index 0000000..795dba5
--- /dev/null
+++ b/adapters/daisy/CMakeLists.txt
@@ -0,0 +1,30 @@
+# Daisy firmware target (T032). Compiles the SAME core/effects/svf into a Daisy
+# firmware via libDaisy — the only platform-specific code is daisy-main.cpp
+# (audio callback + ADC). No JUCE, no ProcessorNode (Constitution IV; SC-007).
+#
+# Requires the ARM embedded toolchain WITH the C++ standard library (ARM's
+# gcc-arm-embedded or the Daisy toolchain). A C-only arm-none-eabi (no libstdc++)
+# cannot build the C++ core — that is surfaced as a hard error, never a fallback.
+
+if(NOT DEFINED libDaisy_SOURCE_DIR)
+  message(FATAL_ERROR
+    "libDaisy not fetched. Configure with the `daisy` preset so cmake/dependencies.cmake "
+    "pins and downloads libDaisy.")
+endif()
+
+# libDaisy ships its own CMake that defines the `daisy` static library + the
+# STM32H750 build flags and linker script.
+add_subdirectory("${libDaisy_SOURCE_DIR}" "${CMAKE_BINARY_DIR}/libDaisy" EXCLUDE_FROM_ALL)
+
+add_executable(acfx_daisy daisy-main.cpp)
+target_compile_features(acfx_daisy PRIVATE cxx_std_20)
+target_link_libraries(acfx_daisy PRIVATE acfx_core daisy)
+
+# Daisy bootloader linker script (STM32H750IB internal flash).
+set(_daisy_lds "${libDaisy_SOURCE_DIR}/core/STM32H750IB_flash.lds")
+target_link_options(acfx_daisy PRIVATE
+  -T "${_daisy_lds}"
+  -Wl,-Map=acfx_daisy.map,--cref
+  -Wl,--gc-sections
+)
+set_target_properties(acfx_daisy PROPERTIES SUFFIX ".elf")


### adapters/daisy/daisy-main.cpp
diff --git a/adapters/daisy/daisy-main.cpp b/adapters/daisy/daisy-main.cpp
new file mode 100644
index 0000000..11d04dd
--- /dev/null
+++ b/adapters/daisy/daisy-main.cpp
@@ -0,0 +1,81 @@
+// Daisy adapter (T032): the libDaisy audio callback drives the SAME
+// core/effects/svf source, and the Seed's ADC knobs map to setParameter. The MCU
+// build uses the concrete SvfEffect directly — no ProcessorNode, no JUCE
+// (Constitution IV; SC-007). Only the audio-callback + control glue is
+// platform-specific; the effect is identical to the desktop targets.
+
+#include <cstdint>
+
+#include "daisy_seed.h"
+
+#include "dsp/audio-block.h"
+#include "dsp/param-id.h"
+#include "dsp/process-context.h"
+#include "effects/svf/svf-effect.h"
+
+using namespace daisy;
+
+namespace {
+
+DaisySeed hw;
+acfx::SvfEffect svf;
+
+// ADC channel assignment (Seed analog pins): cutoff, resonance, mode.
+enum AdcChannel { kAdcCutoff = 0, kAdcResonance = 1, kAdcMode = 2, kAdcCount };
+
+// Last-published knob values; only re-publish past a dead-band so the effect
+// recomputes coefficients when a knob actually moves, not on every block.
+float lastKnob[kAdcCount] = {-1.0f, -1.0f, -1.0f};
+constexpr float kKnobDeadband = 0.002f;
+
+void maybeSet(acfx::SvfEffect::Param param, int adc) {
+    const float v = hw.adc.GetFloat(adc);
+    if (v < lastKnob[adc] - kKnobDeadband || v > lastKnob[adc] + kKnobDeadband) {
+        lastKnob[adc] = v;
+        svf.setParameter(acfx::ParamId{static_cast<std::uint8_t>(param)}, v);
+    }
+}
+
+void AudioCallback(AudioHandle::InputBuffer in, AudioHandle::OutputBuffer out, size_t size) {
+    // Map the knobs to normalized parameter values (the effect denormalizes via
+    // its descriptor — identical mapping to every other adapter). Dead-banded so
+    // a steady knob does not trigger a coefficient recompute every block.
+    maybeSet(acfx::SvfEffect::kCutoff, kAdcCutoff);
+    maybeSet(acfx::SvfEffect::kResonance, kAdcResonance);
+    maybeSet(acfx::SvfEffect::kMode, kAdcMode);
+
+    // libDaisy hands non-interleaved per-channel buffers. Copy in -> out, then
+    // process out in place through the shared core.
+    float* channels[2] = {out[0], out[1]};
+    for (size_t i = 0; i < size; ++i) {
+        out[0][i] = in[0][i];
+        out[1][i] = in[1][i];
+    }
+    acfx::AudioBlock block(channels, 2, static_cast<int>(size));
+    svf.process(block);
+}
+
+} // namespace
+
+int main() {
+    hw.Configure();
+    hw.Init();
+    hw.SetAudioBlockSize(48);
+
+    // Configure the three ADC knobs.
+    AdcChannelConfig adcConfig[kAdcCount];
+    adcConfig[kAdcCutoff].InitSingle(seed::A0);
+    adcConfig[kAdcResonance].InitSingle(seed::A1);
+    adcConfig[kAdcMode].InitSingle(seed::A2);
+    hw.adc.Init(adcConfig, kAdcCount);
+    hw.adc.Start();
+
+    const acfx::ProcessContext ctx{static_cast<double>(hw.AudioSampleRate()),
+                                   static_cast<int>(hw.AudioBlockSize()), 2};
+    svf.prepare(ctx);
+
+    hw.StartAudio(AudioCallback);
+    for (;;) {
+        // Parameters are sampled in the audio callback; nothing to do here.
+    }
+}


### adapters/plugin/CMakeLists.txt
diff --git a/adapters/plugin/CMakeLists.txt b/adapters/plugin/CMakeLists.txt
new file mode 100644
index 0000000..1e9cebe
--- /dev/null
+++ b/adapters/plugin/CMakeLists.txt
@@ -0,0 +1,44 @@
+# The DAW plugin (T028): one JUCE plugin target exported as VST3 + AU natively and
+# CLAP via clap-juce-extensions. A thin adapter — links acfx_core + acfx_host (the
+# same boundary the workbench uses) and adds only JUCE plugin glue. Built under the
+# `desktop` preset.
+
+juce_add_plugin(acfx_plugin
+  PRODUCT_NAME "acfx SVF"
+  COMPANY_NAME "acfx"
+  PLUGIN_MANUFACTURER_CODE Acfx
+  PLUGIN_CODE Asvf
+  FORMATS VST3 AU
+  IS_SYNTH FALSE
+  NEEDS_MIDI_INPUT FALSE
+  NEEDS_MIDI_OUTPUT FALSE
+  COPY_PLUGIN_AFTER_BUILD FALSE
+)
+
+target_sources(acfx_plugin PRIVATE
+  plugin-processor.cpp
+  plugin-parameters.cpp
+)
+
+target_compile_features(acfx_plugin PRIVATE cxx_std_20)
+
+target_compile_definitions(acfx_plugin PUBLIC
+  JUCE_WEB_BROWSER=0
+  JUCE_USE_CURL=0
+  JUCE_VST3_CAN_REPLACE_VST2=0
+  JUCE_DISPLAY_SPLASH_SCREEN=0
+)
+
+target_link_libraries(acfx_plugin PRIVATE
+  acfx_core
+  acfx_host
+  juce::juce_audio_utils
+  juce::juce_recommended_config_flags
+  juce::juce_recommended_warning_flags
+)
+
+# Add the CLAP format to the same target (research.md decision 7).
+clap_juce_extensions_plugin(TARGET acfx_plugin
+  CLAP_ID "org.acfx.svf"
+  CLAP_FEATURES audio-effect filter
+)


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
