I'll review the diff in chunk 6a56babffbf5b038 as an independent audit reviewer.

---

### Duplicate `add_subdirectory(host/processor-node)` when both `ACFX_BUILD_DESKTOP` and `ACFX_BUILD_TESTS` are ON

Finding-ID: AUDIT-BARRAGE-claude-01
Status:     open
Severity:   high
Surface:    CMakeLists.txt:47 and CMakeLists.txt:64

`host/processor-node` is unconditionally added inside both the `ACFX_BUILD_DESKTOP` block and the `ACFX_BUILD_TESTS` block. CMake prohibits adding the same source directory twice; if anyone configures with both flags ON — a natural thing to do when developing locally — the configure step aborts immediately with "The directory host/processor-node has already been added." The comment on line 63 ("it is pure C++ (no JUCE), so the test build includes it") acknowledges the dual-use but doesn't guard against the duplicate. The CI avoids this by using mutually-exclusive presets, but the root CMakeLists.txt makes the combined configuration silently broken. Fix: wrap both `add_subdirectory` calls with a `if(NOT TARGET acfx_host)` guard, or hoist the single call above both conditionals and drive it from the union of the two flags.

---

### `setParameter` still executes inside the audio ISR — dead-band fix reduces call frequency but does not bound worst-case interrupt latency

Finding-ID: AUDIT-BARRAGE-claude-02
Status:     open
Severity:   medium
Surface:    adapters/daisy/daisy-main.cpp:33–51

Round-0 self-red-team on the fix diff: the prior lifted finding (`6a56babffbf5b038::AUDIT-BARRAGE-claude-01`) flagged `setParameter` being called unconditionally every block from the audio ISR. The fix in this commit adds a dead-band gate (`kKnobDeadband = 0.002f`) so `setParameter` is only called when the ADC reading shifts by more than that threshold. This addresses "unconditionally every block" but does not address the underlying concern: `setParameter` — which for an SVF filter must recompute frequency-warped coefficients involving trigonometric operations — still executes directly inside `AudioCallback`, which is the STM32H7 audio interrupt handler. The dead-band reduces average-case ISR duration but does not bound worst-case latency; a knob move that clears the dead-band still triggers coefficient computation inside the ISR. The canonical embedded fix is to sample ADC in the main loop, write a normalized value to an `std::atomic<float>` (or a lock-free slot), and in the ISR read from the atomic — keeping `setParameter` entirely off the interrupt path. As written, a steadily-sweeping knob produces the identical latency profile as the unfixed version; only stable knobs are improved.

Additionally, the new `lastKnob[kAdcCount]` state (line 26) is read and written exclusively from `AudioCallback`, which is safe for now, but there is no `volatile` or atomic qualifier, and the main-loop comment ("nothing to do here") implicitly relies on this invariant remaining true. If parameter update logic migrates to the main loop in a future iteration, a data race would be introduced silently.

---

### Core tests run only on macOS — the "platform-independent" core is never compiled and tested on Linux in CI

Finding-ID: AUDIT-BARRAGE-claude-03
Status:     open
Severity:   medium
Surface:    .github/workflows/ci.yml:13–22

The `core-tests` job specifies `runs-on: macos-latest`. The portability gate (`portability-gate` job, line 24) runs on `ubuntu-latest` but only executes `./scripts/check-portability.sh` — a static analysis / grep script — not a `cmake --preset test && ctest` compilation + execution. The README and architecture doc both assert that `core/` is platform-independent, but that claim is verified by compilation and test run only on macOS. GCC vs. Clang divergences, glibc vs. libc++ differences, and any accidental `__APPLE__`-gated code would pass CI undetected. Adding a Linux matrix entry to `core-tests` (e.g., `ubuntu-latest` with `apt-get install -y cmake ninja-build`) is the minimal fix that would make the platform-independence claim CI-backed rather than asserted.

---

### "Install JUCE prerequisites" CI step is a no-op `echo` masquerading as an installation step

Finding-ID: AUDIT-BARRAGE-claude-04
Status:     open
Severity:   medium
Surface:    .github/workflows/ci.yml:38–40

The `desktop-build` job contains a step named "Install JUCE Linux/macOS build prerequisites" whose `run:` body is `echo "macOS runner ships the required frameworks"`. This step does nothing; it merely prints a string. If JUCE's configure step requires a framework, SDK version, or package that is not present on the `macos-latest` runner image (e.g., a specific Xcode toolchain, `pkg-config`, or ALSA/X11 headers if the image ever changes), the failure will surface in the subsequent "Configure (desktop preset)" step with a cryptic CMake error rather than in the step whose name says "Install prerequisites." The misleading step name makes triage harder. Either the step should install something real, or it should be removed and replaced with a comment in the workflow YAML explaining why no installation is needed. Leaving a no-op step that names an important category of work ("Install… prerequisites") is an operator-discipline trap.

---

### `external/_deps/` gitignore pattern does not match CPM's actual output location

Finding-ID: AUDIT-BARRAGE-claude-05
Status:     open
Severity:   low
Surface:    .gitignore:8

CPM writes fetched sources into `${CMAKE_BINARY_DIR}/_deps/`, not into `${CMAKE_SOURCE_DIR}/external/_deps/`. The build directory is `build/${presetName}` (per `CMakePresets.json`), so the actual `_deps/` directories land at `build/test/_deps/`, `build/desktop/_deps/`, etc. — all covered by the `build/` pattern on line 3. The `external/_deps/` entry is therefore dead: the path it matches is never created by CMake, while the paths that are created are already ignored. The entry imposes no harm but signals a misunderstanding of CPM's layout that could confuse a developer investigating the `.cpmcache/` vs `_deps/` distinction. It should either be corrected to document actual CPM output paths or removed.

---

### Dead-band constant `kKnobDeadband` has no derivation comment

Finding-ID: AUDIT-BARRAGE-claude-06
Status:     open
Severity:   low
Surface:    adapters/daisy/daisy-main.cpp:28

`constexpr float kKnobDeadband = 0.002f` is used to suppress `setParameter` calls when ADC noise moves the reading by less than 0.2 % of full scale. The Daisy Seed has a 12-bit ADC (resolution ≈ 0.024 % per LSB), so the chosen dead-band spans roughly 8 LSBs. This value is plausible but the derivation is not documented. A reader tuning this for a different board (e.g., Teensy 4.x with its 13-bit ADC) has no basis for recalibration. More importantly, a value that is too small leaves ADC dither triggering coefficient recomputes in the ISR; a value too large creates perceptible parameter quantisation at the hardware knob. The non-obvious invariant — "chosen to suppress ADC noise while keeping parameter resolution perceptually transparent" — warrants a one-line comment citing the ADC resolution and the target dead-band in LSBs.