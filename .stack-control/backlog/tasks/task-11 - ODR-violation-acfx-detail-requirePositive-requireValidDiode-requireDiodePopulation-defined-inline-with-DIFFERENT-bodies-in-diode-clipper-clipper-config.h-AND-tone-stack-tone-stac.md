---
id: TASK-11
title: >-
  ODR violation: acfx::detail
  requirePositive/requireValidDiode/requireDiodePopulation defined inline with
  DIFFERENT bodies in diode-clipper/clipper-config.h AND tone-stack/tone-stack.h
  (both shipped). Linked into one binary, one body wins per symbol -> a
  validation error can print the wrong feature message (observed live during
  opamp-stages exec). Fix: namespace each feature helpers
  (clipper_detail/tonestack_detail) like opamp_detail, or extract one shared
  helper. Predates opamp-stages; affects shipped diode-clippers +
  passive-tone-stacks.
status: To Do
assignee: []
created_date: '2026-07-06 08:27'
updated_date: '2026-07-06 08:27'
labels:
  - agent-found
  - 'type:bug'
dependencies: []
ordinal: 11000
---


