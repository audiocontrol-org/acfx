# NUCLEO-F446RE USB Audio Adapter

## Overview

This directory contains the `acfx` adapter for the STMicroelectronics NUCLEO-F446RE development board, transforming a low-cost general-purpose MCU target into a USB audio interface. The adapter uses USB Audio Class 2 (UAC2) as the audio I/O path, with MIDI control and a CDC serial telemetry channel on the same composite device.

This document covers the operational realities of the setup, required wiring, and two load-bearing limitations that shape the implementation.

---

## Two-Cable Requirement (FR-016)

**The board requires TWO USB cables at runtime; this is not optional.**

| Cable | Purpose | Supplies |
|-------|---------|----------|
| **ST-Link USB** (CN1) | Clock source and power | 8 MHz MCO clock input (the ONLY accurate clock for the MCU), plus board power |
| **USB-C breakout** (PA11/PA12) | Audio, MIDI, and telemetry | The UAC2 audio device, MIDI interface, and CDC serial channel |

**Why both are mandatory:**

The NUCLEO-F446RE has an unpopulated external crystal oscillator footprint (X3). Without it, there is no accurate on-board clock. The board's design compensates by using the ST-Link debug probe's 8 MHz clock output (MCO) on the OSC_IN pin in HSE bypass mode. This clock feeds the PLL to generate exactly 48 MHz on PLLQ, which is the USB clock requirement. **If you remove the ST-Link cable, you lose the clock, the PLL fails to lock, and USB cannot enumerate.** The firmware detects this and halts with a visible fault pattern on LD2 (PA5) rather than silently degrading.

The user USB connection is separate because the board has no on-board USB connector. The OTG_FS peripheral's D−/D+ lines (PA11/PA12) must be broken out to an external USB-C or USB-A connector.

---

## USB Wiring: PA11/PA12 Breakout (FR-017)

Connect a standard USB-C breakout (or USB-A to custom breakout) to the **CN10 morpho header** as follows:

| Signal | Pin | Header Location |
|--------|-----|-----------------|
| **D−** | PA11 | CN10, row 11 |
| **D+** | PA12 | CN10, row 12 |
| **GND** | Any GND | CN10 or CN9 |
| **VBUS** | **Do NOT connect** | (see note below) |

**VBUS must remain unwired.** The board is powered entirely from the ST-Link connection. Feeding external VBUS into the 5 V rail creates a conflict between two independent power supplies, which can damage the board or the breakout. The firmware disables VBUS detection in the OTG_FS controller to force the USB session valid without waiting for VBUS.

---

## ⚠ THE PIN TRAP: D11/D12 ≠ PA11/PA12

**DO NOT use the Arduino-labelled `D11` and `D12` pins on the CN9 header.** This is the most common mistake on this board.

| Label | Actual Pin | What it connects to |
|-------|-----------|-------------------|
| D11 (Arduino silkscreen) | **PA7** | Generic GPIO, NOT USB |
| D12 (Arduino silkscreen) | **PA6** | Generic GPIO, NOT USB |
| **PA11** (OTG_FS D−) | CN10, row 11 | USB data line — this is what you need |
| **PA12** (OTG_FS D+) | CN10, row 12 | USB data line — this is what you need |

The board carries two unrelated numbering schemes. The Arduino headers (CN9) use the SAT/generic GPIO numbering that matches Arduino boards. The morpho headers (CN10/CN11) expose the actual port-A pins. **If you wire to D11/D12 on the silkscreen, your USB connection will fail silently.** The firmware will enumerate normally, but the host will see timeouts and dropouts because the data is not reaching the actual OTG_FS peripheral.

Always verify your wiring against the actual morpho header pin labels and the datasheet. When in doubt, check the board's reference manual or measure the actual MCU pins with a multimeter.

---

## Synchronous Clock Model (FR-001, FR-015)

The device presents both isochronous audio endpoints (IN and OUT) as **Synchronous** (USB synchronization attribute `0x0D`), with **NO feedback endpoint**. The device's timebase is derived exclusively from the USB Start-of-Frame (SOF) clock.

### Why Synchronous?

The device has **no analog converter** — all audio is pure DSP flowing between the host and the device. Therefore, there is no independent clock for the device to report. The honest, correct model is **synchronous**: both endpoints are paced by and locked to the USB SOF frame clock, and both directions deliver the nominal per-frame audio count for the selected sample rate.

This fixes a critical defect observed when the device is used in a CoreAudio aggregate (host → device → host for monitoring): the old asynchronous-with-no-feedback model forced the host's resampler to compensate for an effectively free-running return stream, causing consistent pitch downward shift (~0.5 kHz), broadband digital noise, and ~0.5 s of accumulated latency. With the correct synchronous declaration, the host locks to the USB clock and does not resample the device for rate — the aggregate performs only gentle ordinary drift correction, if any, and the audio returns clean and pitch-accurate.

