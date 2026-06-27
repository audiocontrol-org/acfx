I'll walk this chunk carefully. It's a comment-only change to `.github/workflows/ci.yml` — two added comment blocks, no logic, no step changes.

### CI comments assert coverage the workflow does not enforce; risk of over-trust + silent drift

Finding-ID: AUDIT-BARRAGE-claude-01
Status:     open
Severity:   low
Surface:    .github/workflows/ci.yml:40-42 (desktop-build comment), :18-19 (core-tests comment)

The added comment on `desktop-build` states it "compile-verifies the workbench against real JUCE, including the new audio-config units (audio-settings, source-bar, workbench-persistence)." This is a *compile-only* guarantee that the comment correctly scopes ("compile-verifies"), but its truth is entirely dependent on the CMake wiring in another chunk (`adapters/workbench/CMakeLists.txt`, listed under d0c555613386cd51) actually adding those three units to the desktop target. Nothing in this workflow references the units by name, so if a future refactor drops `source-bar` or `workbench-persistence` from the desktop target, the build stays green, the comment silently becomes false, and a reader trusting it concludes the unit is covered when it isn't. The blast-radius is bounded (a stale comment, not a broken gate), hence low — but an unattended agent reading this file to decide "are the audio-config surfaces guarded by CI?" would over-credit the protection: the comment reads as coverage of the selection/persistence surfaces when in fact behavior is unexercised and, per commit 92a865c, explicitly operator-owned manual acceptance. A more robust form would either name nothing it can't enforce, or the workflow would invoke a JUCE-free smoke target that actually constructs these units so the claim is load-bearing rather than narrative.

### Other dimensions checked clean

Finding-ID: AUDIT-BARRAGE-claude-CLEAN
Status:     open
Severity:   informational
Surface:    (the entire diff)

Beyond the drift note above, I walked the rest of the chunk and found nothing to surface. The diff is purely additive comments — no `runs-on`, step, trigger, matrix, or permissions change — so there is no new attack surface, no altered job behavior, no race or ordering concern, and no possibility of breaking the existing `core-tests`/`desktop-build` jobs. The `core-tests` comment's claim (JUCE-free SourceConfig serde test linking into `acfx_core_tests`) is consistent with the commit log (7103f38 "SourceConfig serde + unit test") and the other-chunk file list (`tests/core/workbench-settings-test.cpp`, `tests/CMakeLists.txt`); the only nit is that the comment calls it the "SourceConfig serde test" while the file is `workbench-settings-test.cpp`, a naming-vs-file mismatch too minor to be actionable. I would have flagged a removed/disabled job, a `continue-on-error`, a secret exposure, or a comment that contradicted the job it annotates — none are present.