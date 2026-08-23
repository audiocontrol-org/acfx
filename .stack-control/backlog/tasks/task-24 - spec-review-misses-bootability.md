---
id: TASK-24
title: spec-review-misses-bootability
status: To Do
assignee: []
created_date: '2026-08-23 16:53'
labels:
  - agent-found
  - 'type:gap'
dependencies: []
references:
  - specs/nucleo-f446-adapter/spec.md
ordinal: 24000
---

## Description

<!-- SECTION:DESCRIPTION:BEGIN -->
The nucleo-f446-adapter spec passed a self-authored real-time/transport checklist (40 findings), cross-artifact analyze (7 findings) and a third-party review (7 findings), reaching 100 percent requirement and success-criterion coverage — yet omitted the C-runtime startup entirely. No requirement, task, contract or design decision mentions Reset_Handler, .data relocation, .bss zeroing, __libc_init_array or static-constructor init. FR-012 specifies a CMSIS-generated vector table and FR-013 explicitly declines ST's system_stm32f4xx.c, so the adapter owns bring-up, but nothing owns the reset path the vector table's entry 1 must point at. Consequence: US1's exit criterion ('links one .elf per effect') could not have been honestly met, and any image produced would have uninitialised globals and unrun C++ constructors. Root cause is the same shape as TASK-22: every review reasons over the artifacts' internal consistency and requirement coverage, never over whether the described artifact set is sufficient to build and boot the thing. Worth a bring-up completeness check for any bare-metal target: entry point, stack init, data/bss init, ctor init, main invocation, fault handlers. Resolved in-flight by adding the C-runtime startup task (operator-approved), so this item records the review-process gap, not the missing file. That task was authored as T010a and renumbered to T071 on 2026-08-23 (see TASK-28).
<!-- SECTION:DESCRIPTION:END -->