**Note**: This supersedes decision **D20** from the base feature (see **D4/D20 Supersession** below).

---

## Multi-rate Support (FR-004)

The device now supports **two sample rates**:
- **48 000 Hz** (48 audio frames per 1 ms USB frame — nominal)
- **44 100 Hz** (44 100 audio frames per 1 000 USB frames — rational, averaging ~44.1 frames/frame)

The host selects the sample rate by writing to the device's **Clock Source Sampling-Frequency Control**. The device re-prepares its audio processing at the new rate and resumes streaming without requiring a power cycle or device re-plug.

### Fractional Cadence at 44.1 kHz

At 44.1 kHz, the per-USB-frame audio count is not an integer. The device implements the exact rational schedule via a phase accumulator: each USB frame carries either 44 or 45 audio frames, placed on the schedule such that the **accumulated frame count over 1000 USB frames equals exactly 44 100 frames**. This is NOT a naive 44/45 alternation (which averages 44.5 kHz); it is the deterministic rational-accumulator schedule that maintains the exact long-term rate.

---

## Multi-format Support (FR-005, FR-010)

The device now supports **two stereo PCM formats**:

| Format | Wire Width | bSubslotSize | bBitResolution | Alt-Setting |
|--------|-----------|------|---|---|
| 16-bit signed PCM | 2 bytes per sample | 2 | 16 | Alt-1 |
| Packed 24-bit signed PCM | 3 bytes per sample | 3 | 24 | Alt-2 |

The host selects the format by switching to the corresponding USB alternate setting. Each format is available at both 44.1 kHz and 48 kHz sample rates. Format changes are handled without a power cycle or device re-plug.

### 24-bit FIFO Budget Note

The 24-bit format at 48 kHz stereo in + out (~288 bytes per millisecond per direction) uses **~6.25% free margin** of the STM32F446 OTG-FS device FIFO-RAM budget (300/320 words at nominal 48 frames). This tight margin is verified in the build via `static_assert` (see `support/usb-out-path.h` and `support/usb-in-path.h`). If changes to buffer sizing or endpoint configuration reduce the remaining headroom below that margin, the fallback is to restrict 24-bit to 44.1 kHz only, or to 16-bit-only operation.

---

## D4/D20 Supersession (FR-015)

This feature supersedes two decisions from the base feature's specification. The exact original decisions, and how they are superseded, are recorded here.

### Decision D4: Single Format (Superseded)

**Original D4** (base feature `specs/nucleo-f446-adapter/spec.md`, FR-020):

> "The device MUST advertise **48 kHz, 16-bit, stereo only**, with one streaming alt-setting per streaming interface in addition to the zero-bandwidth alt-setting (**D4**)."

And (Open Questions, item 5):

> "**Additional audio formats.** 24-bit, 44.1 kHz, and 96 kHz all fit inside full-speed bandwidth for stereo. The operator scoped the advertised matrix to 48/16 (**D4**); 44.1 in particular forces variable packet sizes and is more work than it appears."

**How D4 is Superseded**:

The synchronous multi-rate/multi-format feature now declares **both 44.1 kHz and 48 kHz**, and **both 16-bit and 24-bit**, replacing the fixed single-format model. The 48/16 baseline remains as Alt-1 for maximum compatibility; 44.1 and 24-bit are added as Alt-2 and additional rate options. The rational 44.1 kHz cadence (44/45-frame packets per SOF schedule) is the mechanism that allows 44.1 kHz to coexist with 48 kHz without degrading the transport.

### Decision D20: Asynchronous with No Feedback (Superseded)

**Original D20** (base feature `specs/nucleo-f446-adapter/spec.md`, FR-024 through FR-027):

> - FR-024: "The host's SOF MUST be treated as the only sample clock; the device MUST NOT assert a rate of its own (**D20**)."
> - FR-025: "The OUT (host → device) stream MUST behave as an **adaptive sink** — whatever arrives is consumed (**D20**)."
> - FR-026: "The IN (device → host) stream MUST behave as an **asynchronous source**, producing one host-paced frame per SOF (**D20**)."
> - FR-027: "There MUST be **no feedback endpoint** (**D20**). Its absence is a design consequence of having no local clock, not an omission, and MUST be documented as such."

**How D20 is Superseded**:

D20 was a compromise model: the device used SOF as its timebase but declared itself asynchronous and produced variable packet sizes (0–49 frames per SOF, with zero-length packets when the output buffer was empty). This forced the host resampler to compensate for the device's effectively free-running return stream, causing the defects named above (pitch shift, noise, latency).

The new model **keeps the honest D20 principle** — the device has no independent clock, so SOF is the only timebase — but **corrects the declaration**: both endpoints are now **Synchronous**, not Asynchronous. The device commits to delivering the **exact nominal per-frame count** for the selected rate at each USB frame, making it rate-coherent with the host. There is still **no feedback endpoint** (the device has no clock to report), but the endpoint declarations and packet delivery are now honest and stable. This is what ended the pitch shift and noise in the CoreAudio aggregate.

