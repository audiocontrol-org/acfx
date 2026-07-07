---
id: TASK-12
title: >-
  DiodeSpec type + siliconSignalDiode() duplicated in plain acfx:: across
  opamp-stage/opamp-config.h AND diode-clipper/clipper-config.h. Not reachable
  within opamp-stages (isolated), but a hard compile-time 'redefinition of
  acfx::DiodeSpec' error the first TU that composes both builder families (e.g.
  the TS808 feature pairing op-amp + diode clipper). Clean fix: extract ONE
  canonical DiodeSpec + siliconSignalDiode into a shared home (e.g.
  circuit/models/diode.h) that clipper, opamp, and future features share.
  Relates to TASK-11 (the parallel helper-ODR collision). Surfaced by
  code-review of opamp-stages.
status: To Do
assignee: []
created_date: '2026-07-06 09:33'
updated_date: '2026-07-06 09:33'
labels:
  - agent-found
  - 'type:gap'
dependencies: []
ordinal: 12000
---


