# Tooling Feedback


## session-end 2026-06-25
- define front door dead-ends when base Spec Kit command layer isn't installed; setup still reports ready: yes. Filed deskwork#507.
- Spec Kit check-prerequisites.sh hard-fails on a non-numeric branch (platform-foundation), conflicting with Commandment III + stack-control one-branch model (TF-09). setup-plan/setup-tasks tolerate it via feature.json; only check-prerequisites enforces branch name. Resolved by using feature.json-resolved FEATURE_DIR.

## session-end 2026-06-26
- Spec Kit check-prerequisites.sh / create-new-feature.sh enforce numeric branch/spec prefixes (NNN-slug), which directly conflicts with acfx Commandment 3 (descriptive names, no numeric prefixes). Worked around via --paths-only + explicit SPECIFY_FEATURE_DIRECTORY=specs/<descriptive-slug>. The vendored Spec Kit scripts should honor a no-numbering mode per the project constitution.
- In-place 'cmake --preset <p>' after editing the CPM.cmake bootstrap fails with 'Unknown CMake command CPMAddPackage' from a stale build/<p>/CMakeCache; fix is rm -rf build/<p> then reconfigure. Hit on both test and desktop presets.
- govern over the workbench/MCU adapters oscillated 9 rounds (9->7->4->4->6->6->3->3->3) due to cross-chunk false positives (auditor can't see core/JUCE in another chunk: mode-knob clamp, live-input passthrough, reset re-apply). Concluded via documented --override. Cross-chunk visibility or a 'verified-in-sibling-chunk' affordance would cut wasted rounds.