---

## Latency and Ring Capacity (FR-008, FR-009)

### Round-Trip Latency

The latency measurements (round-trip time in both audio frames and milliseconds, and the device's internally pinned ring capacity/startup-fill/water-range values) are **pending the T022 hardware measurement**, which is operator-driven and has not yet been run on this transport.

**Measured latency (frames)**: *Pending T022 hardware measurement*

**Measured latency (milliseconds)**: *Pending T022 hardware measurement*

**Ring capacity (frames)**: *Pending T022 hardware measurement*

**Ring startup fill (frames)**: *Pending T022 hardware measurement*

**Ring steady-state water range (frames, min–max)**: *Pending T022 hardware measurement*

Once measured, these values will be pinned in this section and in the device's firmware configuration, and will become part of the delivered specification. The base feature's earlier placeholder values (1024-frame capacity, 98-frame startup fill) were conservative and are expected to be significantly smaller under the new synchronous, predictable packet cadence.

---

## Limitation 1: Parameter Seam — State-Valued Only (FR-044)

The parameter update mechanism uses a **per-parameter shadow block** with dirty flags. This design is correct for **state-valued parameters** (volume, filter cutoff, any control that sets a persisting value) but **is wrong for event-valued controls** (momentary triggers, tap tempo, any control that fires an event once).

### The Problem

The shadow block holds the **latest value** written by a parameter source (MIDI CC, a future knob input, or other control). If multiple updates arrive within one audio block, only the last value is applied. This is exact the right behaviour for a knob that sets a persisting state, but it silently drops transient events:

- A tap-tempo controller sends one CC per tap.
- If two taps arrive in the same 48-frame audio block, only the second is observed at the block boundary.
- The first tap is lost.

Or:

- A momentary gate or trigger arrives as a CC edge (0→127→0).
- The shadow sees only the value that was current when the block boundary hit.
- If the edge was brief and no block boundary aligned with it, the event vanishes.

### Current Scope

`acfx` today exports **normalized continuous parameters only** (`acfx::ParamId` → `float [0, 1]`). No effect declares event-valued parameters, so this limitation does not affect any current effect. **If you add an event-valued control, you must implement a separate mechanism for it — this shadow block is not sufficient.**

---

## Limitation 2: Single-Execution-Context Assumption (FR-047)

The parameter shadow block uses **no locks and no atomic operations.** It is safe only because of a specific architectural constraint: **all audio work, parameter updates, and peripheral servicing happen in a single execution context—the main loop.**

### The Constraint

- The OTG_FS interrupt handler only **enqueues** incoming data; it does not process it or call the shadow block's `set()` method.
- The main loop (single context) polls the interrupt queue, drains pending USB packets, updates the parameter shadow, and calls `process()`.
- No timer interrupt, no secondary ISR, and no concurrent background thread samples peripherals or calls any part of the audio path.

This is what makes the shadow block's bounded-array design safe without atomics or locks—there is only one reader, one writer, and they execute sequentially in the same context.

### The Breaking Point

**Sampling peripherals from a timer interrupt would break this.**

Example: A future board revision might add an ADC ISR that samples analog knobs every millisecond. If that ISR calls `shadow.set(paramIndex, normalized)` while the main loop is walking the dirty flags in `shadow.flush()`, a race condition occurs:

- The ISR might write to a slot's value while the main loop is reading it.
- The dirty-flag array might be in an inconsistent state mid-flush.
- Parameters could be applied twice, skipped, or corrupted.

**If you add a timer ISR or any concurrent access to the shadow block, you must revisit the memory-ordering discipline.** The current implementation assumes single-threaded execution and offers no protection for concurrent reads and writes.

This is an explicit design choice (not a limitation we expect to hit), recorded here so a future engineer does not silently break the assumption by adding an ISR.

---

## Build and Test

See [`specs/nucleo-f446-adapter/quickstart.md`](../../specs/nucleo-f446-adapter/quickstart.md) for the complete build, flash, and validation workflow.

Key entry points:

- **Host tests** (no board): `cmake --preset test && cmake --build --preset test && ctest --preset test --output-on-failure`
- **Cross-compile**: `cmake --preset nucleo && cmake --build --preset nucleo`
- **Flash**: `st-flash write <firmware>.bin 0x8000000`

---

## Architecture

The adapter is decomposed into two parts:

- **`nucleo-main.cpp`**: The silicon-touching shim — clock bring-up, GPIO configuration, USB stack initialization, the OTG_FS interrupt handler, and the main service loop. Not host-testable; kept minimal.
- **`support/`**: Platform-independent library — format conversion (interleaved int16 ↔ non-interleaved float), the ring buffer, the parameter shadow block, and MIDI CC mapping. Compiles and runs on the host for doctests.

This split ensures the untested-glue gap that exists in the Daisy and Teensy adapters is closed here via dedicated host test coverage.

