---
id: TASK-35
title: nucleo-input-startupfill-inert-under-greedy-drain
status: To Do
assignee: []
created_date: '2026-08-24 02:37'
labels:
  - agent-found
  - 'type:gap'
dependencies: []
ordinal: 35000
---

## Description

<!-- SECTION:DESCRIPTION:BEGIN -->
Nucleo input-ring startup fill is inert under T033's greedy drain, and this changes what T062/T063 measure. On promotion to Running the DSP block loop runs blocks back-to-back at loop rate, so the input ring's startup-fill cushion (kInputRingStartupFillFrames, usb-audio-service.h:58) is consumed within ~2 passes and steady-state input occupancy is bounded in [0,47] regardless of the constant's value. The real cushion lives entirely in the OUTPUT ring (~144 frames after the third block, held there because the IN endpoint drains on the SOF clock, not at loop rate). Consequence for the chartered measurement pass: FR-035/D23 tasks T062/T063 are to measure and pin ring capacity, water marks, and startup fill from HIL, but under this consumer policy the INPUT ring's startup fill affects only the first ~2 ms of a stream open and has no steady-state effect to measure. When T062/T063 run, measure and pin the OUTPUT ring's fill/water marks as the meaningful cushion; treat the input ring's startup fill as a Priming-exit threshold only. Surfaced by independent review of T033 (commit 0210a10). Non-blocking; a measurement-interpretation note, not a bug.
<!-- SECTION:DESCRIPTION:END -->
