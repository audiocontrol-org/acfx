### `modeName()` hardcodes SVF-specific choice labels in a class that claims to be descriptor-driven

Finding-ID: AUDIT-BARRAGE-claude-01
Status:     open
Severity:   medium
Surface:    adapters/plugin/plugin-parameters.cpp:25-36 (also build() at :52-56)

`plugin-parameters.h:11-17` states the design contract explicitly: "There is no hand-written parameter list: each `ParameterDescriptor` becomes a JUCE parameter … so the mapping is identical across adapters (SC-006)." But the choice **labels** for the discrete parameter are not data-driven — they are a hardcoded `switch` in `modeName()` (`0→lowpass`, `1→highpass`, `2→bandpass`) consumed by `build()`. That is precisely "configuration that should be data ending up as code." The `ParameterDescriptor` carries `discreteCount` but not the label strings, so the labels live in code in *this* adapter — and, by the SC-006 "identical across adapters" claim, must be duplicated again in the workbench, which means the single source of truth is fractured across adapters.

Two concrete blast-radius consequences for an adopter who adds a fourth mode (or any second discrete parameter): (1) `build()` loops `for (int i = 0; i < d.discreteCount; ++i) choices.add(modeName(i))`, and `modeName`'s `default` returns `"lowpass"` for every index ≥ 3 — so `discreteCount == 5` produces a `juce::AudioParameterChoice` with three entries all named `"lowpass"`, which DAWs render as indistinguishable duplicates and some hosts reject. (2) A *different* discrete parameter (say a 4-position waveform selector) would silently get filter-mode names. The fix is to thread the label strings through the descriptor (an array/span of names alongside `discreteCount`) so `build()` reads them as data, eliminating the hardcoded `switch` and the cross-adapter duplication.

### CI never compiles the hardware (Daisy/Teensy) presets — the feature's headline "one source, four targets" claim is unverified by any gate

Finding-ID: AUDIT-BARRAGE-claude-02
Status:     open
Severity:   medium
Surface:    .github/workflows/ci.yml:1-47 (vs README.md:1-6, SC-007 claim)

The README's first sentence and the SVF slice's stated thesis is that the same `core/effects/svf` source compiles to desktop **and** microcontroller firmware (Daisy, Teensy) "with no per-target `#ifdef` forks" (SC-007). CI has three jobs: `core-tests`, `portability-gate` (which runs `scripts/check-portability.sh` — static file/grep checks, not a compile), and `desktop-build`. **No job ever invokes `cmake --preset daisy` or `cmake --preset teensy`.** The single most distinctive correctness claim of the slice — that the core actually cross-compiles for ARM — has zero automated verification. The `portability-gate` confirms *textual* portability (no JUCE includes in MCU code, file-size budget) but never proves the code compiles under `arm-none-eabi-gcc`.

Blast radius: the ci.yml header comment asserts hardware presets are "build-checked only where ARM toolchains are provisioned," and README.md:84-86 tells adopters "CI … runs the host tests, the portability gates, and the desktop build on every change" — an adopter reasonably reads this as "portability is enforced." But a core change that breaks the ARM build (e.g. introduces a `<thread>` include or a non-`constexpr` static init) would sail through every green check; the regression surfaces only when someone manually runs the daisy preset. The honest fix is either a CI job that installs gcc-arm-embedded and runs `cmake --build --preset daisy` (build-only, no flashing), or — if that is genuinely out of scope — a one-line note in README's Quality-gates section stating that ARM compilation is NOT in CI, so the gap is visible rather than implied-covered.

### `desktop-build` CI step "Install JUCE … prerequisites" is a no-op `echo` masquerading as a real install step, mislabeled "Linux/macOS"

Finding-ID: AUDIT-BARRAGE-claude-03
Status:     open
Severity:   low
Surface:    .github/workflows/ci.yml:38-41

