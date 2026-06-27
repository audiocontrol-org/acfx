I'll audit chunk `6a56babffbf5b038` carefully, anchoring each finding to the diff.

### CMakeLists.txt double-adds `host/processor-node` when DESKTOP and TESTS are both enabled

Finding-ID: AUDIT-BARRAGE-claude-01
Status:     open
Severity:   medium
Surface:    CMakeLists.txt — `ACFX_BUILD_DESKTOP` block (`add_subdirectory(host/processor-node)`) and `ACFX_BUILD_TESTS` block (`add_subdirectory(host/processor-node)`)

Both the `if(ACFX_BUILD_DESKTOP)` branch and the `if(ACFX_BUILD_TESTS)` branch unconditionally call `add_subdirectory(host/processor-node)`. CMake errors hard ("The binary directory … is already used to build a source directory") if a single configure has both options ON. The shipped presets keep them mutually exclusive, so the happy path is safe — but the options are public (`option(...)`), and the natural "build everything and run the tests in one tree" invocation (`-DACFX_BUILD_DESKTOP=ON -DACFX_BUILD_TESTS=ON`) fails at configure time with a cryptic message rather than the obvious "build desktop + test" workflow a consumer expects.

Blast radius: an adopter combining the two documented surfaces hits a configure-time abort whose message points at CMake internals, not at the real cause. A reasonable fix is to guard the second add with `if(NOT TARGET acfx_host)` (or factor the `host/processor-node` add into a single place gated by `ACFX_BUILD_DESKTOP OR ACFX_BUILD_TESTS`).

### `acfx_core` silently skips its mandatory DaisySP link via `if(TARGET DaisySP)`

Finding-ID: AUDIT-BARRAGE-claude-02
Status:     open
Severity:   medium
Surface:    CMakeLists.txt — `if(TARGET DaisySP)\n  target_link_libraries(acfx_core INTERFACE DaisySP)\nendif()`

The block comment states the core *requires* DaisySP ("It links DaisySP because core/primitives wraps DaisySP's Svf"), and the SVF primitive `#include`s DaisySP headers. Yet the link is wrapped in `if(TARGET DaisySP)`, so when DaisySP has not been fetched the link is silently dropped and `acfx_core` configures successfully. Any downstream consumer that actually compiles the SVF wrapper then fails with a confusing "daisysp/… file not found", far from the real cause.

This is precisely the fallback-that-hides-a-failure-mode the project guidelines forbid ("raise descriptive errors for missing functionality instead"). It is also inconsistent with the sibling `adapters/daisy/CMakeLists.txt`, which correctly `message(FATAL_ERROR …)`s when `libDaisy_SOURCE_DIR` is undefined. Blast radius: a mis-provisioned dependency cache produces a misleading compile error instead of an actionable "DaisySP not fetched — configure with a preset that pins it." Fix: emit a `FATAL_ERROR` when DaisySP is absent (mirroring the libDaisy guard) rather than a conditional link.

### README places `ACFX_WORKBENCH_FILE` under Scenario C (plugin) where it does not apply

Finding-ID: AUDIT-BARRAGE-claude-03
Status:     open
Severity:   medium
Surface:    README.md — "Desktop plugin (VST3 / AU / CLAP) — Scenario C" section, trailing paragraph about `ACFX_WORKBENCH_FILE`

Scenario C documents building the DAW plugin, then ends with: "To play the built-in player for reproducible A/B, point the workbench at a file with `ACFX_WORKBENCH_FILE=/path/to/audio.wav`; otherwise it uses the live input." This instruction is about the *workbench app* (Scenario B), not the plugin — a VST3/AU/CLAP plugin is loaded by a DAW and is not driven by `ACFX_WORKBENCH_FILE`. Meanwhile Scenario B ("Desktop workbench") contains no mention of the env var at all, so the one place the variable is documented is the wrong section.

Blast radius: a reader following Scenario C to A/B-test the plugin sets `ACFX_WORKBENCH_FILE` and observes no effect on the plugin, while a workbench user reading Scenario B never learns the variable exists. Fix: move the `ACFX_WORKBENCH_FILE` paragraph to the Scenario B (workbench) section where the built-in player actually lives.

### README Scenario D duplicates the "Requires an ARM embedded toolchain" paragraph

Finding-ID: AUDIT-BARRAGE-claude-04
Status:     open
Severity:   low
Surface:    README.md — "Hardware cross-compile — Scenario D" section (paragraph before the code block and paragraph after it)

The sentence "Requires an ARM embedded toolchain **with the C++ standard library** …" appears twice in Scenario D — once before the `cmake --preset daisy/teensy` block and again immediately after it (the second copy parenthetically lists "ARM's gcc-arm-embedded or the vendor toolchain"). The repetition reads as an editing artifact; the second occurrence should either be merged into the first or trimmed to just the flashing/listening checkpoint note. Pure hygiene — no behavioral consequence — hence low.

