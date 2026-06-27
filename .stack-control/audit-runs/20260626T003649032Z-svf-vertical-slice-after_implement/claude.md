I'll perform the independent audit of my assigned chunk (`561c01cdba330da9`) and emit findings in the required format.

### Asymmetric C++ standard handling between sibling toolchains: daisy.cmake sets none, teensy.cmake pins it

Finding-ID: AUDIT-BARRAGE-claude-01
Status:     open
Severity:   medium
Surface:    cmake/toolchains/daisy.cmake:23-30 vs cmake/toolchains/teensy.cmake:26-31

`teensy.cmake` explicitly establishes the language level (`set(CMAKE_CXX_STANDARD ${ACFX_TEENSY_CXX_STANDARD})` with a default of 17), but the sibling `daisy.cmake` sets **no** `CMAKE_CXX_STANDARD` at all — it only sets CPU/float flags. Both target a Cortex-M7. The feature's central claim is "core proven ARM-portable," and the core's `Effect` contract reportedly degrades a C++20-concepts path under `__cpp_concepts`. The Daisy build will therefore compile core/ at whatever `arm-none-eabi-g++`'s *default* dialect is (gnu++14 on older GCC, gnu++17 on newer), not a project-pinned level.

The blast radius: a downstream adopter building the `daisy` preset on a toolchain whose default predates C++17 gets either a hard compile failure or a silently-lower dialect than the host/teensy builds use — exactly the kind of per-target divergence a "portability" slice exists to prevent. If the root `CMakeLists.txt` (chunk `6a56babffbf5b038`, not visible here) sets `CMAKE_CXX_STANDARD` globally, then daisy is fine and teensy's explicit set is redundant/conflicting instead — either way the two sibling toolchains treating the language level asymmetrically is a smell that should be reconciled. A fix sets the standard identically (or defers to a single global) in both toolchain files.

### teensy.cmake comment claims toolchain-max standard auto-detection that the code does not perform

Finding-ID: AUDIT-BARRAGE-claude-02
Status:     open
Severity:   medium
Surface:    cmake/toolchains/teensy.cmake:6-9, 26-31

The header comment states `ACFX_TEENSY_CXX_STANDARD` "is set to the highest standard that toolchain supports (>= 17)" and line 30's inline comment says it is "Raised here if the installed toolchain supports more." Neither is true: the code does a plain `if(NOT DEFINED ACFX_TEENSY_CXX_STANDARD) set(... 17)`. There is no probe of the installed compiler, no `check_cxx_compiler_flag`, no negotiation — it is a hardcoded default of 17 with an external override hook. Nothing "raises" anything.

This is documentation drift that misleads an unattended reader/agent into believing the build adapts to the toolchain. Blast radius: if a future effect needs the C++20 concepts path on Teensy, a maintainer trusting this comment will assume the standard already auto-bumped and will not understand why concepts are silently disabled (it stays at 17). The fix is to either implement actual detection or correct the comment to say "defaults to 17; override `ACFX_TEENSY_CXX_STANDARD` to raise it."

### Exception-throwing control flow inside the RT audio callback violates the no-alloc process() rule

Finding-ID: AUDIT-BARRAGE-claude-03
Status:     open
Severity:   medium
Surface:    adapters/workbench/workbench-app.cpp:97-108

`getNextAudioBlock` wraps `source_.fillBlock(region)` in `try { ... } catch (const AudioSourceError&) { buffer.clear(...); return; }`. This is the audio-callback path, which the constitution and CLAUDE.md mandate be free of heap allocation and locks. Throwing a C++ exception is not RT-safe: `__cxa_allocate_exception` falls back to `malloc` when the per-thread emergency buffer is exhausted, and stack unwinding through JUCE frames is unbounded-time. If a source enters a persistent error state, it throws *every block*, producing per-callback allocation and unwinding on the audio thread — precisely the priority-inversion/glitch hazard the RT rule exists to prevent.

The whole point of `AudioSourceError` as an exception is steady-state-clean, error-path-only — but the project's rule is absolute about the callback path, and the most recent commit (`bd79479`) was explicitly an RT-safety pass, so this surface is in scope. A fix uses a non-throwing fill API on the audio thread (e.g. `fillBlock` returns a status / fills silence internally) and reserves exceptions for `prepareToPlay`, where `lastSourceError_` is already captured that way.

### Dead member `params_` in WorkbenchComponent

Finding-ID: AUDIT-BARRAGE-claude-04
Status:     open
Severity:   low
Surface:    adapters/workbench/workbench-app.cpp:38, 145

`span<const ParameterDescriptor> params_;` is declared (line 145) and assigned `params_ = node_->parameters();` in the constructor body (line 38), but it is never read anywhere in the file — `paramView_` is constructed directly from `node_->parameters()`, and MIDI/GUI paths use `node_`/`midi_` directly. This is dead state carrying a span into the descriptor table for no consumer. Hygiene only — but a stored span is a latent footgun (it implies a dependency on the referent's lifetime that no one actually needs), so removing it both clarifies intent and drops a dangling-reference surface. Remove the member and its assignment.

### parameter-view discrete reverse-mapping can select a non-existent combo index for degenerate discreteCount

Finding-ID: AUDIT-BARRAGE-claude-05
Status:     open
Severity:   low
Surface:    adapters/workbench/parameter-view.cpp:62-71 vs 22-25

`setNormalized` forces `count = discreteCount < 2 ? 2 : discreteCount` and then `setSelectedItemIndex(index)` clamped to `count - 1`. When `discreteCount == 1`, the constructor (lines 22-25) adds exactly **one** combo item (index 0 only), but `setNormalized` computes against `count = 2`, so a normalized value ≥ 0.5 yields `index = 1`, clamped to `count - 1 = 1` — and `setSelectedItemIndex(1)` references an item that does not exist. The constructor path also lacks the `< 2` guard that `setNormalized` applies, so the two halves disagree about the legal bucket range. Separately, `discreteCount == 0` makes the constructor's `norm = (index + 0.5f) / count` a divide-by-zero if ever fired.

Discrete params with fewer than two values are degenerate, so the practical blast radius is small — but the inconsistency (forward path uses raw `discreteCount`, reverse path floors it to 2) is the kind of asymmetry that produces a wrong-control-state bug the moment such a descriptor appears, with no test fixture covering it. A fix shares one clamp helper between the constructor and `setNormalized` and guards `discreteCount < 1` at descriptor-build time.