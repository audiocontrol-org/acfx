### `setParameter` called unconditionally every audio block inside Daisy ISR — new surface of prior RT-safety finding

Finding-ID: AUDIT-BARRAGE-claude-01
Status:     open
Severity:   high
Surface:    adapters/daisy/daisy-main.cpp:26-28

`AudioCallback` calls `svf.setParameter()` three times per block, unconditionally, on every invocation:

```cpp
svf.setParameter(acfx::ParamId{acfx::SvfEffect::kCutoff},    hw.adc.GetFloat(kAdcCutoff));
svf.setParameter(acfx::ParamId{acfx::SvfEffect::kResonance},  hw.adc.GetFloat(kAdcResonance));
svf.setParameter(acfx::ParamId{acfx::SvfEffect::kMode},       hw.adc.GetFloat(kAdcMode));
```

The prior govern finding `e8e701847d91847a::AUDIT-BARRAGE-codex-01+AUDIT-BARRAGE-claude-02` flagged "`SvfEffect::setParameter` mutates filter state with no audio-thread ownership boundary." The Daisy adapter is a new file introduced in this diff; it exhibits the same structural pattern on a new surface. Commit bd79479 claims to "Address govern findings: RT-safety, thread ownership, doc drift" but this file was not guarded.

There is no race condition in the Daisy adapter because the main loop (`for(;;){}`) does nothing — all parameter writes come from within the callback itself. However, two concerns remain: (1) if `setParameter` recomputes SVF coefficients (typically involving `tan()` or similar transcendental math), that computation happens inside the hardware ISR at every block boundary (≈1 ms at 48 kHz / 48-sample block), adding unbounded CPU overhead that can cause audio underruns; (2) the pattern, once established in adapters, communicates to future adapter authors that calling `setParameter` from the ISR is the correct idiom, regardless of whether a future implementation makes it expensive. A minimal fix is to read ADC values into temporaries, compare to the last known value, and call `setParameter` only when the value changes by more than a dead-band threshold.

---

### CI `core-tests` job runs only on macOS — Linux compilation never exercised

Finding-ID: AUDIT-BARRAGE-claude-02
Status:     open
Severity:   medium
Surface:    .github/workflows/ci.yml:16-25

The `core-tests` job (line 18: `runs-on: macos-latest`) compiles and runs the test suite exclusively on macOS. The `portability-gate` job (lines 29-34) does run on `ubuntu-latest` but only executes `./scripts/check-portability.sh`, which is a static grep/file-size check — it does not invoke CMake, compile any C++, or run any test binary.

The project's stated architectural invariant is "platform-independent core." A Linux-specific compilation failure in any core header (e.g., a POSIX dependency, an extension __attribute__ that Clang accepts on macOS but GCC rejects on Linux, a `#include <bits/…>` path difference) would pass CI without detection. The CMake preset uses `Unix Makefiles`, so Linux builds are possible and the gap is not intentional omission — it appears to be an oversight. A portable core deserves at least one `cmake --preset test && ctest --preset test` on `ubuntu-latest` alongside the macOS run.

---

### README references `.specify/memory/constitution.md` — path likely dead after migration to `.stack-control/`

Finding-ID: AUDIT-BARRAGE-claude-03
Status:     open
Severity:   medium
Surface:    README.md:91

The final line of `README.md` reads:

```
See [`CLAUDE.md`](CLAUDE.md) and the full project constitution at
[`.specify/memory/constitution.md`](.specify/memory/constitution.md).
```

All other tooling artifacts in the repository live under `.stack-control/` (e.g., `.stack-control/govern/convergence/…`). The `.specify/` namespace belongs to an earlier Spec Kit layer. If `.specify/memory/constitution.md` does not exist on disk, the link is dead and the document it points to — described as "the full project constitution" — is unreachable from the README. Any adopter following the README's "Standards" pointer arrives at a 404. The fix is to verify whether the file exists at `.specify/memory/constitution.md`, and if not, update the link to the authoritative location (likely `.stack-control/` or `specs/`).

---

### `host/processor-node` labeled "desktop-only" but compiled under the `test` preset — classification mislabeling creates maintenance trap

Finding-ID: AUDIT-BARRAGE-claude-04
Status:     open
Severity:   medium
Surface:    CMakeLists.txt:60-65

```cmake
if(ACFX_BUILD_TESTS)
  enable_testing()
  # host/processor-node is desktop-only host code, but it is pure C++ (no JUCE),
  # so the test build includes it to exercise the EffectNode boundary too.
  add_subdirectory(host/processor-node)
  add_subdirectory(tests)
endif()
```

The subdirectory is introduced under `ACFX_BUILD_DESKTOP` AND again under `ACFX_BUILD_TESTS`, with a comment explicitly acknowledging the contradiction: "desktop-only host code … the test build includes it." The classification mismatch is the finding: the directory is named and described as "desktop-only" throughout (the Daisy CMakeLists, the project layout in README, and the constitution all reinforce this framing), yet the build system contradicts the label.

The danger is maintainability: a future contributor adding a JUCE dependency to `host/processor-node` will follow the "desktop-only" label and consider it safe to do so, silently breaking the `test` preset at configure time. The fix is either to (a) rename the directory to remove "desktop-only" from its identity (e.g., `host/effect-node`) and update documentation, or (b) add an explicit `ACFX_BUILD_TESTS` guard inside `host/processor-node/CMakeLists.txt` that rejects JUCE dependencies via a CMake assertion.

---

### `CMakePresets.json` hardcodes `Unix Makefiles` generator — Windows excluded without documentation

Finding-ID: AUDIT-BARRAGE-claude-05
Status:     open
Severity:   low
Surface:    CMakePresets.json:6

The `base` hidden preset specifies `"generator": "Unix Makefiles"`, which all four named presets inherit. `Unix Makefiles` is not available on a native Windows CMake installation (MSVC / LLVM on Windows requires `Ninja` or a Visual Studio generator). The README describes the project as "a cross-platform audio DSP platform" and documents the `cmake --preset test` workflow without qualifying it to macOS/Linux only.

A developer on Windows following the README's build instructions will receive an inscrutable CMake error about the generator, not a clear "Windows is not a supported host build platform." The fix is either to replace the generator with `Ninja` (available on all platforms and recommended by JUCE), or to add a `windowsCondition`-gated preset alongside a note in the README that host-side builds are currently macOS/Linux only.