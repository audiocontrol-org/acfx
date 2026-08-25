---
id: TASK-43
title: nucleo-in-path-startup-ramp-transient
status: Done
assignee: []
created_date: '2026-08-25 19:55'
updated_date: '2026-08-25 20:26'
labels:
  - agent-found
  - 'type:gap'
dependencies: []
ordinal: 43000
---

## Description

<!-- SECTION:DESCRIPTION:BEGIN -->
After the 48-vs-49 dropout fix (TASK-42, commit 0c10f74), the IN path is steady-state sample-exact (0.0000% glitches over the sustained stream) but the FIRST ~1-1.5s of a stream still ramps with ~2.5% glitch samples before going perfectly clean (measured: per-eighth 2.6/0/0/0/0/0/0/0). This is the priming/cold-start transient, not steady-state. Candidate: the cold-start guard + output-ring startup fill interaction during Priming->Running. Low priority - brief clicks at stream open, then clean. Rig: scripts/nucleo-hil/loopback-audio-check.sh; SWD trace method (g_inPullTrace + openocd dump_image) documented in TASK-42 history.
<!-- SECTION:DESCRIPTION:END -->

## Implementation Notes

<!-- SECTION:NOTES:BEGIN -->
Closed: Investigated with SWD time-sampled pipeline trace (DWT-timestamped ring states + cumulative OUT/IN frame counts, dumped via openocd). PROVEN the device is clean throughout: ring primes in 2.3ms, ZERO Running->Priming resets in a clean single stream, regular SOF-cadence OUT delivery, clean 48-frame packets, zero substitutions. The ~1s startup glitch is NOT the device -- it is the host CoreAudio real-USB input-stream startup warmup (absent for the virtual BlackHole reference; device provably clean during it). Confirmed one-time: 0.0000% glitches from 1s onward. No firmware defect; no firmware fix. A false lead (mid-stream ring reset at 93ms) was a two-back-to-back-runs measurement artifact, not real.
<!-- SECTION:NOTES:END -->
