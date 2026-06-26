### Daisy mode-knob normalization — lifted HIGH finding remains unaddressed in this chunk

Finding-ID: AUDIT-BARRAGE-claude-01
Status:     open
Severity:   high
Surface:    adapters/daisy/daisy-main.cpp:31–36, 47

`maybeSet` passes the raw `hw.adc.GetFloat(adc)` value — a continuous float in [0.0, 1.0] — directly to `svf.setParameter(...)` for all three parameters, including mode (line 47: `maybeSet(acfx::SvfEffect::kMode, kAdcMode)`). The comment says "the effect denormalizes via its descriptor — identical mapping to every other adapter," implying the descriptor handles the mapping. However, mode is a discrete parameter (LP / HP / BP / Notch). For cutoff and resonance, a normalized float passing through to a continuous range is straightforward. For a discrete mode, the descriptor's quantization logic must explicitly floor/round the [0,1] float into an integer mode index, scaling by the number of modes. If it treats the incoming value as a direct integer (i.e., `static_cast<int>(0.73f) == 0`), the mode knob is effectively broken — it maps the entire travel of the knob to mode 0.

The governance file (`impl__design-feature-svf-vertical-slice.json`, line 28–31) lists this as a lifted HIGH finding from a prior round: `"title": "Daisy mode-knob normalization — lifted HIGH finding still open with no fix visible in this chunk"`. No discretization or rounding logic has been added to `maybeSet` or around line 47 in this diff. The core SVF parameter definitions are in other chunks (not visible here), so whether the descriptor does the right thing cannot be confirmed from this surface alone. The fix needs to be visible here or evidenced by the descriptor's quantization behavior being explicitly called out in the comment — neither is present.

---

### README Scenario D duplicates the ARM toolchain prerequisite sentence

Finding-ID: AUDIT-BARRAGE-claude-02
Status:     open
Severity:   low
Surface:    README.md:76–78 and README.md:86–87

The Scenario D section opens with "Requires an ARM embedded toolchain **with the C++ standard library** (the stock `arm-none-eabi-gcc` may be C-only and cannot build the C++ core):" (lines 76–78), then after the bash block repeats "Requires an ARM embedded toolchain **with the C++ standard library** (ARM's gcc-arm-embedded or the vendor toolchain)." (lines 86–87). The second sentence reads as a standalone duplicate of the first, not as a continuation or clarification. A reader following the section sees the same prerequisite stated twice with slightly different parenthetical content, which can mislead them into thinking there are two separate requirements. Blast radius: low — a developer reading the section will likely resolve it, but a documentation generator or an AI agent auto-generating setup guides from this README could reproduce the duplicate. The fix is to merge the two parenthetical clarifications into one sentence.

---

### CI "Install JUCE" step is a misleading no-op placeholder

Finding-ID: AUDIT-BARRAGE-claude-03
Status:     open
Severity:   low
Surface:    .github/workflows/ci.yml:38–39

The `desktop-build` job contains a step named "Install JUCE Linux/macOS build prerequisites" whose entire body is `run: echo "macOS runner ships the required frameworks"`. The step name implies it installs dependencies; the body does nothing. This is an operator-discipline trap: if JUCE ever requires a framework or tool that is not pre-installed on `macos-latest` (which changes with GitHub Actions runner updates), a future maintainer will see a step that looks like it handles the installation and will assume the missing dependency must be somewhere else. The correct fix is either to remove the step entirely (if macOS runners genuinely need nothing installed) or replace the echo with the actual installation commands. A no-op step with an action-implying name is a bug-factory per project guidelines.