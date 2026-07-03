---
id: TASK-7
title: >-
  PDS attack/release descriptors carry ms magnitudes (0.1..200 / 1..2000) but
  are tagged ParamUnit::seconds (the enum has no milliseconds); runtime math is
  correct (ms->s at apply) but a host rendering units/automation ranges from the
  descriptor would mislabel/mis-scale. Tuning-pass/metadata: add a milliseconds
  ParamUnit or store seconds like CompressorEffect.
status: To Do
assignee: []
created_date: '2026-07-03 16:17'
updated_date: '2026-07-03 16:17'
labels:
  - agent-found
  - 'type:gap'
dependencies: []
ordinal: 7000
---


