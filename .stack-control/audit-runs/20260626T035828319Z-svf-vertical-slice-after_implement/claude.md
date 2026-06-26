I'll audit chunk `6a56babffbf5b038` carefully and emit findings in the required format. This is a governance audit pass — my findings below are anchored to specific lines in the diff.

### `add_subdirectory(host/processor-node)` is added twice when both `ACFX_BUILD_DESKTOP` and `ACFX_BUILD_TESTS` are enabled, hard-failing configure

Finding-ID: AUDIT-BARRAGE-claude-01
Status:     open
Severity:   medium
Surface:    CMakeLists.txt:55-75

`ACFX_BUILD_DESKTOP` and `ACFX_BUILD_TESTS` are independent `option()`s with no mutual-exclusion guard. The desktop branch calls `add_subdirectory(host/processor-node)` (line ~57), and the tests branch *also* calls `add_subdirectory(host/processor-node)` (line ~72, with the comment "the test build includes it to exercise the EffectNode boundary too"). The two stock presets each flip only one flag, so the default paths are safe — but nothing prevents a consumer from configuring `cmake -B build -DACFX_BUILD_DESKTOP=ON -DACFX_BUILD_TESTS=ON` (e.g. a single build tree that both runs tests and produces the workbench). CMake will then add the same source directory to the same binary directory twice and abort configure with `The binary directory ... is already used by ...`.

Blast radius: an adopter or unattended agent that combines the two surfaces hits a hard, non-obvious configure error with no hint that the flags are exclusive. The fix is to make the second include idempotent — guard with `if(NOT TARGET acfx_host)` around the `host/processor-node` add (or hoist it to a single unconditional `if(ACFX_BUILD_DESKTOP OR ACFX_BUILD_TESTS)` block). This is a latent robustness defect, not a default-path break, hence medium.

### README Scenario C (plugin) documents `ACFX_WORKBENCH_FILE` / "live input" — both are workbench concepts that don't apply to a DAW plugin

Finding-ID: AUDIT-BARRAGE-claude-02
Status:     open
Severity:   medium
Surface:    README.md:71-76

The "Desktop plugin (VST3 / AU / CLAP) — Scenario C" section closes with: "To play the built-in player for reproducible A/B, point the workbench at a file with `ACFX_WORKBENCH_FILE=/path/to/audio.wav`; otherwise it uses the live input." This text describes the **standalone workbench's** built-in player, not the plugin. A plugin receives audio from its DAW host — it does not read `ACFX_WORKBENCH_FILE`, has no "built-in player", and "otherwise it uses the live input" is meaningless for a hosted plugin. The paragraph reads as copy-paste leakage from the Scenario B (workbench) section.

Blast radius: an adopter following Scenario C is told to set an env var that the plugin ignores, and an unattended agent building from this README could wire `ACFX_WORKBENCH_FILE` handling into the plugin processor where it has no place. It also compounds the already-lifted live-input bug (`4cfb00d5b3480886::AUDIT-BARRAGE-codex-01`): the doc asserts a live-input fallback that the code doesn't honor. A reasonable fix is to delete this paragraph from Scenario C (the plugin gets its signal from the host) or replace it with host-routing guidance. Medium: a consumer would eventually figure it out, but the wrong reading is the first one reached and the artifact never corrects it.

### CI "Install JUCE Linux/macOS build prerequisites" step is a no-op `echo` masquerading as a real install step

Finding-ID: AUDIT-BARRAGE-claude-03
Status:     open
Severity:   low
Surface:    .github/workflows/ci.yml:39-41

The `desktop-build` job's step is named "Install JUCE **Linux**/macOS build prerequisites" but its `run:` is just `echo "macOS runner ships the required frameworks"` — it installs nothing, and the job is pinned to `macos-latest`, so "Linux" in the name is doubly misleading. This is a placeholder dressed as a provisioning step. JUCE on Linux genuinely needs system packages (`libasound2-dev`, `libx11-dev`, `libfreetype6-dev`, `libcurl4-openssl-dev`, etc.); the moment someone repoints this job to `ubuntu-latest` (a natural cost/coverage change), the echo silently "succeeds" and the JUCE configure/build fails downstream with an opaque missing-header error instead of at the install step.

Blast radius: limited today (the macOS framework claim is accurate for the current runner), but the step name advertises capability it doesn't have, which is the operator-discipline trap pattern (placeholder masquerading as work). Fix: either rename to reflect that macOS needs no extra installs, or replace the echo with the actual `apt-get` package list guarded on `runner.os == 'Linux'`. Low — current default path works.

### `acfx_core` silently skips the DaisySP link via `if(TARGET DaisySP)`, an error-hiding guard inconsistent with the Daisy adapter's explicit `FATAL_ERROR`

Finding-ID: AUDIT-BARRAGE-claude-04
Status:     open
Severity:   medium
Surface:    CMakeLists.txt:50-53 (cross-ref adapters/daisy/CMakeLists.txt:9-12)

`acfx_core` wraps DaisySP's `Svf` (the CMakeLists comment states `core/primitives` depends on it), yet the link is conditional: `if(TARGET DaisySP) target_link_libraries(acfx_core INTERFACE DaisySP) endif()`. If DaisySP is *not* fetched, the guard quietly produces an `acfx_core` with no DaisySP include/link — so any translation unit that pulls in the SVF wrapper fails later with a cryptic "DaisySP header not found" compile error rather than a clear "DaisySP required" message. This is exactly the soft-fallback shape the repo standards forbid ("raise descriptive errors for missing functionality instead"). Notably, the sibling Daisy adapter does the right thing: `adapters/daisy/CMakeLists.txt:9-12` hard-errors with `message(FATAL_ERROR "libDaisy not fetched...")` when its dependency is absent.

Blast radius: depends on `cmake/dependencies.cmake` (not in this chunk) actually fetching DaisySP unconditionally for every preset; if it does, this is dormant. But the guard institutionalizes a fallback that turns a missing core dependency into a confusing downstream compile failure instead of a configure-time error, and it's inconsistent with how the adapter handles the same class of problem. Fix: make DaisySP a hard requirement at the point `acfx_core` is declared (`if(NOT TARGET DaisySP) message(FATAL_ERROR ...)`), matching the Daisy adapter. Medium as a design issue that compounds — the uncertainty is only whether the dependency is ever actually absent.

---

I checked the daisy-main.cpp audio callback for RT-safety (stack-only buffers, dead-banded knob publishing, no heap), the deadband NaN edge, the clang-format/editorconfig/gitignore hygiene files, the CMakePresets standard-version wiring (Teensy intentionally omits `CMAKE_CXX_STANDARD` per the toolchain note), and the plugin CLAP target naming — those came back clean or acknowledged-by-comment. I did **not** re-report the already-lifted live-input copy bug, the unchecked-acceptance-task finding, or the `<utility>` include finding (all in the convergence ledger), nor the hardware-presets-not-built-in-CI boundary, which the `ci.yml` header explicitly dispositions as a visible deferred follow.