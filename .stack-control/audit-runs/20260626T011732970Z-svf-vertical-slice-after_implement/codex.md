### Deferred CI hardware gate is documented as an accepted gap

Finding-ID: AUDIT-BARRAGE-codex-01
Status:     open
Severity:   medium
Surface:    .github/workflows/ci.yml:3-7

The CI workflow explicitly says hardware presets are only build-checked where ARM toolchains are provisioned, then labels the missing CI coverage as “a deferred follow” on line 6. That phrase is itself a governance trap under this audit’s hard constraints, and it also weakens the advertised FR-015 quality gate: a downstream agent reading this as the authoritative CI contract can preserve the missing Daisy/Teensy build checks as an accepted steady state instead of making the boundary mechanically visible in CI.

The blast radius is medium: host tests and desktop builds still run, so this does not break the whole feature, but it institutionalizes a missing target-class gate for the cross-platform vertical slice. A reasonable fix is to remove the deferral language and encode the invariant directly, e.g. either add explicit skipped/manual hardware jobs with toolchain precondition checks, or state the exact enforced CI boundary without commitment-style wording.
