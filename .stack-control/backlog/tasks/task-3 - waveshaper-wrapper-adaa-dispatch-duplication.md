---
id: TASK-3
title: waveshaper-wrapper-adaa-dispatch-duplication
status: To Do
assignee: []
created_date: '2026-06-30 21:53'
labels:
  - agent-found
  - 'type:gap'
dependencies: []
references:
  - 'https://github.com/audiocontrol-org/acfx/pull/8'
ordinal: 3000
---

## Description

<!-- SECTION:DESCRIPTION:BEGIN -->
PR#8 review (non-blocking): Waveshaper and ADAAWaveshaper duplicate default Chebyshev order (=2), fold gain (=1.0), one-pole DC-blocker coefficient (R=0.995), gain-comp law (1/max(drive,eps)), and the per-shape dispatch switch. Acceptable for this phase; extract a shared helper/header if these start evolving to prevent drift.
<!-- SECTION:DESCRIPTION:END -->
