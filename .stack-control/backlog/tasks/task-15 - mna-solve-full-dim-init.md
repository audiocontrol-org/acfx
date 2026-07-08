---
id: TASK-15
title: mna-solve-full-dim-init
status: To Do
assignee: []
created_date: '2026-07-08 00:41'
labels:
  - agent-found
  - 'type:gap'
dependencies: []
ordinal: 15000
---

## Description

<!-- SECTION:DESCRIPTION:BEGIN -->
code-review /high stop-gap (2026-07-07): MnaSystem::solve() zero-inits and copies the full kDim x kDim work/rhs even though only activeDim (count of referenced nodes + branches) is used; elimination only touches the activeDim submatrix. Scope the init/copy to activeDim to save per-solve hot-path work when template capacity exceeds a topology's actual size. Behavior-neutral. File core/primitives/circuit/mna/mna-system.h (solve()).
<!-- SECTION:DESCRIPTION:END -->
