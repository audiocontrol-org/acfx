### CI builds the JUCE shared-code target, not the plugin format wrappers it claims to validate

Finding-ID: AUDIT-BARRAGE-claude-01
Status:     open
Severity:   medium
Surface:    .github/workflows/ci.yml:44-47 (with adapters/plugin/CMakeLists.txt:7-13,42-44)

The `desktop-build` job's final step is named "Build plugin (VST3 / AU / CLAP)" and runs `cmake --build --preset desktop --target acfx_plugin`. Under JUCE's CMake API, `juce_add_plugin(acfx_plugin ...)` creates `acfx_plugin` as the **shared-code** target; the actual format binaries are separate targets (`acfx_plugin_VST3`, `acfx_plugin_AU`, and the CLAP wrapper from `clap_juce_extensions_plugin`), aggregated under `acfx_plugin_All`. Building `--target acfx_plugin` compiles `plugin-processor.cpp` / `plugin-parameters.cpp` (so it catches the kind of JUCE-API compile bugs commit 60b4523 fixed) but does **not** link the VST3/AU/CLAP wrappers — so the CLAP extension glue and the format-specific entry points are never exercised by CI.

Blast radius: a downstream maintainer reads the green check on a step labeled "VST3 / AU / CLAP" and concludes all three formats build. A regression in the `clap_juce_extensions_plugin` wiring or in format-specific code would ship undetected, because the step that claims to cover it never builds it. The fix is to target `acfx_plugin_All` (or enumerate the per-format targets), and ideally name the step for what it actually does.

### `acfx_core` silently skips linking DaisySP via `if(TARGET DaisySP)`, contradicting the no-fallback rule the daisy adapter follows

Finding-ID: AUDIT-BARRAGE-claude-02
Status:     open
Severity:   medium
Surface:    CMakeLists.txt:54-57

The core is documented (CMakeLists.txt:47-52) as wrapping DaisySP's `Svf` — i.e. DaisySP is a *required* dependency of `acfx_core`, not optional. Yet the link is gated `if(TARGET DaisySP) target_link_libraries(acfx_core INTERFACE DaisySP) endif()`. If DaisySP is ever not fetched for a given preset, `acfx_core` configures cleanly and instead fails much later with an opaque undefined-symbol link error from `core/primitives/svf-primitive.h`, rather than a clear "DaisySP not fetched — configure with a preset that pins it." This is exactly the silent-degradation pattern the project bans ("raise descriptive errors for missing functionality"), and it is **inconsistent with the sibling adapter**: adapters/daisy/CMakeLists.txt:11-14 correctly emits `FATAL_ERROR` when `libDaisy_SOURCE_DIR` is undefined.

Blast radius: an adopter (or the `test` preset, if its dependency wiring drops DaisySP) gets a confusing linker error instead of a configure-time diagnostic, costing debugging time and obscuring the real cause. The fix is to make the missing target a hard `FATAL_ERROR` mirroring the daisy adapter, since the core genuinely cannot build without it.

### Desktop build runs only on macOS while its prerequisite step claims Linux coverage and installs nothing

Finding-ID: AUDIT-BARRAGE-claude-03
Status:     open
Severity:   medium
Surface:    .github/workflows/ci.yml:33-42

The `desktop-build` job runs on `macos-latest`, but its first step is named "Install JUCE Linux/macOS build prerequisites" and its body is a no-op: `run: echo "macOS runner ships the required frameworks"`. Two problems compound here. First, the step name advertises Linux coverage that does not exist — the JUCE workbench/plugin are never compiled on Linux anywhere in CI (the `portability-gate` job on `ubuntu-latest` only runs `check-portability.sh`, a static shell check, not a build). Second, a step literally named "Install … prerequisites" that installs nothing is a placeholder masquerading as a gate.

Blast radius: the project's central thesis is "one source, many targets," and the README presents desktop builds as cross-platform. A Linux-specific JUCE build break (X11/ALSA/freetype dependencies, GCC-vs-Clang divergence) would pass CI silently because no Linux desktop compile ever runs. A reader auditing CI coverage would over-credit it. The fix is either to add a real `ubuntu-latest` desktop-build job with actual `apt-get install` of the JUCE Linux deps, or to rename the step and the job to honestly scope coverage to macOS.

### Committed convergence record reports `override-eligible` with nine open high/blocking findings and zero closed

Finding-ID: AUDIT-BARRAGE-claude-03b
Status:     open
Severity:   medium
Surface:    .stack-control/govern/convergence/impl__design-feature-svf-vertical-slice.json:11-66

This governance artifact is checked into the repo at HEAD recording `"rounds": 1`, `"closedInLoopFindings": []`, and `"outcome": "override-eligible"`, while `liftedFindings` enumerates nine findings rated `high`/`blocking` — including a silent-NaN audio path, a throwing path on the audio callback thread, and a `loop()`-vs-ISR parameter race. The subsequent commit bd79479 ("Address govern findings: RT-safety, thread ownership, doc drift") indicates remediation happened, yet this record shows nothing closed in-loop and the outcome frozen at "override-eligible."

Blast radius: an unattended agent (or auditor) reading the persisted governance state at HEAD reaches the natural conclusion that the feature shipped with nine unresolved high/blocking defects and was waved through by override. If those were in fact fixed by bd79479, this record is stale and contradicts the code; if they were not fixed, the feature shipped with known audio-safety defects. Either reading is materially wrong-or-alarming, and the artifact does not disambiguate. The fix is to regenerate the convergence record against the post-fix HEAD so `closedInLoopFindings` reflects reality, or to annotate which lifted findings bd79479 resolved.

### Enabling `ACFX_BUILD_DESKTOP` and `ACFX_BUILD_TESTS` together double-adds `host/processor-node`

Finding-ID: AUDIT-BARRAGE-claude-04
Status:     open
Severity:   low
Surface:    CMakeLists.txt:62-66,71-74

Both the desktop branch (`add_subdirectory(host/processor-node)` at line 63) and the test branch (`add_subdirectory(host/processor-node)` at line 73) add the same subdirectory. The presets keep these options mutually exclusive, so this never fires in the sanctioned flow — but a reasonable adopter who configures `-DACFX_BUILD_DESKTOP=ON -DACFX_BUILD_TESTS=ON` (build everything and run the tests in one tree) hits CMake's hard error `add_subdirectory … which has already been used`. 

Blast radius: low — it surfaces immediately at configure time with a CMake-level message, not a silent miscompile, and the preset-driven happy path is unaffected. Still, it is a latent footgun in a build system whose selling point is composability. A guard (`if(ACFX_BUILD_DESKTOP OR ACFX_BUILD_TESTS)` adding the subdir once, or a `if(NOT TARGET acfx_host)` check) removes the trap.