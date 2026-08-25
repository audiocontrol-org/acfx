---
id: TASK-38
title: nucleo-lifecycle-counters-across-event
status: To Do
assignee: []
created_date: '2026-08-24 17:54'
labels:
  - agent-found
  - 'type:gap'
dependencies: []
ordinal: 38000
---

## Description

<!-- SECTION:DESCRIPTION:BEGIN -->
SC-013 asks that the transport counters be READABLE ACROSS a USB lifecycle event (suspend/resume, bus reset). T057 verified the streaming-survives half live (bus reset + alt transitions) but NOT the counter-continuity half, because a bare SWD attach resets the RAM counters (st-util reset-on-connect) so before/after reads are unreliable. The clean readout is the CDC telemetry built in Phase 12 (T058). Follow-up: once T058 lands, read the counter set over CDC before and after a bus reset (and a sleep/wake) and confirm FR-054 (counters survive unchanged) end-to-end on hardware. Also covers the residual suspend/resume live bless (T053/T054), currently host-verified via unit+LI1-LI4 only.
<!-- SECTION:DESCRIPTION:END -->
