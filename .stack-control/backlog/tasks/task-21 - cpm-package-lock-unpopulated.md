---
id: TASK-21
title: cpm-package-lock-unpopulated
status: To Do
assignee: []
created_date: '2026-08-23 11:00'
labels:
  - agent-found
  - 'type:gap'
dependencies: []
references:
  - cmake/dependencies.cmake
ordinal: 21000
---

## Description

<!-- SECTION:DESCRIPTION:BEGIN -->
cpm-package-lock.cmake contains only its two-line header comment - zero package entries for any dependency. The repo's real pinning mechanism is an explicit GIT_TAG per CPMAddPackage in cmake/dependencies.cmake, which does carry full SHAs plus a fetch-verification comment block. But CLAUDE.md, the constitution and the nucleo design record all state a cpm-package-lock.cmake entry as the acfx convention, so the stated convention and the actual mechanism disagree repo-wide. Either populate the lock (CPM can generate it via CPMUsePackageLock) or drop it as vestigial and correct the prose. Pre-existing, affects every dependency, not specific to nucleo-f446-adapter. Surfaced by research R12 while authoring specs/nucleo-f446-adapter.
<!-- SECTION:DESCRIPTION:END -->
