---
id: TASK-2
title: cpm-stale-reconfigure
status: To Do
assignee: []
created_date: '2026-06-29 08:49'
updated_date: '2026-06-29 08:49'
labels:
  - agent-found
  - 'type:bug'
dependencies: []
ordinal: 2000
---

## Description

<!-- SECTION:DESCRIPTION:BEGIN -->
cmake --preset test (reconfigure over an existing build/test dir) fails with 'Unknown CMake command CPMAddPackage' because the CPM.cmake bootstrap (cmake/CPM.cmake file(DOWNLOAD)+include) does not re-establish CPMAddPackage on reconfigure. Workaround: rm -rf build/test before make test (a fresh configure works). build-only 'cmake --build --preset test' is unaffected. Surfaced repeatedly during measurement-infrastructure execution. Fix: make the CPM bootstrap reconfigure-safe (guard the include / cache the macro).
<!-- SECTION:DESCRIPTION:END -->

## Implementation Notes

<!-- SECTION:NOTES:BEGIN -->
- **Node:** design:feature/measurement-infrastructure
<!-- SECTION:NOTES:END -->
