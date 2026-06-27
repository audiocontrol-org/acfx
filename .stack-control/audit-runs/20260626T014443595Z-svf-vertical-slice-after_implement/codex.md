### CI leaves the hardware preset gate outside the implemented quality surface

Finding-ID: AUDIT-BARRAGE-codex-01
Status:     open
Severity:   medium
Surface:    .github/workflows/ci.yml:5-7

The CI comment explicitly states that hardware presets are only build-checked when ARM toolchains are provisioned, but this workflow never provisions those toolchains and never runs `cmake --preset daisy`, `cmake --build --preset daisy`, `cmake --preset teensy`, or `cmake --build --preset teensy`. That matters because this vertical slice’s stated delivery surface includes Daisy and Teensy firmware builds, and `CMakePresets.json:33-50` defines those presets as first-class build surfaces. A downstream consumer reading “CI builds/tests the slice” will get host tests, portability script checks, and desktop builds, but not the actual hardware CMake integration.

Blast radius is medium: this does not break host or plugin users, but it lets the most toolchain-sensitive part of the slice regress while still presenting CI as the visible quality gate. A reasonable fix is to either provision the ARM toolchains and build the Daisy/Teensy presets in CI, or narrow the CI/documentation claim so it cannot be read as validating Scenario D build integration.
