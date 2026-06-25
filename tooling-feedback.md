# Tooling Feedback


## session-end 2026-06-25
- define front door dead-ends when base Spec Kit command layer isn't installed; setup still reports ready: yes. Filed deskwork#507.
- Spec Kit check-prerequisites.sh hard-fails on a non-numeric branch (platform-foundation), conflicting with Commandment III + stack-control one-branch model (TF-09). setup-plan/setup-tasks tolerate it via feature.json; only check-prerequisites enforces branch name. Resolved by using feature.json-resolved FEATURE_DIR.
