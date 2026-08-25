---
id: TASK-40
title: nucleo-hil-run-hil-unusable-on-multi-programmer-host
status: To Do
assignee: []
created_date: '2026-08-25 01:57'
labels:
  - agent-found
  - 'type:gap'
dependencies: []
ordinal: 40000
---

## Description

<!-- SECTION:DESCRIPTION:BEGIN -->
scripts/nucleo-hil/run-hil.sh (T059) cannot orchestrate a HIL run unattended on the actual dev host. Two problems surfaced during the Phase-12 live run (2026-08-24): (1) it calls 'st-flash --reset write' with NO --serial, but this host has TWO ST-Link programmers attached (a V3 in a stuck-SWD state + the F446's onboard V2); st-flash defaults to the first and fails 'Failed to enter SWD mode'. Needs a --stlink-serial passthrough to target the F446 by serial (066EFF...). (2) it die()s when multiple /dev/cu.usbmodem* nodes exist; this host has 2-3 (acfx CDC + ST-Link VCP(s) + the other board), so --serial-device must be passed, but the auto-detect path refuses rather than trying to identify the acfx port by reading for the key=value telemetry signature. The reader (read-serial-snapshot.sh) and evaluator (evaluate-transport-quality.sh) DID run verbatim on the real snapshots and produced the correct verdict; only the top-level flash+stream orchestration wrapper needed manual driving. Fix: add --stlink-serial to run-hil.sh (passed through to st-flash --serial), and make CDC auto-detect probe candidates for the 'iu=/bp=/tl=' telemetry signature rather than refusing on count>1. Small change; the novel evaluation logic is already hardware-verified.
<!-- SECTION:DESCRIPTION:END -->