The step `name: Install JUCE Linux/macOS build prerequisites` runs `run: echo "macOS runner ships the required frameworks"`. It installs nothing — it is a placeholder that the step name implies is doing real provisioning work. The label also says "Linux/macOS" while the job's `runs-on` is `macos-latest` and no Linux desktop job exists, so the "Linux" half is dead/aspirational. This is the operator-discipline trap the project guidelines call out: a step whose name promises an action it does not perform.

Blast radius is low (the job currently works because the macOS runner does ship the Apple frameworks JUCE links), but it is misleading to a maintainer who later moves the job to a Linux runner — JUCE on Linux needs real packages (`libasound2-dev`, `libgtk-3-dev`, `libwebkit2gtk`, etc.), and the echo will silently "succeed" while the subsequent build fails with cryptic missing-header errors. Fix: either delete the cosmetic step (the configure step already validates the toolchain) or make it a real `apt-get`/`brew` install guarded by `runner.os`, and correct the name to match the actual runner.

### Discrete-parameter bin count diverges between `build()` (raw `discreteCount`) and `apply()` (clamped to ≥2)

Finding-ID: AUDIT-BARRAGE-claude-04
Status:     open
Severity:   low
Surface:    adapters/plugin/plugin-parameters.cpp:52-56 vs :86-91

`build()` creates choices with the raw count (`for (int i = 0; i < d.discreteCount; ++i)`), but `apply()` round-trips through a *different* denominator: `const int count = e.descriptor.discreteCount < 2 ? 2 : e.descriptor.discreteCount;` then `norm = (index + 0.5f) / count`. For any descriptor with `discreteCount < 2`, the two halves disagree: `build()` would register 0 or 1 choices while `apply()` normalizes as if there were 2 bins, so the normalized value pushed to `setParameter` lands in the wrong half of the descriptor's range and the effect denormalizes to an unintended mode.

This is not reachable today (the SVF descriptor has `discreteCount == 3`), so severity is low — but it is a latent inconsistency in a class whose whole purpose is a correct descriptor→JUCE→`setParameter` round-trip for *any* effect's descriptor table. The guard belongs in one place: either reject/clamp `discreteCount < 2` at `build()` time (a discrete parameter with <2 choices is malformed and should be a hard error per the no-silent-fallback rule), or compute the bin count once and share it. As written, the silent `< 2 ? 2` clamp in only one of the two methods is exactly the kind of asymmetric fallback that hides a malformed descriptor instead of surfacing it.

### Daisy adapter hands a 2-channel block to a single shared `SvfEffect` — verify the core maintains per-channel filter state

Finding-ID: AUDIT-BARRAGE-claude-05
Status:     open
Severity:   medium
Surface:    adapters/daisy/daisy-main.cpp:43-49 (cross-file: core/effects/svf, core/primitives DaisySP::Svf wrapper — not in this chunk)

`AudioCallback` copies both input channels to output, wraps them in `AudioBlock block(channels, 2, size)`, and runs them through one module-static `acfx::SvfEffect svf` (daisy-main.cpp:18). A State-Variable Filter is stateful (integrator/feedback memory). If `SvfEffect` wraps a *single* DaisySP `Svf` instance and feeds both channels' samples through it, the left and right channel states interleave and corrupt each other — stereo material comes out as a smeared mono-ish artifact. This is the one adapter that bypasses the `host/processor-node` boundary and drives the concrete effect directly (the comment at :3-5 calls this out deliberately), so it is also the place where a per-channel-state defect in the shared core would first bite on real hardware.

I cannot confirm or refute this from the audited chunk — `core/effects/svf/svf-effect.h` and the primitives wrapper are in other chunks (b04c878073d5d626). I am surfacing it because the SAME effect instance is reused across desktop and MCU, so if the core holds only one filter's worth of state, every stereo path is wrong, not just Daisy. The operator should verify that `SvfEffect::process(AudioBlock)` instantiates/advances one filter *per channel* (i.e. `block.channelCount()` independent states) and that `prepare(ctx)` with `ctx.channels == 2` allocates them. If it does, this is clean; if it holds a single state, this is a high-severity correctness bug in the core that this adapter exercises directly.