---
id: TASK-16
title: mna-plan-terminalsof-reuse
status: To Do
assignee: []
created_date: '2026-07-08 00:41'
labels:
  - agent-found
  - 'type:gap'
dependencies: []
ordinal: 16000
---

## Description

<!-- SECTION:DESCRIPTION:BEGIN -->
code-review /high stop-gap (2026-07-07): MnaAssembler::plan() hand-extracts per-component terminal fields (a/b, p/n, inPlus/inMinus/out, anode/cathode) for validation; netlist.h terminalsOf(const Component&) already centralizes terminal extraction. Partial reuse (op-amp is 3-terminal and plan needs named terminals) but worth consolidating to one source of truth. File core/primitives/circuit/mna/mna-assembler.h.
<!-- SECTION:DESCRIPTION:END -->
