---
id: TASK-9
title: 'tape-dynamics: expose latency for host PDC'
status: To Do
assignee: []
created_date: '2026-07-04 01:34'
labels:
  - agent-found
  - 'type:gap'
dependencies: []
references:
  - core/effects/tape-dynamics/tape-dynamics-effect.h
ordinal: 9000
---

## Description

<!-- SECTION:DESCRIPTION:BEGIN -->
TapeDynamicsEffect has constant internal latency (Oversampler group delay 45/67.5/78.75 samples + dry-delay compensation ring) but exposes no latency accessor for host PDC; mix=0 is a latency-aligned delayed passthrough. Add host-facing latency reporting.
<!-- SECTION:DESCRIPTION:END -->
