---
id: TASK-41
title: format-aware-shim-wiring-seam
status: To Do
assignee: []
created_date: '2026-08-25 16:13'
labels:
  - agent-found
  - 'type:gap'
dependencies: []
ordinal: 41000
---

## Description

<!-- SECTION:DESCRIPTION:BEGIN -->
T019 made usb-out-path.h/usb-in-path.h format-aware (AudioFormat param) but no task owned wiring g_currentAudioFormat + the 294B OUT buffer through the usb-audio-service.h shim; 24-bit shipped as dead code (silent 16-bit conversion on a 24-bit stream) until a controller-added follow-up (commit e8e1f7d) closed it. Same family as TASK-25 (link-success is not a boot-check): analyze/tasks reason over artifact content, not integration completeness. Suggested mechanical check: when a new format/param is threaded into a leaf function, assert every LIVE call site passes it (not just the unit tests).
<!-- SECTION:DESCRIPTION:END -->
