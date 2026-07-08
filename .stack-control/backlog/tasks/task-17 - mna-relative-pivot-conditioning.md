---
id: TASK-17
title: mna-relative-pivot-conditioning
status: To Do
assignee: []
created_date: '2026-07-08 00:41'
labels:
  - agent-found
  - 'type:gap'
dependencies: []
ordinal: 17000
---

## Description

<!-- SECTION:DESCRIPTION:BEGIN -->
code-review /high stop-gap (2026-07-07): the D1 relative singular-pivot threshold kRelEps*matScale (adopted from AugmentedSolver) can false-reject a well-posed but extreme-stiffness circuit (conductance ratio > ~1e12 in one circuit, e.g. 1e13-ohm beside 1-ohm) as singular. Inherent tradeoff of any single-scalar relative gate; trigger is outside the realistic audio envelope. Options: document the conditioning limit in the header contract, or consider per-row/column equilibration or a Schur-aware threshold for the branch (zero-diagonal) rows. Not a bug in the operating envelope. File core/primitives/circuit/mna/mna-system.h.
<!-- SECTION:DESCRIPTION:END -->
