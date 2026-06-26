### CI documents an unowned hardware-validation gap

Finding-ID: AUDIT-BARRAGE-codex-01
Status:     open
Severity:   medium
Surface:    .github/workflows/ci.yml:3-7

The CI header says hardware presets are build-checked only where ARM toolchains are provisioned, then points to a non-owned future validation path instead of encoding an invariant in this workflow. In this file there is no Daisy or Teensy job at all: the workflow only runs host tests, portability checks, and desktop builds (`.github/workflows/ci.yml:15-49`). That makes the comment weaker than the executable surface and leaves the hardware side of FR-015 as a claim rather than a gate.

Blast radius is medium: a downstream agent or adopter reading CI as the quality contract can reasonably conclude that hardware build coverage exists under some provisioned condition, when this workflow never provisions or exercises it. A reasonable fix is to either add explicit hardware build jobs/toolchain setup, or make the invariant precise in the workflow comments: CI does not build MCU presets, and the only current gate for them is the named portability script.
