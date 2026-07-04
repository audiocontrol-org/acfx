---
id: TASK-10
title: >-
  saturation-voicings-test: 3 harmonic-voicing cases (incl. voicing=tubePreamp,
  ~line 187) fail intermittently ONLY inside the full acfx_core_tests run but
  pass in isolation — order/global-state-dependent flake (suspect denormal/FPU
  mode or a static left by a prior test). Pre-existing; found while governing
  component-abstractions, whose netlist.h change was verified innocent via git
  stash (same 3 fail without it). Not a component-abstractions defect.
status: To Do
assignee: []
created_date: '2026-07-04 04:50'
updated_date: '2026-07-04 04:50'
labels:
  - agent-found
  - 'type:bug'
dependencies: []
ordinal: 10000
---


