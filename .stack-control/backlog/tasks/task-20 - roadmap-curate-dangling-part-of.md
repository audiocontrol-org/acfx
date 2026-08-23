---
id: TASK-20
title: roadmap-curate-dangling-part-of
status: To Do
assignee: []
created_date: '2026-08-23 05:19'
labels:
  - agent-found
  - 'type:bug'
dependencies: []
references:
  - ROADMAP.md
ordinal: 20000
---

## Description

<!-- SECTION:DESCRIPTION:BEGIN -->
stackctl curate --doc ROADMAP.md fails on main: cannot archive terminal item 'multi:feature/phase-digital-fundamentals' because 'design:feature/svf-vertical-slice' still references it via part-of. Pre-existing and reproducible on clean main; verified NOT caused by the hardware-targets/nucleo-f446-adapter additions, whose diff only appends two nodes at end of file. Fix is to re-point or remove that part-of edge so the terminal parent can archive. Found while adding roadmap nodes for the nucleo adapter.
<!-- SECTION:DESCRIPTION:END -->
