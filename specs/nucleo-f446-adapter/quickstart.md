# Quickstart — NUCLEO-F446RE adapter validation

**Feature**: `specs/nucleo-f446-adapter` · **Date**: 2026-08-23

How to build, flash, and validate this adapter, and which of the spec's success criteria each
step actually proves. Contract details live in [`contracts/nucleo-support.md`](./contracts/nucleo-support.md);
entity invariants in [`data-model.md`](./data-model.md). This file is a run guide, not an
implementation guide.

---

## Prerequisites

**Toolchain.** A complete ARM embedded toolchain **with libstdc++** on `PATH`. Homebrew's
`arm-none-eabi-gcc` is **C-only** and will not work — the toolchain file probes for this and
fails loud (FR-007). Use the official Arm GNU Toolchain and put its `bin/` first on `PATH`.

**Hardware, for anything past the host tests.** Two cables, always:

| Cable | Purpose | Why it is not optional |
|---|---|---|
| ST-Link USB (CN1) | Power, flashing, **and the 8 MHz MCO clock** | X3 is unpopulated, so the ST-Link MCU's MCO is the only accurate clock source (**D6**, FR-016) |
| USB-C breakout → PA11/PA12 on CN10 | The audio, MIDI, and serial device | The board has no USB connector of its own |

**Breakout wiring**: D− → PA11, D+ → PA12, GND → GND. **Leave VBUS unwired** — the board is
ST-Link powered, and feeding breakout VBUS into the 5 V rail puts two supplies in contention
(**D17**, FR-022).

> **Do not use the Arduino-labelled `D11`/`D12` pins.** Those are **PA7/PA6**. The board carries
> two unrelated numbering schemes and this is the trap the spike hit; PA11/PA12 are on the CN10
> morpho header.

**Offline builds**: `export CPM_SOURCE_CACHE=external/.cpm-cache` before configuring if the
sandbox has no network.

---

## 1. Host tests — no board required

This is where most of the adapter's substance is verified, and it runs anywhere.

```bash
cmake --preset test
cmake --build --preset test
ctest --preset test --output-on-failure
```

**Proves**: SC-009 (conversion incl. beyond-full-scale clipping, every ring behaviour and its
counter, the parameter shadow block) and SC-010 (no allocation in the audio path, via the
existing allocation sentinel). Covers US2, US3, and the host half of US6.

Targeted run while iterating:

```bash
ctest --preset test -R nucleo --output-on-failure
```

**Why this step exists at all**: the Daisy and Teensy adapters have **zero** behavioural tests —
CI proves only that they cross-compile. **D1** split this adapter specifically so this command
means something (FR-002, FR-049).

---

## 2. Cross-compile and link — no board required

```bash
cmake --preset nucleo
cmake --build --preset nucleo
```

**Proves**: SC-008 and US1 — every declared firmware target links, and one binary is produced per
effect (**D19**, FR-009).

Expected artifacts: one `.elf` per effect, plus a `.map`, in the `nucleo` preset's build
directory.

**Negative check** (US1 AS2) — confirm the libstdc++ probe fails loud rather than producing a
confusing error deep in a header:

```bash
PATH=/path/to/c-only-arm-toolchain/bin:$PATH cmake --preset nucleo
```

Expect a `FATAL_ERROR` naming the missing C++ standard library.

---

## 3. Flash a board

```bash
st-flash --format ienum write <firmware>.bin 0x8000000
```

(or `openocd`, or drag-and-drop to the ST-Link mass-storage volume — whichever the developer
already has). Exact invocation is a developer-environment detail, not a contract.

---

## 4. Clock bring-up and the fault path

**Normal**: after flashing, the board runs at 168 MHz with exactly 48 MHz on PLLQ (**D6**).

**Fault** (US8, SC-007): with the ST-Link cable disconnected — or the MCO otherwise absent — the
firmware must blink **LD2 (PA5)** in its distinct fault pattern and halt. It must **not**
enumerate.

This is the check that a failed board is distinguishable from an unpowered one **by eye, with no
debug probe**. If LD2 is dark, that is a failure of FR-015a, not an inconclusive result.

> Note the ordering this implies: the LED GPIO is initialized *before* the clock is validated, so
> it runs on the reset-default HSI. The blink cadence is approximate there, which is fine — and
> it is the only option, since without a locked PLL there is no USB to report over (research R3).

---

## 5. Enumeration

Connect the breakout cable. Expect **three** functions on one connection, with **zero** driver
installations (US4, SC-001):

- an audio device, stereo in / stereo out, 48 kHz (**D4**)
- a MIDI port (**D5**)
- a serial port (FR-018a, the telemetry channel)

macOS:

```bash
system_profiler SPUSBDataType | grep -A 12 -i acfx
ls /dev/cu.usbmodem*
```

