---
id: TASK-6
title: >-
  PDS linked+feedback drops cross-channel max: in routing.h the linked path
  computes maxSrc across channels but detectNorm() with detection==feedBack
  reads cores[0].prevOutput_ (channel 0's own output), discarding the
  cross-channel max. So StereoLink::linked + Detection::feedBack does not share
  detection driven by the loudest channel (contradicts FR-013/SC-011 for that
  combo). Niche combo; the design's feedback tap (final output y) vs linked-max
  semantics is genuinely ambiguous — needs an operator decision (max of channel
  outputs? primary only?).
status: To Do
assignee: []
created_date: '2026-07-03 16:17'
updated_date: '2026-07-03 16:17'
labels:
  - agent-found
  - 'type:bug'
dependencies: []
ordinal: 6000
---


