I was dispatched as an audit subagent for chunk `6a56babffbf5b038`, so I'll skip the skills bootstrap and execute the audit directly. I walked every file in scope against the prior lifted findings to avoid re-reporting triaged issues.

### `add_subdirectory(host/processor-node)` invoked twice when DESKTOP and TESTS are both enabled — CMake configure error

Finding-ID: AUDIT-BARRAGE-claude-01
Status:     open
Severity:   medium
Surface:    CMakeLists.txt (ACFX_BUILD_DESKTOP block ~L58-62 and ACFX_BUILD_TESTS block ~L70-75)

`CMakeLists.txt` adds the `host/processor-node` subdirectory in two independent option blocks:

```cmake
if(ACFX_BUILD_DESKTOP)
  add_subdirectory(host/processor-node)   # <-- first
  ...
endif()
...
if(ACFX_BUILD_TESTS)
  enable_testing()
  add_subdirectory(host/processor-node)   # <-- second, same source dir, no distinct binary dir
  add_subdirectory(tests)
endif()
```

A second `add_subdirectory()` against the same source path with no override binary directory is a hard CMake error ("The binary directory … is already used to invoke add_subdirectory"). The supplied presets dodge this because each preset flips exactly one of `ACFX_BUILD_DESKTOP` / `ACFX_BUILD_TESTS`, but nothing in the build constrains them to be mutually exclusive — a developer running a combined `-DACFX_BUILD_DESKTOP=ON -DACFX_BUILD_TESTS=ON` configure (the obvious way to build the workbench *and* run the core suite in one tree) gets a configure failure rather than the union of both target sets. Blast radius: a contributor following the natural "turn both on" instinct hits a confusing failure with no hint that the options are secretly incompatible. Fix: guard with an `if(NOT TARGET acfx_host)` (or equivalent already-added check), or hoist the `host/processor-node` subdirectory into a single block shared by `ACFX_BUILD_DESKTOP OR ACFX_BUILD_TESTS`.

### `if(TARGET DaisySP)` silently no-ops core's only DSP dependency instead of hard-failing

Finding-ID: AUDIT-BARRAGE-claude-02
Status:     open
Severity:   medium
Surface:    CMakeLists.txt (~L48-54)

`acfx_core` wraps DaisySP's `Svf` (per `core/primitives/svf-primitive.h`, referenced in the header comment and other chunks), so DaisySP is a *required* dependency for any TU that includes the SVF primitive. Yet the linkage is guarded:

```cmake
if(TARGET DaisySP)
  target_link_libraries(acfx_core INTERFACE DaisySP)
endif()
```

The prose comment two lines above states flatly "It links DaisySP." When DaisySP is *not* a target (a dependency-fetch regression in `cmake/dependencies.cmake`, an offline configure, a preset that forgets to pin it), this guard quietly drops the link — and the failure then surfaces as a cryptic missing-header / undefined-symbol error deep inside `svf-primitive.h`, not as a clear configure-time diagnostic. This is exactly the "silent fallback that hides a failure mode" the project standards forbid, and it is *inconsistent with the project's own pattern*: `adapters/daisy/CMakeLists.txt` raises a `FATAL_ERROR` when `libDaisy_SOURCE_DIR` is undefined. Core should do the same — `if(NOT TARGET DaisySP) message(FATAL_ERROR "DaisySP not fetched …") endif()` — so a missing dependency fails loud at configure time. (Cross-chunk caveat: I cannot see `cmake/dependencies.cmake` in this chunk to confirm DaisySP is always fetched whenever core builds; if it is, the guard is dead defensive code that still mis-describes intent and should be tightened.)

### README claims the workbench has a working "bound MIDI CC" that a lifted finding says is never enabled

Finding-ID: AUDIT-BARRAGE-claude-03
Status:     open
Severity:   medium
Surface:    README.md (Desktop workbench section, "a bound MIDI CC")

The README's Scenario B description promises the workbench ships "auto-generated controls for cutoff / resonance / mode, a built-in player or live input, **a bound MIDI CC**, and a dry/processed A/B toggle." The convergence record for this same governance pass lifts finding `561c01…codex-01`: "MIDI inputs are registered but never enabled." If that code finding is accurate, the README overstates currently-working behavior — an adopter reading the README as the source of truth will wire a MIDI controller, observe nothing, and assume a hardware/config problem rather than an unfinished surface. This is documentation drift on a user-facing capability claim, distinct from the code finding itself (different surface, README vs. `workbench-app.cpp`). Fix: either complete the MIDI enable path the code finding identifies, or soften the README claim to match what actually works today (e.g. drop the MIDI clause until the input is enabled). Blast radius is bounded to a confused user, hence medium rather than high.

### CI "Install JUCE prerequisites" step is a no-op echo mislabeled "Linux/macOS" on a macOS-only runner

Finding-ID: AUDIT-BARRAGE-claude-04
Status:     open
Severity:   low
Surface:    .github/workflows/ci.yml (desktop-build job, L40-42)

```yaml
- name: Install JUCE Linux/macOS build prerequisites
  run: echo "macOS runner ships the required frameworks"
```

This step installs nothing — it is a placeholder `echo`. Two smells: (1) the step *name* claims to cover "Linux/macOS" while the job's `runs-on` is `macos-latest` only, so the Linux half is fiction; (2) it is a structural placeholder that reads as "prerequisites handled" while doing no work. The current macOS runner does ship the frameworks JUCE needs (CoreAudio, Accelerate, etc.), so the build works today — hence low, not higher. But the moment anyone adds a Linux matrix leg (the README and the portability job already use `ubuntu-latest`), this step gives false assurance: it will "pass" while installing none of the ALSA/X11/freetype dev packages a JUCE Linux build actually requires. Fix: either delete the cosmetic step entirely (the build step alone is honest about what runs), or rename it to its real scope ("macOS frameworks are provided by the runner") so it stops advertising Linux coverage that does not exist.

---

Checks that came back **clean** and why: `.clang-format` / `.editorconfig` are internally consistent (both 4-space C++, 100-col, 2-space CMake) with no contradictory rules; `.gitignore`'s `external/.cpm-cache/` entry correctly matches the `CPM_SOURCE_CACHE` path set in `CMakeLists.txt`; `CMakePresets.json` keeps `ACFX_BUILD_*` options partitioned one-per-preset (the reason the double-`add_subdirectory` bug doesn't bite preset users); `adapters/plugin/CMakeLists.txt` uses valid 4-char JUCE manufacturer/plugin codes and links the CLAP extension to the same target as documented. I did **not** re-report the already-lifted Daisy ISR `setParameter` RT-safety finding — the dead-band logic in `daisy-main.cpp` is the new mitigation for it, and the residual RT-safety question (coefficient recompute inside the audio callback) is the same shape already triaged, not a regression.