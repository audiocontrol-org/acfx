---
id: TASK-39
title: nucleo-outputunderruns-reads-one-under-healthy-audio
status: To Do
assignee: []
created_date: '2026-08-25 01:54'
labels:
  - agent-found
  - 'type:gap'
dependencies: []
ordinal: 39000
---

## Description

<!-- SECTION:DESCRIPTION:BEGIN -->
On hardware (NUCLEO-F446RE, SVF firmware, 20s full-duplex pink-noise run, T058/T059 HIL), outputUnderruns fires at rate ~0.9999 against blocksProcessed (ou=20486, bp=20487) under HEALTHY operation. Audio is intact (SVF LP@1kHz verified live: LF <1k passes ~unchanged 0.088->0.081, HF >3k cut ~13 dB 0.052->0.0118; recorded RMS 0.083, no clipping), so the per-pass zero-fill is SMALL. Root cause is boundary misalignment: usb-in-path.h step 5 counts one outputUnderruns EVENT per pass in which ring.read() substitutes >=1 frame; the IN path pulls host-SOF-paced chunks (min(room,48)) that do not align to the DSP's 48-frame block boundary, so nearly every pull catches a small partial-block remainder and trips the event. Consequences: (1) the outputUnderruns EVENT counter is a poor health gate as defined -- it reads ~1.0 even when audio is perfect; the frame-level framesSubstituted (currently NOT in AudioTransportStats telemetry) would be the informative signal and could be exposed. (2) A ring startup-fill / watermark that keeps the output ring a full block ahead of the IN path (the T062 tuning work) would eliminate most of the misalignment underruns. Operator direction 2026-08-24: "if it works on hardware I don't care" about ring tuning -- audio works, so this is captured, NOT a blocker. Candidate work: reconsider outputUnderruns event definition and/or expose framesSubstituted; and/or T062 output-ring startup-fill tuning. Surfaced by the Phase-12 (US9) live hardware verification.
<!-- SECTION:DESCRIPTION:END -->
