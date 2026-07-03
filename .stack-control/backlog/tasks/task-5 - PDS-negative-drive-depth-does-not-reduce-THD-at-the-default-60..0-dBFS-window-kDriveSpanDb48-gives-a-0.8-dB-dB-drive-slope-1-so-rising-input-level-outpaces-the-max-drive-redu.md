---
id: TASK-5
title: >-
  PDS negative drive depth does not reduce THD at the default -60..0 dBFS
  window: kDriveSpanDb=48 gives a 0.8 dB/dB drive slope < 1, so rising input
  level outpaces the max drive reduction and negative depth only slows the THD
  rise. SC-004 negative-direction softening holds only for windows narrower than
  the drive span (e.g. -30..0). Tuning-pass: reconcile kDriveSpanDb vs the
  default ref window, or expose a window/span control. Surfaced by T021/US5
  test.
status: To Do
assignee: []
created_date: '2026-07-03 15:07'
updated_date: '2026-07-03 15:07'
labels:
  - agent-found
  - 'type:bug'
dependencies: []
ordinal: 5000
---


