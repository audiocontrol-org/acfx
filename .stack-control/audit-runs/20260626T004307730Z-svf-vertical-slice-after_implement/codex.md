### Combined desktop+test configuration re-adds the same subdirectory

Finding-ID: AUDIT-BARRAGE-codex-01
Status:     open
Severity:   medium
Surface:    CMakeLists.txt:55-73

`host/processor-node` is added in the `ACFX_BUILD_DESKTOP` branch at lines 55-58 and again in the `ACFX_BUILD_TESTS` branch at lines 69-74. The shipped presets avoid this only because they enable one surface at a time, but the options are public build flags and should compose. A user configuring `-DACFX_BUILD_DESKTOP=ON -DACFX_BUILD_TESTS=ON` will hit CMake’s duplicate `add_subdirectory`/binary-dir error before any target builds.

The blast radius is medium: the default presets work, but a reasonable downstream consumer trying to run tests and desktop targets from one configure tree gets a hard configure failure. A reasonable fix is to gate `host/processor-node` through a single shared condition, or add it once before the desktop/test target branches when either option is enabled.

### Governance convergence metadata does not include this audited chunk

Finding-ID: AUDIT-BARRAGE-codex-02
Status:     open
Severity:   high
Surface:    .stack-control/govern/convergence/impl__design-feature-svf-vertical-slice.json:7-14

The convergence record lists chunk IDs `5d46...`, `952c...`, `b04c...`, `b74f...`, `ba29...`, and `e8e...`, but the current audited chunk is `6a56babffbf5b038`, containing CI, root CMake, presets, README, Daisy, and plugin CMake surfaces. Acting on this metadata as written makes the governance artifact claim convergence over a fleet of chunks that does not include the very chunk containing the top-level build and CI wiring.

The blast radius is high because this file is a machine-readable governance artifact; an unattended consumer could treat the feature as converged while excluding the current build-surface chunk from the recorded chunk set. The fix is to regenerate or correct the convergence record so `chunkIds` matches the actual chunk partition for the audited range.

### Versioned governance file captures a local absolute path

Finding-ID: AUDIT-BARRAGE-codex-03
Status:     open
Severity:   medium
Surface:    .stack-control/govern/convergence/impl__design-feature-svf-vertical-slice.json:134

The committed convergence artifact records `"anchorRoot": "/Users/orion/work/acfx-work/platform-foundation"`. That path is specific to one workstation and will be wrong in any other clone, CI runner, or operator checkout.

The blast radius is medium: it may not affect the audio feature directly, but it makes a versioned governance artifact non-portable and can mislead tools that consume the root path literally. Store repo-relative provenance, omit the local root from committed state, or generate it at runtime.

### CI comment contains prohibited deferred-work wording

Finding-ID: AUDIT-BARRAGE-codex-04
Status:     open
Severity:   low
Surface:    .github/workflows/ci.yml:3-7

The CI header includes wording at line 6 that explicitly parks ARM hardware build-check coverage outside the visible CI job set. The workflow itself only runs host tests, portability checks, and the desktop build; there is no ARM-toolchain job corresponding to the comment’s hardware-build claim.

The blast radius is low as code execution is unaffected, but this is an operator-discipline trap in a quality-gate file: it normalizes a missing gate in prose instead of encoding the invariant or making the absence mechanically visible. A cleaner fix is to state the current CI invariant directly and avoid committed-to-later wording.
