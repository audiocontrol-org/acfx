---
id: TASK-42
title: nucleo-in-path-residual-dropout-glitch
status: Done
assignee: []
created_date: '2026-08-25 19:34'
updated_date: '2026-08-25 19:55'
labels:
  - agent-found
  - 'type:bug'
dependencies: []
ordinal: 42000
---

## Description

<!-- SECTION:DESCRIPTION:BEGIN -->
Synchronous USB-audio transport on hardware has a residual ~0.2% scattered silence-dropout glitch (output ring transiently empties, IN path substitutes silence). Tone itself is spectrally pristine (-131 dB noise floor, -94 dB THD); pitch/rate/format all correct. Confirmed NOT a clock/rate issue (single-clock full-duplex rig shows in==out, no sustained deficit) and NOT a rig artifact (rig reads -153 dB THD+N on a clean virtual loopback). It is a real-time buffering/timing problem. SEVEN fixes were tried on hardware, EVERY ONE worse than the baseline (startupFill=98, synchronous, one-block-per-pass): (1) sink-held anti-substitution gate; (2) sink-target pull; (3) capacity+deep startupFill; (4) UAC2 feedback endpoint async FIFO_COUNT (77% glitches - wrong for a clockless passthrough where in-rate MUST equal out-rate; reverted); (5-7) output-ring startupFill sweep 256/384 (monotonically worse) and SOF-locked pipeline via tud_sof_cb (worse - tud_sof_cb is delivered deferred through tud_task, adding latency jitter, not from the raw ISR). Baseline is a robust local optimum; audio-level interventions all regress it. NEXT DIAGNOSTIC: USB-packet-level capture (T002 Wireshark/tshark over XHC20, needs operator macOS host + possible SIP) to SEE the actual IN packet stream (short/ZLP/irregular?) instead of inferring it - every audio-level guess has failed, so ground-truth wire visibility is required. A true SOF-lock would process in the SOF ISR (not deferred) - untried, higher risk. Reusable rig: scripts/nucleo-hil/loopback-audio-check.sh + acfx-loopback.swift (single-clock full-duplex, pitch+THD+N).
<!-- SECTION:DESCRIPTION:END -->

## Implementation Notes

<!-- SECTION:NOTES:BEGIN -->
Closed: Root-caused via SWD occupancy trace (48-vs-49 phantom frame) and fixed (commit 0c10f74): conditional pull cap. Hardware-verified steady-state glitches 0.0000% (was ~0.19% continuous), pitch exact. Regression test IN-GRAN.
<!-- SECTION:NOTES:END -->
