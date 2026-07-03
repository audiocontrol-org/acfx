---
id: TASK-7
title: >-
  PDS attack/release descriptors carry ms magnitudes (0.1..200 / 1..2000) but
  are tagged ParamUnit::seconds (the enum has no milliseconds); runtime math is
  correct (ms->s at apply) but a host rendering units/automation ranges from the
  descriptor would mislabel/mis-scale. Tuning-pass/metadata: add a milliseconds
  ParamUnit or store seconds like CompressorEffect.
status: Done
assignee: []
created_date: '2026-07-03 16:17'
updated_date: '2026-07-03 17:24'
labels:
  - agent-found
  - 'type:gap'
dependencies: []
ordinal: 7000
---

## Implementation Notes

<!-- SECTION:NOTES:BEGIN -->
Closed: Fixed in review round 1: attack/release descriptors store seconds tagged ParamUnit::seconds (matching CompressorEffect); kMsToSec removed from the descriptor path.
<!-- SECTION:NOTES:END -->
