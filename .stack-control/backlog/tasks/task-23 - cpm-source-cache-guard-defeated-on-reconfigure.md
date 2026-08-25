---
id: TASK-23
title: cpm-source-cache-guard-defeated-on-reconfigure
status: To Do
assignee: []
created_date: '2026-08-23 16:12'
labels:
  - agent-found
  - 'type:bug'
dependencies: []
references:
  - cmake/CPM.cmake
ordinal: 23000
---

## Description

<!-- SECTION:DESCRIPTION:BEGIN -->
CMakeLists.txt sets the in-repo CPM cache with 'if(NOT DEFINED CPM_SOURCE_CACHE) set(CPM_SOURCE_CACHE ${CMAKE_SOURCE_DIR}/external/.cpm-cache) endif()'. That guard works only on a CLEAN first configure. CPM.cmake itself (v0.40.5 lines 154-161) declares CPM_SOURCE_CACHE as a CACHE PATH defaulting to OFF when the env var is unset, so from the second configure onward the variable IS defined (as OFF), the guard is skipped, OFF is falsy in cmake/CPM.cmake's 'if(CPM_SOURCE_CACHE)', and the bootstrap falls back to ${CMAKE_BINARY_DIR}/cmake/CPM_<ver>.cmake. Verified directly: after a clean configure resolved packages from external/.cpm-cache, a plain reconfigure wrote a fresh 42140-byte build/test/cmake/CPM_0.40.5.cmake fetched from GitHub. Consequences: (1) every reconfigure needs network, defeating the in-repo offline cache the file's own comment advertises; (2) file(DOWNLOAD) creates the destination file even when the fetch fails, and include() of a 0-byte file SUCCEEDS silently defining nothing, so a transient network blip surfaces as the misleading 'Unknown CMake command CPMAddPackage' at dependencies.cmake rather than as a download error. Observed live this session. Candidate fixes: use 'if(NOT CPM_SOURCE_CACHE)' rather than 'NOT DEFINED', or set it as a CACHE PATH with FORCE, or export CPM_SOURCE_CACHE into the environment before including CPM.cmake. Pre-existing; not introduced by nucleo-f446-adapter.
<!-- SECTION:DESCRIPTION:END -->
