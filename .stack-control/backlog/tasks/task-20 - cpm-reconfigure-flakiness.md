---
id: TASK-20
title: cpm-reconfigure-flakiness
status: To Do
assignee: []
created_date: '2026-07-15 01:29'
labels:
  - agent-found
  - 'type:bug'
dependencies: []
references:
  - adapters/web T003; cmake/CPM.cmake
ordinal: 20000
---

## Description

<!-- SECTION:DESCRIPTION:BEGIN -->
A second 'emcmake cmake --preset web' (or any re-configure) of an existing build/ dir can cache CPM_SOURCE_CACHE=OFF, tripping CPM.cmake's CPM_DIRECTORY guard -> 'Unknown CMake command CPMAddPackage'. Latent in cmake/CPM.cmake (not Emscripten-specific); surfaced first by the adapters/web WASM build. Workaround: rm -rf build/web before configure. Fix candidate: pin CPM_SOURCE_CACHE unconditionally (drop the 'if(NOT DEFINED)' guard).
<!-- SECTION:DESCRIPTION:END -->
