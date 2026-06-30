---
id: TASK-4
title: waveshaper-lut-rebuild-on-init-guard
status: To Do
assignee: []
created_date: '2026-06-30 21:53'
labels:
  - agent-found
  - 'type:gap'
dependencies: []
ordinal: 4000
---

## Description

<!-- SECTION:DESCRIPTION:BEGIN -->
PR#8 review (non-blocking, ref PR#8): Waveshaper LUT rebuild-on-init is documented but a caller footgun — setShape/setEvaluation after init() take effect only on the next init() (preserves RT-safety). Consider a cheap debug/test-only guard that flags a stale-table read (e.g. assert in a non-RT debug build when shape/eval changed since the last init).
<!-- SECTION:DESCRIPTION:END -->
