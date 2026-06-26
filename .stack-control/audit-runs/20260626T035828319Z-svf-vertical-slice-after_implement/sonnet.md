### Double `add_subdirectory(host/processor-node)` when DESKTOP and TESTS both enabled

Finding-ID: AUDIT-BARRAGE-claude-01
Status:     open
Severity:   medium
Surface:    CMakeLists.txt:47–65

`CMakeLists.txt` adds `host/processor-node` unconditionally inside both the `ACFX_BUILD_DESKTOP` and `ACFX_BUILD_TESTS` blocks (lines +47 and +62 of the diff). CMake raises a hard error if `add_subdirectory()` is called twice with the same relative path and the binary directory is already occupied. The provided presets are mutually exclusive, so this is latent rather than immediately triggered — but a developer who manually sets `-DACFX_BUILD_DESKTOP=ON -DACFX_BUILD_TESTS=ON` (e.g. to run integration tests while also iterating on the desktop adapter) will get a CMake configuration error with a confusing message. The comment at the test block even says the subdirectory is "desktop-only host code" added here specifically to exercise the EffectNode boundary — exactly the scenario a combined build would want. Fix: gate the second include with `if(ACFX_BUILD_TESTS AND NOT ACFX_BUILD_DESKTOP)`, or hoist the single `add_subdirectory(host/processor-node)` call above both option blocks.

---

### README Scenario D has a duplicate paragraph

Finding-ID: AUDIT-BARRAGE-claude-02
Status:     open
Severity:   low
Surface:    README.md:~76–84

The sentence block "Requires an ARM embedded toolchain **with the C++ standard library** (the stock `arm-none-eabi-gcc` may be C-only and cannot build the C++ core):" appears twice in the Scenario D section — once before the code block and once immediately after it. The post-block occurrence is a copy-paste remnant and should be deleted.

---

### CI JUCE prerequisites step is a no-op echo

Finding-ID: AUDIT-BARRAGE-claude-03
Status:     open
Severity:   informational
Surface:    .github/workflows/ci.yml:40

The step "Install JUCE Linux/macOS build prerequisites" runs `echo "macOS runner ships the required frameworks"` — a string literal printed to stdout, not an install command. It is a comment masquerading as a CI step. If a framework dependency is ever added that macOS GitHub-hosted runners don't pre-install (e.g. a system audio library for a plugin format, or an SDK), this step will silently pass while the configure or build step below it fails with an opaque linker error. Either replace the echo with a real check (`xcode-select -p`, `brew list`, etc.) or remove the step entirely so the step list is honest about what is actually done.

---

### Teensy preset inherits C++20 default but the toolchain file is the only guard

Finding-ID: AUDIT-BARRAGE-claude-04
Status:     open
Severity:   informational
Surface:    CMakePresets.json:43–47, CMakeLists.txt:31–33

The `teensy` preset (unlike `test`, `desktop`, and `daisy`) does not set `CMAKE_CXX_STANDARD` as a cache variable. `CMakeLists.txt` falls back to C++20 whenever `CMAKE_CXX_STANDARD` is not already defined. Depending on the GCC version bundled with the Teensy toolchain, C++20 support may be absent or incomplete (the PJRC Teensy toolchain historically shipped GCC 11.x, which has partial C++20 support, and earlier Teensyduino versions used GCC 5.x). The `cmake/toolchains/teensy.cmake` file (not in this chunk) is the sole line of defence — if it does not override the standard, the build will attempt C++20 on a toolchain that cannot deliver it. Worth explicitly setting `CMAKE_CXX_STANDARD` in the preset or documenting the minimum required toolchain version in the `teensy` preset's `displayName` or a comment.