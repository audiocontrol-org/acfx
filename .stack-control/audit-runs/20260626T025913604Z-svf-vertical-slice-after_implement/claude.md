I'll audit this chunk (build system, CI, README, Daisy adapter, plugin/clang config) independently.

### Combined `-DACFX_BUILD_TESTS=ON -DACFX_BUILD_DESKTOP=ON` configure fails: `host/processor-node` added twice

Finding-ID: AUDIT-BARRAGE-claude-01
Status:     open
Severity:   medium
Surface:    CMakeLists.txt:55-75

The top-level `CMakeLists.txt` adds the `host/processor-node` subdirectory under **two** independent option blocks. The desktop block (line ~57) does `add_subdirectory(host/processor-node)`, and the tests block (line ~71) does it again with the comment "the test build includes it to exercise the EffectNode boundary too." Each `add_subdirectory` of that path defines the `acfx_host` target (the plugin links `acfx_host`, confirming the target name).

If a developer configures with both flags on in one tree — a natural combined dev build, or an IDE that enables several `ACFX_BUILD_*` options at once — CMake aborts at configure time with a duplicate-`add_subdirectory`/duplicate-target error. The supplied presets dodge this only because each preset flips exactly one surface (`test` sets `ACFX_BUILD_TESTS`, `desktop` sets `ACFX_BUILD_DESKTOP`), so the failure mode is invisible to anyone who only ever uses presets, which is exactly why it will surprise the first person who deviates. Blast radius: a confusing hard failure for combined builds, with no diagnostic pointing at the real cause. Fix: guard the subdirectory (`if(NOT TARGET acfx_host) add_subdirectory(host/processor-node) endif()`) or hoist it to a single block that fires when either option is set.

### README Scenario C (plugin) ends with workbench-only instructions (`ACFX_WORKBENCH_FILE`)

Finding-ID: AUDIT-BARRAGE-claude-02
Status:     open
Severity:   medium
Surface:    README.md:73-80

The "Desktop plugin (VST3 / AU / CLAP) — Scenario C" section builds the three plugin format targets, then closes with: "To play the built-in player for reproducible A/B, point the workbench at a file with `ACFX_WORKBENCH_FILE=/path/to/audio.wav`; otherwise it uses the live input." That paragraph describes the **workbench** standalone app (Scenario B), not the plugin. A plugin is loaded by a host DAW; it has no `ACFX_WORKBENCH_FILE` entry point and is not "pointed at a file" via an env var.

This is documentation drift: the env-var/built-in-player guidance belongs under Scenario B (the workbench), where the Scenario B blurb already mentions "a built-in player or live input." An adopter following Scenario C to validate the plugin is handed instructions that do nothing for the plugin and will look for a workbench behavior in a DAW context. Blast radius: medium — a reader testing the plugin is misdirected, though most will eventually realize the plugin runs in a DAW. Fix: move the `ACFX_WORKBENCH_FILE` paragraph up into the Scenario B section.

### CI never builds the Daisy/Teensy (ARM) presets — the central "one source → MCU" claim has no automated build gate

Finding-ID: AUDIT-BARRAGE-claude-03
Status:     open
Severity:   medium
Surface:    .github/workflows/ci.yml:1-49

The feature's headline thesis is "write once, run as desktop, plugin, and MCU firmware from the same source" (README; SC-007). Yet CI builds only `test` and `desktop` presets. The `daisy`/`teensy` presets — the *only* automated proof that `core/effects/svf` actually cross-compiles for ARM — are never configured or built. The substitute is `portability-gate` running `scripts/check-portability.sh`, which (per its description elsewhere) is a file-size/grep/no-JUCE-in-MCU heuristic, **not** a compile. A grep gate cannot catch a C++ construct the ARM toolchain rejects, a missing include only the MCU path needs, or a libDaisy API drift — so a change that breaks the ARM build merges green while the cross-platform claim still reads as proven.

The header comment also contains a deferral phrase — "Hardware presets are build-checked only where ARM toolchains are provisioned — a deferred follow noted in quickstart.md" — which per the audit's bug-factory rule should be surfaced; here it concedes the gap rather than closing it. Blast radius: medium — regressions to the feature's defining capability ship undetected by CI, and the README/govern ledger continue to assert ARM portability that no automated gate enforces. Fix: add an ARM-toolchain install + `cmake --preset daisy && cmake --build --preset daisy` (and teensy) configure/build job, even if behind a provisioned-toolchain conditional, so the cross-compile is exercised rather than asserted.

### CI "Install JUCE Linux/macOS build prerequisites" is a no-op `echo` and mislabels the macOS-only runner

Finding-ID: AUDIT-BARRAGE-claude-04
Status:     open
Severity:   low
Surface:    .github/workflows/ci.yml:37-40

The `desktop-build` job runs `runs-on: macos-latest`, but its first step is named "Install JUCE Linux/macOS build prerequisites" and its body is `run: echo "macOS runner ships the required frameworks"` — a placeholder that installs nothing. Two problems: (1) the step name advertises an install that does not happen, an operator-discipline smell that reads as "prerequisites handled" when the job simply assumes the macOS image is sufficient; (2) it names "Linux" while the job only ever runs on macOS, so the desktop/plugin build is never exercised on Linux despite the framing. JUCE on Linux genuinely needs dev packages (ALSA, X11, etc.), so if a Linux desktop job is ever added by copying this pattern, the no-op step silently provides nothing.

Blast radius: low — cosmetic/misleading today, but it primes a future Linux job for a confusing failure. Fix: either delete the empty step (the macOS image assumption can live in a comment) or replace it with the real prerequisite installs and a matrix that actually covers the OSes named.

### README hardware section repeats the "ARM toolchain with C++ stdlib" requirement verbatim

Finding-ID: AUDIT-BARRAGE-claude-05
Status:     open
Severity:   low
Surface:    README.md:82-96

The "Hardware cross-compile — Scenario D" block states "Requires an ARM embedded toolchain **with the C++ standard library** …" immediately before the `cmake --preset daisy/teensy` code block, then states it again immediately after ("Requires an ARM embedded toolchain **with the C++ standard library** (ARM's gcc-arm-embedded or the vendor toolchain)."). The requirement is duplicated with the code block sandwiched between the two copies.

This is pure documentation hygiene — no behavioral consequence — but the redundancy makes the section read as a copy-paste artifact and slightly obscures the one piece of net-new information in the second copy (the "flashing and listening … is a separate checkpoint" note). Blast radius: low. Fix: merge into a single requirement sentence and keep the distinct flashing-checkpoint note once.

---

I also checked the Daisy adapter (`daisy-main.cpp`) RT path (dead-banded `setParameter`, in→out copy then in-place process — sound, no alloc in the callback), the `.clang-format`/`.editorconfig`/`.gitignore` (consistent, CPM cache path matches `CMakeLists` `CPM_SOURCE_CACHE`), and the plugin CMake CLAP target naming (`acfx_plugin_CLAP` matches CI/README). The previously-lifted "README Scenario C → `acfx_plugin`" HIGH appears addressed in this diff (README now builds the explicit `_VST3/_AU/_CLAP` format targets), and the Daisy mode-knob normalization HIGH is already lifted/open, so I did not re-litigate either.