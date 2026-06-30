---
id: TASK-1
title: svf-no-denormal-flush
status: To Do
assignee: []
created_date: '2026-06-29 08:43'
updated_date: '2026-06-29 08:43'
labels:
  - agent-found
  - 'type:bug'
dependencies: []
ordinal: 1000
---

## Description

<!-- SECTION:DESCRIPTION:BEGIN -->
The DaisySP-based SvfPrimitive/SvfEffect does not flush subnormal floats in its processing/state, so the measurement harness stability() metric reports {false, denormal} when fed a subnormal-decaying input. Surfaced by T013 (measurement-infrastructure). The harness is correct — this is a real RT-safety limitation in the effect (denormals are slow on some CPUs). Out of scope for measurement-infrastructure (FR-019: no audio-path code). Fix belongs in the SVF/primitive layer: add denormal flush-to-zero (or DAZ/FTZ) in the process path.
<!-- SECTION:DESCRIPTION:END -->

## Implementation Notes

<!-- SECTION:NOTES:BEGIN -->
- **Node:** design:feature/measurement-infrastructure
<!-- SECTION:NOTES:END -->
