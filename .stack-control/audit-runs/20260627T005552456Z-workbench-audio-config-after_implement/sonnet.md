### No new automated safety net — CI change is comment-only

Finding-ID: AUDIT-BARRAGE-claude-01
Status:     open
Severity:   informational
Surface:    .github/workflows/ci.yml:15-23, 37-45

The entire CI contribution of this diff for the `workbench-audio-config` feature is two YAML comment blocks — one on `core-tests`, one on `desktop-build`. No new steps, triggers, matrix entries, or jobs were added. Both jobs were already executing the relevant work before these commits: `core-tests` was already running `acfx_core_tests` (the serde test registered in T002/T003), and `desktop-build` was already compiling the workbench adapter units (registered in T001's CMakeLists changes). The comments accurately describe pre-existing coverage — they do not extend it.

The comment at the `desktop-build` job (lines 40–43) explicitly characterizes device selection, source selection, MIDI selection, and persistence as "otherwise interactive manual acceptance." This is honest and accurate, but it means that all four user stories' primary runtime behaviors (US1–US4) have no automated regression signal beyond successful compilation. A future change that silently breaks runtime persistence or source selection would pass CI without notice.

This is almost certainly the accepted trade-off for a JUCE audio application that requires real hardware and audio subsystems at runtime; commit `92a865c` ("mark interactive scenarios as operator-owned acceptance") confirms the disposition. The blast-radius is low: the comment is transparent rather than misleading, the serde unit test provides data-layer automated coverage, and the interactive gap is explicitly owned. Recording this as informational so future contributors understand that the CI green light on this feature certifies compile success and serde round-tripping only — not interactive behavior.