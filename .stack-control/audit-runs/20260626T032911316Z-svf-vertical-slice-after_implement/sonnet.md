### README.md: Scenario D opens with the ARM toolchain prerequisite sentence twice in consecutive paragraphs

Finding-ID: AUDIT-BARRAGE-claude-01
Status:     open
Severity:   low
Surface:    README.md:49–63

The "Hardware cross-compile — Scenario D" section opens with "Requires an ARM embedded toolchain **with the C++ standard library**…" and then, immediately after the code block, repeats a nearly-identical sentence: "Requires an ARM embedded toolchain **with the C++ standard library** (ARM's gcc-arm-embedded or the vendor toolchain)." The two sentences contradict each other slightly — the first says the stock `arm-none-eabi-gcc` may lack the C++ stdlib; the second says to use ARM's gcc-arm-embedded or the vendor toolchain as if they are alternatives to each other. A reader building from this doc cannot tell whether a stock `arm-none-eabi` suffices or not, or whether the two paragraphs are saying the same thing. The second paragraph was almost certainly intended to start "Flashing and listening on a physical board is a separate checkpoint when hardware is in hand." — the prerequisite sentence is a leftover from an edit that merged two previously-separate paragraphs. Blast-radius is cosmetic/documentation only, hence low.

---

### CI `desktop-build` job has a no-op install step that masks missing prerequisites

Finding-ID: AUDIT-BARRAGE-claude-02
Status:     open
Severity:   medium
Surface:    .github/workflows/ci.yml:37–39

```yaml
      - name: Install JUCE Linux/macOS build prerequisites
        run: echo "macOS runner ships the required frameworks"
```

The step is labelled "Install JUCE Linux/macOS build prerequisites" but performs no installation — it emits a comment to stdout and exits 0. This is the operator-discipline trap the project guidelines call out explicitly: a fallback/placeholder that silently suppresses the failure mode it claims to handle. If GitHub's `macos-latest` runner image ever drops or relocates a required system framework (CoreAudio, CoreMIDI, AudioToolbox headers, etc.), the subsequent configure/build step will fail with an opaque CMake or linker error, not a clear "prerequisite missing" message. The name "Install" further creates false confidence that the step took an action. Either: (a) replace the echo with the actual `brew install` incantation for any packages that are not guaranteed by the image spec, or (b) rename the step to something honest ("Prerequisites already present on macOS runner") and add a comment that documents exactly which image-provided packages are being relied on and where to find the image manifest. Blast-radius is medium: the CI gate can become a false negative that silently passes the check by luck of the runner image, hiding a real missing-dependency defect from any developer who reproduces the failure locally.

---

### `CMakeLists.txt` will error on `add_subdirectory(host/processor-node)` if both `ACFX_BUILD_TESTS` and `ACFX_BUILD_DESKTOP` are ON simultaneously

Finding-ID: AUDIT-BARRAGE-claude-03
Status:     open
Severity:   medium
Surface:    CMakeLists.txt:52–73

`add_subdirectory(host/processor-node)` appears in two independent `if()` blocks:

```cmake
if(ACFX_BUILD_DESKTOP)
  add_subdirectory(host/processor-node)   # line ~52
  ...
endif()

if(ACFX_BUILD_TESTS)
  ...
  add_subdirectory(host/processor-node)   # line ~69
  ...
endif()
```

CMake does not allow the same source directory to be added twice with the same binary directory. Any invocation with both `-DACFX_BUILD_TESTS=ON -DACFX_BUILD_DESKTOP=ON` will fail immediately with `add_subdirectory: … has already been added`. The presets happen to be mutually exclusive (each sets only one option), but there is no CMake-level guard enforcing this, and the combination is a reasonable developer workflow (build everything and run tests in one pass). The comment on the TESTS block ("host/processor-node is desktop-only host code, but it is pure C++ (no JUCE), so the test build includes it to exercise the EffectNode boundary too") makes it clear both branches need the same target — the fix is to factor the subdirectory out of both blocks:

```cmake
if(ACFX_BUILD_DESKTOP OR ACFX_BUILD_TESTS)
  add_subdirectory(host/processor-node)
endif()
```

Blast-radius: any developer or CI matrix entry that enables both flags hits an immediate hard build failure with a CMake-level error message, not a compile or link error. The feature's tests cannot be verified alongside a desktop build in a single configure pass until this is fixed.