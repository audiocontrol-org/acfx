I'll review my assigned chunk (`6a56babffbf5b038`) carefully and emit findings anchored to the diff.

### CI "Build plugin (VST3 / AU / CLAP)" builds the shared-code target, not the plugin formats

Finding-ID: AUDIT-BARRAGE-claude-01
Status:     open
Severity:   high
Surface:    .github/workflows/ci.yml:44-47 (cross-ref adapters/plugin/CMakeLists.txt:1-44)

The desktop-build job's final step is labeled "Build plugin (VST3 / AU / CLAP)" and runs `cmake --build --preset desktop --target acfx_plugin -j`. But `juce_add_plugin(acfx_plugin ...)` (adapters/plugin/CMakeLists.txt:1) does **not** make `acfx_plugin` the plugin binary — JUCE creates `acfx_plugin` as the *shared-code* static library and emits the actual format artifacts as separate targets (`acfx_plugin_VST3`, `acfx_plugin_AU`, the CLAP target, and the aggregate `acfx_plugin_All`). Building the bare `acfx_plugin` target compiles the plugin sources but never links the VST3/AU/CLAP wrappers or produces any plugin bundle.

The blast radius: this is the only CI gate that purports to prove the DAW-plugin slice (Phase 4, e74b0db) builds. A linker error in the JUCE format wrappers, a broken CLAP-extension registration (`clap_juce_extensions_plugin`, CMakeLists.txt:42), or a missing AU/VST3 symbol would all pass CI green while the step claims all three formats built. A downstream adopter trusting the green check would believe the plugin formats compile and link when CI never exercised them. A correct fix targets `acfx_plugin_All` (or enumerates the format targets explicitly) so the wrappers actually link.

### Daisy mode-knob normalization reproduces the lifted Teensy out-of-range exposure on an unaudited sibling

Finding-ID: AUDIT-BARRAGE-claude-02
Status:     open
Severity:   high
Surface:    adapters/daisy/daisy-main.cpp:33-47

The convergence ledger lifts `d58…claude-01`: "Teensy mode-pin normalization produces `norm = 1.0`, potentially yielding out-of-range discrete index." The daisy adapter has the identical exposure on a different surface, and nothing in this chunk shows it was covered by the round-4 fix. `maybeSet` (line 33) reads `hw.adc.GetFloat(adc)` — a value in the **closed** range [0.0, 1.0] — and passes it verbatim as the normalized parameter value via `svf.setParameter(... v)` (line 38). For `kMode` (line 47) the effect denormalizes a normalized value into a discrete mode index; if that mapping is `floor(norm * modeCount)` (the natural discrete denormalization), an ADC reading of exactly `1.0` yields index `modeCount`, one past the last valid mode.

Applying the channel-enumeration / fix-review driver: the lifted finding was dispositioned on the Teensy path, but the *value channel* it opens (any adapter feeding a raw [0,1]-inclusive control into the mode descriptor) was not enumerated. The daisy path is precisely that channel and carries no fixture proving the `norm == 1.0` boundary is safe. If the descriptor clamps the discrete index this is benign; if it does not (which the existence of a Teensy-specific fix implies), a knob at full deflection selects an out-of-range mode and reads/writes past the mode table in the real-time callback. A correct fix clamps the discrete index in the descriptor (one fix covering all adapters) rather than per-adapter, and adds a `norm == 1.0` fixture.

### `acfx_core` conditionally links DaisySP via `if(TARGET DaisySP)` — a silent fallback that hides a hard dependency

Finding-ID: AUDIT-BARRAGE-claude-03
Status:     open
Severity:   medium
Surface:    CMakeLists.txt:52-54

The core library is declared to *require* DaisySP — the surrounding comment (CMakeLists.txt:46-50) states "It links DaisySP because core/primitives wraps DaisySP's Svf," and `core/primitives/svf-primitive.h` `#include`s DaisySP headers. But the link is guarded: `if(TARGET DaisySP)` then `target_link_libraries(acfx_core INTERFACE DaisySP)`. When DaisySP is not fetched, the guard silently skips the link and `acfx_core` configures successfully — only to fail later with a confusing compile/link error from `svf-primitive.h` referencing a missing DaisySP symbol, with no message pointing at the real cause.

This is exactly the fallback-that-hides-failure pattern the project constitution forbids ("No fallbacks or mock data outside test code — raise descriptive errors for missing functionality"). The inconsistency is visible in the same diff: adapters/daisy/CMakeLists.txt:9-12 raises a hard `FATAL_ERROR` when `libDaisy_SOURCE_DIR` is undefined, the correct pattern. The core should do the same — `if(NOT TARGET DaisySP) message(FATAL_ERROR ...)` — so a missing pinned dependency fails loudly at configure time with an actionable message rather than as an opaque downstream compile error.

### Enabling `ACFX_BUILD_DESKTOP` and `ACFX_BUILD_TESTS` together is a fatal configure error (double `add_subdirectory`)

Finding-ID: AUDIT-BARRAGE-claude-04
Status:     open
Severity:   medium
Surface:    CMakeLists.txt:60-74

Both build-surface branches add the same subdirectory unconditionally: the desktop branch runs `add_subdirectory(host/processor-node)` (line 62) and the tests branch runs `add_subdirectory(host/processor-node)` again (line 71, with the comment explaining tests need the EffectNode boundary). These are independent `option()`s (lines 12-15), not mutually exclusive. A user who configures the obvious "build everything" invocation — `cmake -DACFX_BUILD_DESKTOP=ON -DACFX_BUILD_TESTS=ON` — hits CMake's hard error "The binary directory … is already used," because a source directory can only be added once.

The presets (CMakePresets.json) keep desktop and test separate, so the happy path is fine, but the options are first-class public knobs and nothing documents or enforces the exclusivity. The blast radius is a confusing configure failure for any adopter combining flags. A correct fix guards the inclusion idempotently (e.g. `if(NOT TARGET acfx_host) add_subdirectory(host/processor-node) endif()`) so the directory is added at most once regardless of which surfaces are enabled.

### CI never builds the Daisy/Teensy targets, so the hardware adapters ship with zero compile coverage

Finding-ID: AUDIT-BARRAGE-claude-05
Status:     open
Severity:   medium
Surface:    .github/workflows/ci.yml:1-47

The workflow header comment (ci.yml:4-8) states hardware presets are "build-checked only where ARM toolchains are provisioned — a deferred follow noted in quickstart.md." No job in the file provisions an ARM toolchain or invokes the `daisy`/`teensy` presets, so in practice the hardware adapters are **never** build-checked in CI. That means daisy-main.cpp / the Daisy + Teensy CMake targets (Phase 5, ae69f91) — the very surfaces this slice exists to prove portable (the README's "one source, many targets" claim) — have no automated gate; a change that breaks the ARM cross-compile lands green.

Two concerns compound here. First, the substantive coverage gap above: the headline portability guarantee is unverified by CI even though the `daisy`/`teensy` build presets exist (CMakePresets.json:23-44). Second, "a deferred follow noted in quickstart.md" is a deferral phrasing the project flags as a bug-factory smell; the invariant-first framing would state what CI *does* gate (host correctness + desktop compile + portability script) and that the ARM cross-compile is an explicitly-unguarded surface, rather than implying a build-check happens "where toolchains are provisioned" when none ever are. The honest move is either to add an ARM-toolchain build-check job or to state plainly in the workflow that hardware cross-compile is not gated by CI and is verified only on-hardware.