### ci.yml "Install JUCE Linux/macOS build prerequisites" is a no-op echo masquerading as a setup step

Finding-ID: AUDIT-BARRAGE-claude-05
Status:     open
Severity:   low
Surface:    .github/workflows/ci.yml — `desktop-build` job, step `name: Install JUCE Linux/macOS build prerequisites` running `echo "macOS runner ships the required frameworks"`

The step is named as if it installs JUCE prerequisites for both Linux and macOS, but its body is a bare `echo` that installs nothing. The job runs on `macos-latest`, so the echo is harmless today — but the name overstates what happens, and if the runner is ever switched to (or a Linux runner added for) the Linux build the README advertises ("JUCE Linux/macOS"), JUCE's Linux dependencies (`libasound2-dev`, `libx11-dev`, `libfreetype6-dev`, etc.) will be missing and the build will fail at link/compile time while this step still reports success.

Blast radius: a future Linux CI lane appears "covered" by a step that does nothing. Per project guidelines a placeholder step is a bug-factory. Fix: either drop the step (macOS needs nothing) or make it a real `apt-get install` guarded by `runner.os == 'Linux'`, with an honest name.

### ci.yml comment commits to a "deferred follow" and claims hardware build-checking that no job performs

Finding-ID: AUDIT-BARRAGE-claude-06
Status:     open
Severity:   medium
Surface:    .github/workflows/ci.yml — header comment ("Hardware presets are build-checked only where ARM toolchains are provisioned — a deferred follow noted in quickstart.md")

Two problems in the header comment. First, it contains an explicit deferral phrase — "a deferred follow noted in quickstart.md" — the kind of deferred-work commitment the dispatch wrapper treats as a bug-factory; the boundary should be stated as the workflow's actual invariant (CI gates host correctness + desktop build) rather than as a promise of later work. Second, the comment asserts "Hardware presets are build-checked only where ARM toolchains are provisioned," which implies conditional machinery that would build-check the `daisy`/`teensy` presets if a toolchain were present — but no such job exists anywhere in the file (there are only `core-tests`, `portability-gate`, and `desktop-build`). The hardware presets are simply not built by CI at all.

Blast radius: a reader auditing CI coverage from this comment concludes hardware builds are gated "where provisioned," when in fact zero hardware build-checking exists and none would run even if a toolchain appeared. Fix: replace the comment with the workflow's true coverage statement and drop the deferral phrasing; if hardware build-checking is intended, add the job (even a conditional one) so the comment matches reality.

### Daisy mode-knob dead-band is applied to the raw value, so it does not prevent discrete-mode chatter at a quantization boundary

Finding-ID: AUDIT-BARRAGE-claude-07
Status:     open
Severity:   low
Surface:    adapters/daisy/daisy-main.cpp:`maybeSet(...)` + `constexpr float kKnobDeadband = 0.002f` + the `kMode` call in `AudioCallback`

`maybeSet` gates re-publishing on the *raw* normalized knob value crossing a 0.002 dead-band, then hands that raw value to `setParameter`; the descriptor quantizes it to a mode index downstream. For the continuous cutoff/resonance knobs this is fine. For the discrete `mode` knob it does not do what the surrounding comment implies: when the knob is parked near a mode-quantization boundary, ADC noise larger than 0.002 (typical for a Seed ADC reading after scaling) repeatedly straddles the boundary, so `maybeSet` keeps re-publishing values that quantize to *alternating* mode indices — the filter mode flaps audibly. A 0.002 raw-value dead-band is smaller than realistic ADC jitter and provides no hysteresis on the quantized index.

Blast radius: a user resting the mode knob on a boundary hears the filter type flicker; it is a real-hardware UX defect, not a correctness break, hence low. The right fix is hysteresis on the *quantized mode index* (only re-publish when the decoded mode actually changes), rather than a dead-band on the raw knob value. This is distinct from the lifted clamp finding (which concerns out-of-range index selection) — this is about boundary chatter even when clamping is correct.

---

I also checked the items that came back **clean**: `.clang-format`/`.editorconfig` are internally consistent (100-col, 4-space, matching `max_line_length`); `.gitignore`'s `external/.cpm-cache/` matches the `CPM_SOURCE_CACHE` default in CMakeLists; `CMakePresets.json` preset/build/test-preset names line up and the JUCE `PLUGIN_MANUFACTURER_CODE`/`PLUGIN_CODE` satisfy the 4-char/uppercase rule; the Daisy linker-script wiring and `EXCLUDE_FROM_ALL` subdir add are sound; and the `daisy-main.cpp` audio callback is allocation-free with `lastKnob` initialized to `-1.0f` so the first block always publishes valid coefficients before `process()`.