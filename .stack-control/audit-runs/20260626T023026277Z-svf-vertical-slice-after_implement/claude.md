I'll review chunk `6a56babffbf5b038` as an independent audit reviewer and emit findings anchored to the diff.

### README Scenario C tells users to build `acfx_plugin`, which (per CI's own comment) produces no plugin bundles

Finding-ID: AUDIT-BARRAGE-claude-01
Status:     open
Severity:   high
Surface:    README.md:73-76 (Scenario C block) vs `.github/workflows/ci.yml:46-49` and `adapters/plugin/CMakeLists.txt`

The lifted finding `6a56babffbf5b038::…CI builds the shared-code target, not the plugin formats` was fixed **only in CI** — `ci.yml:49` now builds `acfx_plugin_VST3 acfx_plugin_AU acfx_plugin_CLAP`, and the CI comment at `ci.yml:46-48` explicitly states: *"The aggregate acfx_plugin target builds only the shared code; the format wrappers are separate targets that produce the actual bundles."* The README was not updated to match. README Scenario C (`README.md:75-76`) still instructs:

```
cmake --build --preset desktop --target acfx_plugin
```

By the workflow's own documented semantics, this builds the shared-code object library and produces **no VST3/AU/CLAP bundle**. Blast radius: a user or unattended agent following the README's "Desktop plugin (VST3 / AU / CLAP)" section runs a build that succeeds, sees no error, and concludes the plugin built — then finds nothing to load in a DAW. This is the round-0 self-red-team case: the fix for the CI finding closed the CI surface while leaving the sibling README surface holding the exact shape the finding identified. Fix: change README Scenario C to build the format targets (`--target acfx_plugin_VST3 acfx_plugin_AU acfx_plugin_CLAP`), matching CI.

### CI claims hardware-preset coverage via a deferral phrase, but no job build-checks the Daisy/Teensy adapters

Finding-ID: AUDIT-BARRAGE-claude-02
Status:     open
Severity:   medium
Surface:    .github/workflows/ci.yml:5-8 (header comment) and the absent daisy/teensy jobs

The workflow header comment (`ci.yml:7-8`) reads: *"Hardware presets are build-checked only where ARM toolchains are provisioned — **a deferred follow** noted in quickstart.md, so the boundary is visible rather than silently dropped (FR-015)."* Two problems. (1) "a deferred follow" is a deferral phrase of the kind the dispatch-wrapper rejects as a bug-factory; the hard constraints direct surfacing it when it appears in the diff. (2) More substantively, the comment frames the hardware boundary as "visible" — but there is **no daisy/teensy job at all**, not even a conditional/guarded one. The three jobs are `core-tests`, `portability-gate`, and `desktop-build`. So any portability regression in `adapters/daisy/daisy-main.cpp` or the Teensy adapter (a wrong `#include`, an API drift, a C++20 feature the toolchain rejects) ships uncaught.

Blast radius: a consumer reading FR-015 + this comment reasonably assumes hardware compilation is gated somewhere; it isn't. "Visible as prose" is not a gate. A reasonable fix is either an `arm-none-eabi`-toolchain job that runs `cmake --preset daisy && cmake --build --preset daisy` (the daisy CMakeLists already fails loud if libDaisy is absent, so the gate degrades cleanly), or — if genuinely out of scope this round — replacing the deferral phrasing with a concrete tracked obligation rather than a prose hand-wave.

### `acfx_core` silently skips linking DaisySP via `if(TARGET DaisySP)`, degrading a missing dependency into a cryptic compile error

Finding-ID: AUDIT-BARRAGE-claude-03
Status:     open
Severity:   medium
Surface:    CMakeLists.txt:57-62 (`if(TARGET DaisySP) target_link_libraries(...)`)

`acfx_core` is described (CMakeLists.txt:48-52) as wrapping DaisySP's `Svf`, so `core/primitives/svf-primitive.h` includes DaisySP headers unconditionally. But the link is guarded:

```cmake
if(TARGET DaisySP)
  target_link_libraries(acfx_core INTERFACE DaisySP)
endif()
```

If `cmake/dependencies.cmake` fails to define the `DaisySP` target for any preset (cache miss, fetch failure, a preset that forgets to request it), this guard silently skips the link and the include directories. The build then fails much later with a cryptic `daisysp.h: No such file or directory` deep in core, instead of a clear statement that DaisySP wasn't fetched. This is a soft-fallback that hides a failure mode — exactly what the project standard forbids ("raise descriptive errors for missing functionality instead"). It is also inconsistent with the sibling `adapters/daisy/CMakeLists.txt:9-12`, which does the right thing: `message(FATAL_ERROR ...)` when `libDaisy_SOURCE_DIR` is undefined. Blast radius: confusing build breakage that an operator must reverse-engineer rather than read. Fix: replace the silent guard with an `else()` branch emitting `message(FATAL_ERROR "DaisySP not fetched; see cmake/dependencies.cmake")`, mirroring the daisy adapter.

### CI `desktop-build` "Install JUCE … prerequisites" step is a no-op echo, and Linux desktop configuration is never exercised

Finding-ID: AUDIT-BARRAGE-claude-04
Status:     open
Severity:   low
Surface:    .github/workflows/ci.yml:38-41 (desktop-build runner + install step)

The step named `Install JUCE Linux/macOS build prerequisites` (`ci.yml:40-41`) runs only `echo "macOS runner ships the required frameworks"` on a `macos-latest` runner — it installs nothing and the "Linux" half of its name is never honored anywhere in the workflow (the only Linux job, `portability-gate`, runs a shell script, not a build). The name overstates what the step does and implies Linux desktop coverage that doesn't exist. Blast radius is low — it's a CI hygiene/labeling issue, not a correctness defect a consumer's build hits — but a reader scanning CI to confirm cross-platform desktop builds are validated would be misled. Fix: rename the step to reflect that it is a macOS-only no-op (or drop it), and, if Linux desktop support is intended, add an actual Linux configure/build job.

---

I walked the rest of the chunk and found the following clean: `.clang-format`/`.editorconfig` are plain style config with no behavioral consequence; `.gitignore` correctly covers `build/`, `external/.cpm-cache`, and `external/_deps` matching the binaryDir and `CPM_SOURCE_CACHE` paths in CMakeLists.txt; `CMakePresets.json` target/standard wiring is internally consistent (Teensy intentionally defers `CMAKE_CXX_STANDARD` to its toolchain, matching the `if(NOT DEFINED ...)` guard); the `adapters/plugin/CMakeLists.txt` format-target names (`acfx_plugin_VST3/_AU/_CLAP`) match what CI builds; and the daisy mode-knob out-of-range exposure is already lifted (`…claude-02`), so I did not re-report it. I did **not** re-report the CI plugin-format finding itself, since its CI-side fix is present in this diff — finding -01 instead targets the README surface the fix failed to propagate to.