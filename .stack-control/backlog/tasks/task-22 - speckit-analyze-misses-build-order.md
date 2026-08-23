---
id: TASK-22
title: speckit-analyze-misses-build-order
status: To Do
assignee: []
created_date: '2026-08-23 16:04'
labels:
  - agent-found
  - 'type:gap'
dependencies: []
references:
  - specs/nucleo-f446-adapter/tasks.md
ordinal: 22000
---

## Description

<!-- SECTION:DESCRIPTION:BEGIN -->
Cross-artifact analyze verifies requirement/SC coverage and consistency, but NOT build-order feasibility, so a task can be placed in a phase whose prerequisites are produced several phases later and still read as analyze-clean. Concrete instance: nucleo-f446-adapter T009 sits in Phase 2 (declared 'no user story work can begin until this phase is complete') yet must wire five tests/core/nucleo-*-test.cpp sources that T016 (Phase 4), ~T019 (Phase 5) and three US6 tasks (Phase 8) create. tests/CMakeLists.txt lists sources explicitly rather than by glob, so executing T009 as written makes cmake --preset test fail for every target until Phase 8 lands. Survived the requirements checklist, analyze, and a third-party review because all three reason over artifact content, not over whether the task sequence is executable against the build graph. Worth a mechanical check: for each task that references a file path, assert the path exists or is created by an earlier-ordered task.
<!-- SECTION:DESCRIPTION:END -->