Then open it duplex from any class-compliant client and confirm 2-in / 2-out at 48 kHz.

**Known-unverified**: driverless enumeration is hardware-verified on **macOS** only. Linux and
Windows are expected to work by class compliance but have not been confirmed — that is recorded
in the spec's Assumptions, not asserted here.

---

## 6. Audio passthrough

Select the device as both input and output in a class-compliant client and stream a known signal
through it.

**Proves**: SC-002 (US5) — audio reaches and leaves `process()` intact, transformed as the
compiled-in effect's transfer characteristic predicts.

Use a firmware whose effect is **deterministic and audible**, so "did it work" has an answer that
does not depend on taste.

---

## 7. Capture-only

Open **only** the input (mic) side, leaving the output side closed.

**Proves**: SC-005 (US7) — the received stream is **silence** with `inputStarved` incrementing;
never a hang, never stale audio. Then open the playback side and confirm duplex resumes without
a restart (US7 AS3).

This is legal host behaviour whose failure mode is a mysterious hang, which is why it is an
explicit validation step rather than an edge case someone remembers to try.

---

## 7a. USB lifecycle

With the device streaming, sleep the host and wake it; separately, force a bus reset.

**Proves**: SC-013 (US10) — streaming resumes with no power cycle, no audio buffered before the
suspend is replayed, and the counters survive the event so its cost stays measurable.

Suspend, resume, and bus reset all clear the rings and return the transport to **Priming**
(FR-051, FR-052, FR-053), so the device restarts from a defined state rather than draining a
stale partial ring — which is why "no replay on resume" needs no separate mechanism. Counters
are **not** reset by any of these events; only a power cycle clears them (FR-034a, FR-054).

---

## 8. Parameters over MIDI

Send CCs from any controller or host application.

**Proves**: SC-006 (US6) — a burst of changes within one block period lands at its **last** value
per parameter, and no parameter's pending change is evicted by another's.

> **The concrete CC-number convention is open question 7** and is the operator's call. Until it
> is settled, this step validates the *mechanism* (mapping → shadow block → one `setParameter`
> per dirty parameter per block), not a published CC chart.

---

## 9. Telemetry and the HIL harness

Read the counters directly off the serial port — line-oriented `key=value`, one snapshot per
line (research R7):

```bash
cat /dev/cu.usbmodem*        # or: screen /dev/cu.usbmodem* 115200
```

**Proves**: SC-004 — all eight `AudioTransportStats` fields readable from a host **without a
debug probe**.

Then run the harness (US9, FR-050):

```bash
# invocation TBD -- open question 6; the spike's tools/loopback_test.py is the starting point
```

It streams a known signal, reads the counter set back over CDC, and expresses error counts as a
**rate against `blocksProcessed`** rather than as bare totals (US9 AS2).

**Never wired into normal CI** — it needs a physical board (US9 AS3, FR-050). Where it lives and
how it is invoked is open question 6.

> **Sanity check before trusting any run**: if `worstBlockMicros` reads **0** after blocks have
> been processed, the DWT timing source failed to initialize. Treat that as a loud failure, not
> as "instantaneous" (research R6, invariant I-TS4).

---

## 10. The measurement pass — Phase H

Ring **capacity, water marks, and startup fill** are deliberately not pinned anywhere in this
spec or plan (**D23**, FR-035). They are derived here, once a board and the harness exist, by the
procedure in [`research.md`](./research.md) § R5:

1. Build with an instrumented capacity generous enough not to clip the distribution — the point
   is to *observe* the excursion, not to survive it.
2. Stream for a sustained run; record ring occupancy min/max/distribution over
   `blocksProcessed`, plus every counter.
3. Derive startup fill from the observed **lower** excursion and capacity from the observed
   **upper** excursion, each with headroom justified by the measured spread.
4. Re-run and confirm the counters hold at the operator's bar — which is itself **open question
   2**.

> The spike's ~0.2% dropout figure was measured under a **naive single buffer** and predicts
> nothing about the tuned design. Do not carry it forward as a target.

---

## Success-criteria coverage map

| Step | Board? | Criteria proved |
|---|---|---|
| 1 — host tests | no | SC-003, SC-009, SC-010, SC-012 |
| 2 — cross-compile | no | SC-008 |
| 4 — clock/fault | yes | SC-007 |
| 5 — enumeration | yes | SC-001 |
| 6 — passthrough | yes | SC-002 |
| 7 — capture-only | yes | SC-005 |
| 7a — USB lifecycle | yes | SC-013 |
| 8 — parameters | yes | SC-006 |
| 9 — telemetry/HIL | yes | SC-004, SC-011 |
| 10 — measurement | yes | closes FR-035; feeds open questions 1 and 2 |
