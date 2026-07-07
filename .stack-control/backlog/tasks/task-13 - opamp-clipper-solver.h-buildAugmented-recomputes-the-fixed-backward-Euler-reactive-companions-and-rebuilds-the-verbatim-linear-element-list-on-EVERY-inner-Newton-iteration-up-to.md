---
id: TASK-13
title: >-
  opamp-clipper-solver.h buildAugmented() recomputes the fixed backward-Euler
  reactive companions and rebuilds the verbatim linear-element list on EVERY
  inner Newton iteration (up to 50x/sample), though the design fixes companions
  for the whole timestep — only the diode Norton companions change per
  iteration. Correctness-neutral (companions recomputed from unchanged held
  history give identical values; validated to 1e-16), but wasteful in the
  per-sample solve. Hoist the fixed-companion assembly out of the Newton loop;
  re-linearize only the diodes inside. Non-normative lab code. Surfaced by
  code-review of opamp-stages.
status: To Do
assignee: []
created_date: '2026-07-06 09:33'
updated_date: '2026-07-06 09:33'
labels:
  - agent-found
  - 'type:gap'
dependencies: []
ordinal: 13000
---


