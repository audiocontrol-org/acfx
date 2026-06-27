I'll analyze this diff carefully against the stated audit criteria before writing up findings.

---

### Discrete-parameter mid-point normalization is an implicit, unverified cross-layer contract

Finding-ID: AUDIT-BARRAGE-claude-01
Status:     open
Severity:   high
Surface:    adapters/plugin/plugin-parameters.cpp:79–83

The `apply()` method encodes a discrete choice index as a normalized float using the formula `(index + 0.5f) / count`, then passes the result to `setParameter(id, norm)`. This formula is an implicit contract with the SVF effect's `setParameter` implementation: whatever decode logic lives in `core/effects/svf/svf-effect.*` must invert exactly this formula to recover the correct integer mode. The contract is nowhere documented and no test covers it end-to-end at the plugin-parameter level. If the SVF uses a floor-based decode (`floor(norm * count)`), it produces the right result for indices 0 and 2 but not necessarily for index 1 at exactly 0.5f (floating-point equality on `1.5f / 3.0f`). If it uses a round-based decode, the formula works. If a future refactor changes either side without updating the other, the wrong filter mode is silently selected — the plugin UI shows "bandpass" while the SVF runs in lowpass. The Daisy adapter bypasses this entirely by passing `hw.adc.GetFloat()` (a raw 0–1 ADC reading) directly to the same `setParameter`, so the two adapters encode the discrete state through fundamentally different mechanisms with no shared contract layer.

A reasonable fix: lift the discrete-to-normalized mapping into a shared free function in the core parameter model (alongside `normalize`/`denormalize`) so both adapters call the same encoding, and add a round-trip test that verifies `decode(encode(i)) == i` for every valid index.

---

### `modeName()` hardcodes SVF mode labels independently of the effect's authoritative definition

Finding-ID: AUDIT-BARRAGE-claude-02
Status:     open
Severity:   medium
Surface:    adapters/plugin/plugin-parameters.cpp:23–34

`modeName()` returns string labels for SVF filter modes by raw integer index (`0` → "lowpass", `1` → "highpass", `2` → "bandpass"). These labels are authored independently in the plugin adapter with no reference to the effect's own parameter descriptor. If the SVF effect reorders its mode constants or adds a mode, the plugin's `AudioParameterChoice` will display the wrong label for a given index with no compile-time or runtime error. The `ParameterDescriptor` type (visible in `plugin-parameters.h` via `dsp/parameter.h`) is the natural place for per-choice label arrays. Encoding them there would make every adapter's display automatically correct without per-adapter hardcoding. Blast-radius: every DAW session that loads the plugin could have mode labels mismatched to the actual filter behavior after any future SVF mode reorder.

---

### `core-tests` CI job runs only on `macos-latest` — Linux compilation never verified

Finding-ID: AUDIT-BARRAGE-claude-03
Status:     open
Severity:   medium
Surface:    .github/workflows/ci.yml:16–24

The stated architectural goal is a platform-independent core. The `portability-gate` job on `ubuntu-latest` runs `./scripts/check-portability.sh` (a static text scan), but it does not compile and run the host test suite on Linux. GCC and Clang on Linux may reject C++20 idioms, POSIX-specific headers, or platform-assuming compiler extensions that Apple Clang on macOS silently accepts. The quickstart calls out Scenario A (host tests) as the primary correctness gate; running it only on one platform weakens the portability claim. A second `core-tests` job on `ubuntu-latest` (with `apt-get install -y cmake` if needed) would cover this with minimal CI cost.

---

### `acfx_core` conditionally links `DaisySP` — test-preset builds may silently omit it

Finding-ID: AUDIT-BARRAGE-claude-04
Status:     open
Severity:   medium
Surface:    CMakeLists.txt:52–54

```cmake
if(TARGET DaisySP)
  target_link_libraries(acfx_core INTERFACE DaisySP)
endif()
```

`acfx_core` is the INTERFACE library that all targets consume. The condition `if(TARGET DaisySP)` passes only when `cmake/dependencies.cmake` has fetched DaisySP, which the `test` preset may not do (the full `dependencies.cmake` is in another chunk). The README states that `core/primitives/` wraps DaisySP's `Svf`, and the `svf-effect` in `core/effects/svf/` presumably includes those primitives. If the test preset fetches DaisySP (dependencies.cmake is gated per preset), this is fine. If it does not, any test translation unit that includes the DaisySP-wrapping primitive headers will fail with a missing-include error — but only at build time, not at CMake configure time, making the failure non-obvious. The guard should either be replaced with a hard `FATAL_ERROR` when the core requires DaisySP and it isn't found, or the fetch in `dependencies.cmake` should be unconditional so the guard is always true.

---

### No-op CI step is a false gate

Finding-ID: AUDIT-BARRAGE-claude-05
Status:     open
Severity:   low
Surface:    .github/workflows/ci.yml:38–39

```yaml
- name: Install JUCE Linux/macOS build prerequisites
  run: echo "macOS runner ships the required frameworks"
```

This step does nothing; it is a placeholder that always succeeds. It gives the CI log an "Install prerequisites" entry that a reader infers is an actual installation step. If the JUCE desktop build ever requires a framework or library not present on the stock macOS GitHub runner (e.g., a future JUCE version pulling in an optional system dep), this step provides no safety net and its green checkmark is actively misleading. Remove the step entirely, or replace it with a comment in the YAML, or make it an actual check (e.g., `xcode-select -p`).

---

### `teensy` preset omits `CMAKE_CXX_STANDARD` unlike every other preset

Finding-ID: AUDIT-BARRAGE-claude-06
Status:     open
Severity:   low
Surface:    CMakePresets.json:52–57

The `test`, `desktop`, and `daisy` presets each explicitly set `CMAKE_CXX_STANDARD: "20"`. The `teensy` preset does not. The top-level `CMakeLists.txt` line 31 sets `CMAKE_CXX_STANDARD 20` as the project default, but a toolchain file can override it. The comment in `CMakeLists.txt` acknowledges "Teensy may be pinned lower by its toolchain file," implying the intent is to let the toolchain control the standard — but the absence in the preset is silent rather than documented, and nothing prevents the project default (C++20) from being applied to a toolchain that does not support it. At minimum, add a comment in the preset explaining the omission; preferably, the toolchain file should explicitly set `CMAKE_CXX_STANDARD` to the supported value so the behavior is deterministic regardless of project defaults.