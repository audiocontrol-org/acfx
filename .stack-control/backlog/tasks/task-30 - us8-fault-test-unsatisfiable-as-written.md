---
id: TASK-30
title: us8-fault-test-unsatisfiable-as-written
status: To Do
assignee: []
created_date: '2026-08-23 21:32'
labels:
  - agent-found
  - 'type:bug'
dependencies: []
references:
  - 'specs/nucleo-f446-adapter/quickstart.md:103'
ordinal: 30000
---

## Description

<!-- SECTION:DESCRIPTION:BEGIN -->
US8's hardware acceptance criterion cannot be satisfied as written, on any rig, because it removes power and clock together and then treats the resulting dark LED as a failure.

quickstart.md:103 states the fault check as "with the ST-Link cable disconnected -- or the MCO otherwise absent -- the firmware must blink LD2 (PA5) in its distinct fault pattern and halt", and quickstart.md:107 adds "If LD2 is dark, that is a failure of FR-015a, not an inconclusive result." T052 restates the first form: "with the ST-Link cable disconnected the board blinks the fault pattern and does not enumerate."

But quickstart.md:22 records that ST-Link USB (CN1) supplies "Power, flashing, AND the 8 MHz MCO clock", and FR-022 / D17 deliberately leave the breakout's VBUS unwired so that the breakout supplies no power at all (feeding it into the 5 V rail would put two supplies in contention against ST-Link). Therefore disconnecting ST-Link removes the target's ONLY power source at the same instant it removes the clock. An unpowered MCU cannot blink anything, so LD2 is necessarily dark -- and the criterion declares a dark LD2 a failure. The test as written can only ever report failure, and it cannot distinguish a broken fault path from a board that is simply off. That is precisely the confusion SC-007 exists to remove ("a failed board is distinguishable from an unpowered one by eye, with no debug probe").

The satisfiable form is already present in the quickstart's own wording -- "or the MCO otherwise absent" -- but nothing states how to achieve it, and the primary phrasing everywhere else is the unsatisfiable one. On a NUCLEO-F446RE the intended condition (target powered, MCO gone) is reachable by moving JP5 to E5V and supplying 5 V externally on CN7, then unplugging CN1: the target keeps running while the ST-Link MCU that generates the MCO goes unpowered. That requires an external 5 V supply, which is a rig prerequisite the spec, the quickstart and T052 never mention.

Recommended fixes: (1) restate T052 and quickstart section 4 in terms of "MCO absent while the target remains powered", making the ST-Link-unplugged phrasing a consequence rather than the procedure; (2) record the external-5V-on-E5V rig prerequisite next to the two-cable requirement in FR-016/FR-017 and in T068's README task; (3) keep the "a dark LD2 is a failure" rule but scope it to the powered case, since it is meaningless otherwise.

Operator rig note as of 2026-08-23: no external supply available, so the genuine trigger cannot be exercised in this session. The blink pattern and halt can still be observed by deliberately injecting a PLL-lock failure in a temporary build while the board stays ST-Link powered -- that verifies FR-015a/FR-015b (pattern shape, halt, checkable by eye) but NOT FR-015's real MCO-absent trigger. Any verification recorded this session must say which of the two it covered.

Surfaced while sequencing T049-T052 with the board physically connected.
<!-- SECTION:DESCRIPTION:END -->
