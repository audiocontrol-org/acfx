---
id: TASK-25
title: link-success-is-not-a-boot-check
status: To Do
assignee: []
created_date: '2026-08-23 17:28'
labels:
  - agent-found
  - 'type:gap'
dependencies: []
references:
  - adapters/nucleo/CMakeLists.txt
ordinal: 25000
---

## Description

<!-- SECTION:DESCRIPTION:BEGIN -->
US1's acceptance criterion for the nucleo adapter is that every declared firmware target LINKS (T015: 'links one .elf per effect'). That criterion is satisfiable by an unbootable image. Observed live: building the vector table into a STATIC library produced acfx_nucleo.elf that linked with zero errors while readelf reported .isr_vector at size 0x000000 and nm showed g_vectorTable absent entirely, because a linker only extracts an archive member that resolves an undefined symbol and nothing references the vector table. The linker script's KEEP(.isr_vector) does not help: KEEP protects an already-included section from --gc-sections, it cannot cause archive extraction. Reset_Handler survived only incidentally, because ENTRY(Reset_Handler) creates the undefined reference that forces startup.o out of the archive. Net effect: .text was placed at 0x08000000, so the boot ROM would read an instruction word as the initial stack pointer. For any bare-metal target, 'it links' must not be the acceptance criterion. A real check asserts image STRUCTURE: vector table present and non-zero-sized at the reset address, first word equal to the expected initial SP, second word resolving to the reset handler, and .text placed after the table. Same family as TASK-24 (reviews reason over artifacts, not over whether the result can boot).
<!-- SECTION:DESCRIPTION:END -->
