I walked the `.github/workflows/ci.yml` chunk. The diff is **comment-only** — two explanatory blocks added to the `core-tests` and `desktop-build` jobs, with zero changes to steps, triggers, runners, or commands. So there is no executable behavior to find a correctness bug in; the audit surface is whether the added prose makes claims the rest of the feature actually backs.

### CI comments assert cross-file linkage facts with nothing enforcing them

Finding-ID: AUDIT-BARRAGE-claude-01
Status:     open
Severity:   medium
Surface:    .github/workflows/ci.yml:18-19 and :40-42 (added comment blocks)

Both added comments make specific, falsifiable claims about *other files* that this chunk cannot verify and that nothing in CI enforces. The `core-tests` comment asserts the "JUCE-free SourceConfig serde test … **links into `acfx_core_tests`**" — that wiring lives in chunk `cc36a7e4` (`tests/CMakeLists.txt`, `tests/core/workbench-settings-test.cpp`). The `desktop-build` comment asserts the build "**including the new audio-config units (audio-settings, source-bar, workbench-persistence)**" — that target membership lives in chunk `d0c555613386cd51` (`adapters/workbench/CMakeLists.txt`). Note the comment names the test by its *contents* ("SourceConfig serde test") while the actual file is `workbench-settings-test.cpp`; an operator grepping for the named test won't find it, and the comment names three units by filename, which is exactly the kind of detail that silently rots when a CMakeLists target is edited.

The blast radius: a maintainer reading green CI plus these comments will believe the serde test is actually exercised and the three units are actually compiled. If a future CMakeLists edit drops the serde test from `acfx_core_tests`, or drops a unit from the desktop target, CI stays green, the comment stays asserting coverage, and nothing flags the divergence — false coverage confidence with no guard. This is a documentation-drift trap rather than a present-tense bug: it is only wrong *if* the sibling chunks don't match, which I cannot confirm from this chunk. The triage action is to cross-check these two comments against `tests/CMakeLists.txt` and `adapters/workbench/CMakeLists.txt` in the named chunks; the durable fix is to not encode target/unit membership in a free-text CI comment at all (point at the CMake target instead), since the comment is unenforced prose duplicating build config.

### Compile-verify is correctly *labeled*, but the comment sits one inference away from "CI covers these surfaces"

Finding-ID: AUDIT-BARRAGE-claude-02
Status:     open
Severity:   low
Surface:    .github/workflows/ci.yml:40-42

The `desktop-build` comment ends "…the device/source/MIDI selection + persistence surfaces that are otherwise interactive manual acceptance." Read precisely, it is honest — it says *compile*-verifies and explicitly acknowledges these surfaces are "interactive manual acceptance." But the sentence places "the device/source/MIDI selection + persistence surfaces" as the direct object of a CI job, which a skimming reader can collapse into "CI exercises selection + persistence." It does not: `desktop-build` only proves the units compile, and the only behavioral coverage in the whole feature (per the diff) is the `SourceConfig` serde unit test in `core-tests` — selection wiring and the persist→restore round-trip have no automated assertion.

Blast radius is small because the wording does name "manual acceptance," and this is consistent with the project's stated stance (commit 92a865c marks interactive scenarios operator-owned). So this is hygiene, not a coverage gap to re-litigate. If tightening is wanted, append a clause making the boundary explicit — e.g. "compile-only; selection/persistence *behavior* is verified by operator-owned manual acceptance, not by this job" — so an unattended reader cannot infer behavioral coverage that isn't there.