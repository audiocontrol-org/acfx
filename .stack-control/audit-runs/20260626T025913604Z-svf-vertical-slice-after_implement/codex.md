### CI/README still encode an open hardware validation gap as deferred work

Finding-ID: AUDIT-BARRAGE-codex-01  
Status:     open  
Severity:   medium  
Surface:    .github/workflows/ci.yml:3-7; README.md:79-81

The workflow comment says hardware presets are build-checked only where ARM toolchains are provisioned, with “a deferred follow noted in quickstart.md” at `.github/workflows/ci.yml:5-7`. README repeats the same shape by saying flashing and listening are “a separate checkpoint when hardware is in hand” at `README.md:79-81`.

That is an operator-discipline trap in this governance pass: the artifact does not express a precise invariant for what is in-scope now versus what remains unverified, and it uses explicit deferred-work language in a shipped quality-gate surface. Blast radius is medium: an adopter or unattended agent can reasonably treat Scenario D as accepted by documentation/process despite no CI or local command proving the firmware link/flash path. A reasonable correction is to state the current verified boundary mechanically, for example host portability checks plus any configured cross-compile target, and keep unverified hardware acceptance as an explicit unchecked/manual acceptance item rather than prose embedded in CI/README.
