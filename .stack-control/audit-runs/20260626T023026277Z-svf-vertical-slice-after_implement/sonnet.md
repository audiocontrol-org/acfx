### README.md documents `acfx_plugin` as the plugin build target, which only builds shared code, not format bundles

Finding-ID: AUDIT-BARRAGE-claude-01
Status:     open
Severity:   medium
Surface:    README.md:67-72 vs .github/workflows/ci.yml:44-47

`README.md` instructs developers to build the plugin with:

```bash
cmake --build --preset desktop --target acfx_plugin
```

But the comment directly above the corrected CI step (added in the round-5 fix) states explicitly:

```
# The aggregate acfx_plugin target builds only the shared code; the format
# wrappers are separate targets that produce the actual bundles.
```

The CI was fixed to build `acfx_plugin_VST3 acfx_plugin_AU acfx_plugin_CLAP` — resolving the prior lifted finding — but the README was not updated to match. A developer following the README will invoke `cmake --build ... --target acfx_plugin`, receive no error, and produce no installable plugin bundles. Blast radius: any user of this repo who follows the documented "Scenario C" build path walks away with an empty artifact and no diagnostic that anything went wrong. Fix: update `README.md` Scenario C to name the three format targets, mirroring the CI step.

---

### Linux compilation of the core DSP is never exercised by CI

Finding-ID: AUDIT-BARRAGE-claude-02
Status:     open
Severity:   medium
Surface:    .github/workflows/ci.yml (all three jobs)

Every job that compiles C++ runs only on `macos-latest`:

- `core-tests` — configure + build + ctest on `macos-latest`
- `desktop-build` — workbench and plugin on `macos-latest`
- `portability-gate` — runs on `ubuntu-latest` but executes `./scripts/check-portability.sh`, a static grep/file-size script with no compilation

There is no CI job that actually compiles the core DSP library on Linux. Platform-divergent behavior — `<cmath>` rounding modes, `__attribute__` support, libstdc++ vs libc++ ABI, strict-aliasing diagnostics — goes undetected. Blast radius: a Linux-specific compilation failure (or silent behavioral regression) is invisible to CI until a developer or hardware target surfaces it. A straightforward fix is to add a second `core-tests` job with `runs-on: ubuntu-latest` using the same configure/build/ctest steps.

---

### CI "Install JUCE Linux/macOS build prerequisites" step is a no-op placeholder

Finding-ID: AUDIT-BARRAGE-claude-03
Status:     open
Severity:   low
Surface:    .github/workflows/ci.yml:37-38

```yaml
- name: Install JUCE Linux/macOS build prerequisites
  run: echo "macOS runner ships the required frameworks"
```

The step name asserts an installation action; the implementation is a comment masquerading as a command. This is the exact pattern the project constitution prohibits: a placeholder that hides a missing implementation. If GitHub's macOS runner image is updated and a required framework (e.g., CoreAudio headers, Accelerate) is moved or removed, the CI will fail with an opaque linker error rather than at this clearly-named step. Blast radius: diagnosis of a future dependency regression requires reading the full build log rather than seeing a labelled failure. The step should either install the known prerequisites explicitly or be removed entirely if the macOS runner image is genuinely self-sufficient.

---

### Daisy mode-knob normalization — lifted HIGH finding still open with no fix visible in this chunk

Finding-ID: AUDIT-BARRAGE-claude-04
Status:     open
Severity:   high
Surface:    adapters/daisy/daisy-main.cpp:33-39

The convergence JSON (`liftedFindings`) carries this finding as unresolved:

> "Daisy mode-knob normalization reproduces the lifted Teensy out-of-range exposure on an unaudited sibling"

In `daisy-main.cpp`, `maybeSet()` forwards the raw ADC float (`hw.adc.GetFloat(adc)`, range `[0.0, 1.0]`) directly to `setParameter` for all three parameters including mode:

```cpp
void maybeSet(acfx::SvfEffect::Param param, int adc) {
    const float v = hw.adc.GetFloat(adc);
    if (v < lastKnob[adc] - kKnobDeadband || v > lastKnob[adc] + kKnobDeadband) {
        lastKnob[adc] = v;
        svf.setParameter(acfx::ParamId{static_cast<std::uint8_t>(param)}, v);
    }
}
```

Mode is a discrete enum (LP / BP / HP). If the descriptor maps `[0, 1)` to mode index 0, `[1/3, 2/3)` to index 1, and `[2/3, 1.0]` to index 2 (or any comparable scheme), values near `1.0` from a fully-clockwise knob must land in a defined bucket. The Teensy finding (in another chunk) identified that this boundary was not being clamped or validated, producing an out-of-range mode index. No fix to the Daisy adapter is visible here — the `maybeSet` implementation is unchanged — and the finding is not listed under `closedInLoopFindings`. The fix may live in a core-side denormalization path visible only in another chunk (e.g., the parameter descriptor); if so, that chunk should close this finding explicitly. As written, the Daisy adapter is an unverified sibling with the same surface.