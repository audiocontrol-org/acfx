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

