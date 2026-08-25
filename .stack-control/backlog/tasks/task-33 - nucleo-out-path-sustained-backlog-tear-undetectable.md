---
id: TASK-33
title: nucleo-out-path-sustained-backlog-tear-undetectable
status: To Do
assignee: []
created_date: '2026-08-24 02:04'
labels:
  - agent-found
  - 'type:gap'
dependencies: []
ordinal: 33000
---

## Description

<!-- SECTION:DESCRIPTION:BEGIN -->
Nucleo OUT path: a torn USB payload merged into a SUSTAINED fifo backlog causes an undetectable, persistent L/R channel swap. Mechanism: the OUT read is bounded to 196 bytes (49 frames x 2 ch x 2 B), itself a multiple of the 4-byte stereo frame, and tu_fifo_read_n clamps to the queued count (external tinyusb 0.21.0, src/common/tusb_fifo.c:475 "limit to available count"). So a non-multiple-of-4 return is necessarily a SHORT return, i.e. a DRAINED fifo - which is the only signal FR-028a truncation has. If the backlog never falls below 196 bytes, every read returns a full, 4-aligned 196 bytes, the embedded tear is never detected, and every frame after it is byte-shifted so L and R are swapped for as long as the backlog persists. Neither clear-on-tear (tried and reverted, commit f1808e9 reverted by 54f200c) nor a frame-aligned read bound detects this: available() - (available() % 4) capped at 196 yields 196 when available is 390, i.e. an identical read; uncapped it reads 388 and straddles at frame 48, which is just the whole-fifo drain the bounded read replaced. A byte-carry across passes does not help either, because the tear LOST bytes from the wire, so the stream frame phase is permanently offset and nothing in the byte stream reveals it. Preconditions are exotic and compounding: (a) a torn payload, which requires a host-side protocol violation or a corrupted isochronous transfer, since packets are whole frames in normal operation, AND (b) a service loop persistently behind by more than one packet. Condition (b) is exactly what US5 T036 worstBlockMicros and the T032 g_outFifoWorstBacklogBytes high-water mark exist to make observable - use those measurements to decide whether this needs a defence at all. If it does, the defence must come from a signal outside the byte stream. Surfaced during T032 review round 3 (commits 35e2bea, 7359daa, f1808e9, 54f200c).
<!-- SECTION:DESCRIPTION:END -->
