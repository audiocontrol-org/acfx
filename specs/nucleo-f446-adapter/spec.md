> ‼ **acfx COMMANDMENTS — non-negotiable** ‼
> **1. COMMIT AND PUSH EARLY AND OFTEN** — version control is a distributed, journaled
> filesystem that safeguards your work, **NOT a sacred rite reserved for the blessed.**
> Small atomic commits, pushed promptly; never hoard unpushed work.
> **2. NO GIT HOOKS, EVER** — this repo uses zero git hooks; none exist, none get added.
> **3. DESCRIPTIVE NAMES, NEVER NUMERIC PREFIXES** — names carry information; fake sequence
> numbers (`001-`) imply false order and false precision (datestamps excepted).
> **4. ALL UI/UX WORK GOES THROUGH `/frontend-design`** — no exceptions, no offroading; every
> user-facing visual/interaction decision routes through the frontend-design skill.
> **5. SCOPE IS THE OPERATOR'S CALL** — never cut/defer/drop scope on "YAGNI" or "simplicity";
> when scope is open, present options and ASK. The operator decides scope, not the agent.
> (acfx Constitution, Principles I–V — `.specify/memory/constitution.md`.)

# Feature Specification: NUCLEO-F446RE adapter with USB audio I/O

**Feature Branch**: `nucleo-f446-adapter`

**Created**: 2026-08-22

**Status**: Draft

**Input**: Design record `docs/superpowers/specs/2026-08-22-nucleo-f446-adapter-design.md`
(operator-approved 2026-08-22 after third-party review; design-to-spec exit gate 7 of 7);
roadmap item `design:gap/nucleo-f446-adapter`, part of `multi:feature/hardware-targets` →
`multi:feature/progressive-dsp-platform`. Decisions **D1–D26** in that record are settled and
normative and are carried into the requirements below. The record's open questions are carried
forward as open, not resolved.

## Context

acfx compiles one platform-independent core into four adapters today: the JUCE workbench,
the JUCE plugin, Daisy, and Teensy. Both existing MCU adapters share two assumptions that
this target breaks.

**A vendor HAL owns the board.** libDaisy and the Teensy core each provide clock setup, an
audio callback, and peripheral access, so the adapter is a ~100-line shim. A NUCLEO-F446RE
provides none of that.

**Audio arrives through an on-board codec.** Both boards carry an I2S codec, so
`AudioCallback` is handed float buffers that already exist. A NUCLEO-F446RE has no audio
hardware at all.

The proposition is that **USB Audio Class is the audio interface** — the host becomes the
codec, and the board needs no analog parts. That turns a very cheap, very available
general-purpose development board into an acfx target, and it establishes a target class
distinct from "dev board with a codec on it".

### What the board imposes

| Constraint | Consequence |
|---|---|
| HSE crystal footprint (X3) unpopulated | The accurate clock is the ST-Link MCU's 8 MHz MCO into OSC_IN, in HSE **bypass** mode |
| ⇒ | **The ST-Link USB cable is mandatory at runtime**, not just for debugging. Two cables, always. |
| No audio codec | USB Audio Class is the **chosen** audio path. The MCU has SAI/I2S/ADC/DAC; this target deliberately does not use them |
| No pots, encoders, or display; one button (B1, PC13) | No local control surface to bind parameters to |
| USB OTG_FS only, PA11/PA12, 12 Mb/s | Full speed: one isochronous packet per 1 ms frame, ≤1023 bytes |
| 512 KB flash / 128 KB SRAM, Cortex-M4F single-precision | Ample for the current effects; a real budget question for the convolution and physical-modeling phases |
| No USB connector on the board | A USB-C breakout must be wired to PA11/PA12 on the CN10 morpho header |

### The adapter contract

Every adapter does the same four things, and `adapters/daisy/daisy-main.cpp` is the
exemplar: construct the CMake-injected `AppEffect`, `prepare()` it with a
`acfx::ProcessContext`, hand `process()` a non-interleaved `acfx::AudioBlock` of `float*`
per channel for in-place processing, and route control changes through
`setParameter(acfx::ParamId, normalized)`.

### The two frictions this feature has to resolve

**Data shape.** `AudioBlock` wants non-interleaved `float*` per channel. USB Audio carries
interleaved `int16`. Something must de-interleave and convert on the way in, and reverse it
on the way out, every 1 ms frame, without allocating.

**Verifiability.** `core/` has a host-side doctest suite. The Daisy and Teensy adapters have
**no behavioural tests at all** — CI proves only that they cross-compile and link. A
single-file adapter offers no seam a host test can reach, so matching that precedent would
import its blind spot. The chosen decomposition (D1) splits the adapter so that everything
which does not touch silicon is host-testable.

### Provenance

An exploratory spike in a separate repository (`~/work/stm32`, not proposed for import)
hardware-verified the load-bearing assumptions: the PA11/PA12 wiring, the HSE-bypass clock
arrangement at exactly 48.0000 MHz on PLLQ, driverless UAC2 enumeration on macOS with
duplex 48 kHz streaming, the silent removal of TinyUSB's `rx_done`/`tx_done` data-path
callbacks in 0.21.0, and Homebrew's newlib-less `arm-none-eabi-gcc`. Its measured 0.2%
dropout figure was taken under a naive single buffer and is **not** a prediction for the
tuned design.

## Clarifications

### Session 2026-08-23

- Q: How is the DSP block assembled relative to the USB packet cadence? → A: Fixed 48-frame
  blocks drawn from the ring buffer; the ring absorbs packet-size jitter and the 49-frame
  prepare is headroom, not the working block size. This is what makes D23's startup fill and
  water marks meaningful and what gives the four under/overrun counters a producer and a
  consumer to sit between.
- Q: Over what channel does the HIL harness read `AudioTransportStats` off the board? → A: A
  CDC serial function on the same composite device. TinyUSB's `cdc_uac2` example — already
  named in the design record as the descriptor template — carries CDC, so it costs one more
  IAD-grouped function and stays driverless on macOS, Linux, and Windows 10+.
- Q: What is the float-to-int16 conversion policy on the way back to the host? → A: Scale by
  32768, round to nearest, and clamp to [-32768, 32767]. Clamping is load-bearing: an effect
  that overshoots 1.0 would otherwise wrap to the opposite rail and produce loud broadband
  noise instead of benign clipping.
- Q: How does a fatal clock-bring-up failure make itself observable? → A: Blink the on-board
  LD2 (PA5) in a distinct fault pattern, then halt. If the PLL does not lock, USB cannot come
  up, so no USB channel — CDC included — can report the fault; the single LED is the only
  signal available without a debug probe.

## User Scenarios & Testing *(mandatory)*

### User Story 1 - Cross-compile an effect to a NUCLEO-F446RE firmware image (Priority: P1)

A developer selects the Nucleo toolchain and builds any acfx effect into a standalone
firmware binary, the same way `acfx_add_effect_daisy` already produces Daisy binaries. The
build fails loud and early on a toolchain that cannot produce the image, rather than
producing a subtly broken one.

**Why this priority**: Nothing else in this feature can be exercised on hardware until an
image exists, and CI's cross-compile-and-link gate is the first of the three verification
layers.

**Independent Test**: Configure with the Nucleo toolchain file and build; assert every
declared effect firmware target links. Assert a compiler without a C++ standard library
fails with a descriptive message rather than an obscure link error.

**Acceptance Scenarios**:

1. **Given** a working `arm-none-eabi` toolchain with libstdc++, **When** the project is
   configured with `cmake/toolchains/nucleo-f446.cmake` and built, **Then** one firmware
   binary per declared effect links successfully.
2. **Given** Homebrew's C-only `arm-none-eabi-gcc`, **When** configuration runs, **Then**
   the libstdc++ probe fails the configure step with a message naming the missing component.
3. **Given** a new effect declared through `acfx_add_effect_nucleo`, **When** the build
   runs, **Then** a separately named firmware target is produced for it.

---

### User Story 2 - Convert between USB sample format and the effect's block format (Priority: P1)

The support library converts interleaved 16-bit stereo USB payloads into the non-interleaved
float channel pointers `AudioBlock` requires, and back again, for any packet size the
transport can deliver. This runs on the host under doctest, with no board attached.

**Why this priority**: This is the first of the two frictions named above, it is on the
critical path for every audio frame, and it is the slice that proves the two-layer
decomposition actually bought host-side testability.

**Independent Test**: Round-trip known int16 buffers through de-interleave → interleave in a
host doctest and assert exact recovery; assert no allocation occurs.

**Acceptance Scenarios**:

1. **Given** an interleaved stereo int16 buffer of N frames, **When** it is de-interleaved
   and converted to float, **Then** each channel's samples appear in order in its own
   contiguous float buffer, normalized consistently with the reverse conversion.
2. **Given** float channel buffers, **When** they are converted and interleaved back to
   int16, **Then** the original int16 values are recovered exactly for values representable
   in the format.
3. **Given** a float sample outside [-1.0, 1.0), **When** it is converted to int16, **Then**
   it is clamped to the format's limits rather than wrapping to the opposite rail.
4. **Given** a packet carrying any frame count from 0 to 49 inclusive, **When** conversion
   runs, **Then** exactly that many frames are converted and no read or write occurs past
   the payload.
5. **Given** any conversion call, **When** it executes, **Then** no heap allocation occurs.

---

### User Story 3 - Buffer audio across the USB and DSP rates with observable behaviour (Priority: P1)

The support library holds a statically sized ring buffer between the USB packet cadence and
the DSP's own fixed 48-frame block cadence. When the two do not line up, the buffer's
response is defined, bounded, and counted — never silent and never unbounded. This too runs
on the host.

**Why this priority**: The transport is adaptive with no feedback endpoint (D20), so rate
mismatch is a normal operating condition rather than an exceptional one. Undefined behaviour
here is the difference between a measurable quality bar and mysterious dropouts.

**Independent Test**: Drive the ring buffer from a host doctest with scripted
producer/consumer patterns — starvation, saturation, and balanced flow — and assert the
emitted samples and the counter deltas.

**Acceptance Scenarios**:

1. **Given** the DSP requests a block and the ring holds fewer frames than requested,
   **When** the read executes, **Then** silence fills the shortfall and `inputUnderruns`
   increments.
2. **Given** USB delivers frames faster than the DSP drains them and the ring is full,
   **When** the write executes, **Then** the oldest frames are dropped and `inputOverruns`
   increments.
3. **Given** USB polls the IN endpoint and the output ring is empty, **When** the read
   executes, **Then** silence is emitted and `outputUnderruns` increments.
4. **Given** the DSP produces faster than USB drains and the output ring is full, **When**
   the write executes, **Then** the oldest frames are dropped and `outputOverruns`
   increments.
5. **Given** any of the above, **When** it occurs, **Then** the substitution is counted and
   observable rather than silently absorbed, and no heap allocation or lock is used.

---

### User Story 4 - Enumerate as a driverless class-compliant audio device and stream duplex (Priority: P1)

A user plugs the board into a host with two cables — ST-Link for power and clock, USB-C
breakout for audio — and the board appears as a stereo-in / stereo-out 48 kHz audio device
with no driver installation, alongside a MIDI port on the same connection.

**Why this priority**: This is the feature's central proposition. Without driverless
enumeration and duplex streaming, the target has no audio interface at all.

**Independent Test**: Attach the board to a host and open it duplex at 48 kHz from a
class-compliant audio client; confirm the device appears without driver installation and
that both a MIDI port and an audio device are present.

**Acceptance Scenarios**:

1. **Given** a board running the firmware and both cables attached, **When** it is connected
   to a host, **Then** it enumerates as a composite device exposing a UAC2 audio function, a
   USB MIDI function, and a CDC serial function, with no driver installation required.
2. **Given** the enumerated device, **When** a host queries its formats, **Then** exactly one
   advertised format is offered — 48 kHz, 16-bit, stereo — on each streaming direction.
3. **Given** the device is open, **When** a host streams audio in both directions, **Then**
   audio flows duplex, 2-in and 2-out.
4. **Given** the board is powered from ST-Link with the breakout's VBUS unwired, **When** it
   is connected, **Then** the USB session is treated as valid and enumeration proceeds.

---

### User Story 5 - Process live host audio through an acfx effect (Priority: P1)

Audio arriving from the host passes through the effect's `process()` and returns to the host
altered, once per audio block, with the effect prepared for the largest block the transport
can produce.

**Why this priority**: This is the payoff — it is what makes the board an acfx *target*
rather than a USB passthrough.

**Independent Test**: Load a firmware whose effect has an audible, deterministic transfer
characteristic; stream a known signal through the device from a host and confirm the output
matches the expected transformation.

**Acceptance Scenarios**:

1. **Given** the firmware starts, **When** the effect is prepared, **Then** it is prepared
   with a maximum block size of 49 frames, 48 kHz, 2 channels.
2. **Given** the ring holds at least a full block, **When** the DSP runs, **Then** it draws a
   fixed 48-frame block — the packet sizes that filled the ring do not set the block size.
3. **Given** a block of frames is assembled, **When** `process()` runs, **Then** it operates
   in place on non-interleaved float channel pointers and performs no heap allocation.
4. **Given** a packet carrying fewer than the nominal 48 frames, or zero frames, **When** it
   is handled, **Then** exactly the frames present are written into the ring and the DSP
   cadence is unaffected.
5. **Given** a block completes, **When** timing is recorded, **Then** `blocksProcessed`
   increments and `worstBlockMicros` reflects the longest block observed.

---

### User Story 6 - Change effect parameters live over USB MIDI (Priority: P2)

A user turns a knob on a MIDI controller, or sends a CC from a host application, and the
corresponding effect parameter changes without audio interruption.

**Why this priority**: The board has no local control surface, so without this a firmware is
frozen at its default parameter values. It is P2 because audio passthrough is demonstrable
without it.

**Independent Test**: Host doctest the parameter seam directly — write to slots, walk dirty
flags, assert exactly the changed parameters are applied — plus a hardware confirmation that
an incoming CC moves the intended parameter.

**Acceptance Scenarios**:

1. **Given** the device is enumerated, **When** a MIDI CC arrives, **Then** its mapped
   `ParamId` slot is written with the normalized value.
2. **Given** one or more slots were written since the last block, **When** the next audio
   block boundary arrives, **Then** `setParameter` is called exactly once for each parameter
   whose slot is dirty, and not at all for clean ones.
3. **Given** a parameter is written many times within one block period, **When** the block
   boundary arrives, **Then** the last written value is applied and no intermediate value is
   stranded.
4. **Given** many parameters change in the same block period, **When** the block boundary
   arrives, **Then** every one of them is applied — no parameter's pending update is evicted
   by another's.
5. **Given** the parameter store, **When** it is sized, **Then** it is bounded by
   construction at the effect's parameter count and cannot overflow.

---

### User Story 7 - Behave correctly when the host opens the capture stream alone (Priority: P2)

A host opens the device's microphone interface without opening its speaker interface. The
device continues to emit a well-defined stream rather than hanging, repeating stale audio,
or producing undefined output.

**Why this priority**: This is legal host behaviour that a topology deriving the IN stream
from the OUT stream cannot otherwise answer, and its failure mode is a mysterious hang. It
is P2 because the duplex case is the primary journey.

**Independent Test**: Open the capture stream alone from a host and confirm the received
stream is silence, with the corresponding counter incrementing.

**Acceptance Scenarios**:

1. **Given** the host sets the mic interface to its streaming alt-setting and leaves the
   speaker interface at its zero-bandwidth alt-setting, **When** the IN endpoint is polled,
   **Then** silence is emitted.
2. **Given** silence is emitted for this reason, **When** it occurs, **Then** `inputStarved`
   increments.
3. **Given** this state persists, **When** the host later opens the speaker interface,
   **Then** normal duplex operation resumes without a restart.

---

### User Story 8 - Fail loudly rather than run on an inadequate clock (Priority: P2)

If the clock cannot be brought up to the exact configuration USB requires, the firmware
stops in an observable way instead of continuing on an internal oscillator.

**Why this priority**: A silent fallback to the internal RC oscillator yields a device that
enumerates erratically and produces intermittent, hard-to-diagnose audio faults — the exact
class of failure acfx's no-fallbacks rule exists to prevent. P2 because the happy path is
hardware-verified.

**Independent Test**: Force the lock check to fail and confirm the firmware halts observably
and never proceeds to USB initialization.

**Acceptance Scenarios**:

1. **Given** the external clock source is absent or the PLL fails to lock, **When** clock
   bring-up runs, **Then** the on-board LD2 blinks a distinct fault pattern and the firmware
   halts.
2. **Given** such a failure, **When** it occurs, **Then** the firmware does **not** fall back
   to the internal oscillator and does **not** proceed to USB initialization — so the fault
   is reported by the LED and cannot be reported over any USB channel.
3. **Given** normal bring-up, **When** it completes, **Then** the system clock is 168 MHz and
   the USB clock is exactly 48 MHz.

---

### User Story 9 - Verify transport quality against a hardware-in-the-loop harness (Priority: P3)

A developer with a board attached runs a harness that streams audio through the device and
asserts against the transport's own counters, rather than inferring glitches from signal
correlation after the fact.

**Why this priority**: It is the third verification layer and the only one that can measure
the real transport, but it requires physical hardware and therefore cannot run in normal CI.

**Independent Test**: Run the harness against an attached board and confirm it reports the
full counter set and passes or fails against the configured bar.

**Acceptance Scenarios**:

1. **Given** an attached board, **When** the harness runs, **Then** it streams a known signal
   through the device and reads back the full `AudioTransportStats` counter set over the
   device's CDC serial function.
2. **Given** counter readings, **When** the harness evaluates them, **Then** it expresses
   error counts as a rate using `blocksProcessed` as the denominator rather than as raw
   totals.
3. **Given** the harness runs, **When** CI runs without a board attached, **Then** the harness
   is not invoked as part of the normal CI job.

---

### Edge Cases

- **Zero-length packet.** The host sends an isochronous packet carrying no frames. Handled as
  a 0-frame payload; no read or write occurs, and it is not treated as an error.
- **Maximum-size packet.** A 49-frame payload arrives, exceeding the nominal 48. The endpoint
  is sized for it by the stack's own arithmetic; it must be consumed in full.
- **Neither stream open.** Both interfaces sit at their zero-bandwidth alt-settings. No audio
  is produced or consumed; the device remains enumerated and responsive.
- **Playback-only.** The speaker interface is open and the mic interface is not; OUT audio is
  consumed and processed with no IN stream to feed.
- **Effect output beyond full scale.** `process()` produces a float sample outside
  [-1.0, 1.0). It is clamped at the format boundary rather than wrapping (FR-038a).
- **Ring holds a partial block.** Frames have arrived but fewer than the fixed 48 the DSP
  draws. The block is completed with the defined underflow substitution and counted; the DSP
  cadence does not stretch to match the ring (FR-030a, FR-031).
- **Effect with zero parameters.** The parameter shadow block is bounded at the parameter
  count, which is zero; the dirty-flag walk is a no-op.
- **Nothing reading the CDC channel.** No host has the serial port open. Telemetry writes must
  not block or stall the audio path when the channel is unread.
- **MIDI CC with no mapping.** A CC arrives that maps to no `ParamId`. It is ignored without
  disturbing any parameter slot.
- **Sustained rate mismatch.** The host's SOF cadence and the assembled block cadence drift
  persistently in one direction, so the ring trends toward starvation or saturation. Behaviour
  is the defined substitution plus a steadily incrementing counter — degradation is visible in
  the statistics rather than silent.
- **Block overrun.** An effect's `process()` takes longer than the frame period.
  `worstBlockMicros` records it, making the CPU budget observable rather than inferred from
  dropout symptoms.
- **ST-Link cable removed at runtime.** The clock source disappears from a running system.
  Recovery behaviour is an open question (see Open Questions); the requirement here is only
  that the failure not be silently masked.

## Requirements *(mandatory)*

### Functional Requirements

#### Decomposition and repository boundaries

- **FR-001**: The adapter MUST be decomposed into two parts (**D1**): `nucleo-main.cpp`,
  holding only what touches silicon — clock bring-up, GPIO alternate-function setup, USB stack
  initialization, the OTG_FS interrupt handler, and the main service loop — and a separate
  `acfx_nucleo_support` target holding the sample-format conversion, de-interleaving, the audio
  ring buffer, the parameter seam, and the MIDI-CC mapping.
- **FR-002**: `acfx_nucleo_support` MUST compile and run on the host, so it can be covered by
  the existing host doctest suite (**D1**, **D18**).
- **FR-003**: `nucleo-main.cpp` MUST be kept as small as its untestability implies — logic that
  can live in the support library MUST NOT live in the shim.
- **FR-004**: `acfx_core` MUST NOT acquire any knowledge of USB, TinyUSB, or the board;
  dependencies point strictly inward. The adapter may depend on core; core must never depend on
  the adapter.
- **FR-005**: Each shipped source file MUST remain within the ~300–500 line budget.

#### Build, toolchain, and dependencies

- **FR-006**: A toolchain file `cmake/toolchains/nucleo-f446.cmake` MUST exist, targeting
  Cortex-M4 with the `fpv4-sp-d16` FPU, hard float ABI, and `-fno-exceptions -fno-rtti`
  (**D11**), mirroring `cmake/toolchains/daisy.cmake`.
- **FR-007**: The toolchain MUST reuse the Daisy toolchain's libstdc++ probe, failing the
  configure step with a descriptive message on a compiler shipped without a C++ standard
  library (**D12**).
- **FR-008**: A CMake factory `acfx_add_effect_nucleo` MUST exist, mirroring the shape of
  `acfx_add_effect_daisy` in `cmake/acfx-effect-targets.cmake` (**D11**).
- **FR-009**: The build MUST produce **one firmware binary per effect** through that factory
  (**D19**), with the effect type injected at configure time as `ACFX_EFFECT_TYPE`.
- **FR-010**: Dependencies MUST be acquired via CPM with corresponding `cpm-package-lock.cmake`
  entries (**D8**): TinyUSB pinned at **0.21.0**, `cmsis_device_f4`, and CMSIS core. Submodules
  MUST NOT be used for these.
- **FR-011**: The build MUST NOT track TinyUSB `master` (**D9**).
- **FR-012**: The interrupt vector table MUST be generated from the CMSIS `IRQn_Type` enum so
  that it covers the full external-interrupt range including `OTG_FS_IRQn` (**D13**). A
  core-exceptions-only table is insufficient.
- **FR-013**: The adapter MUST own and define `SystemCoreClock` with the true configured clock
  frequency (**D14**), because ST's `system_stm32f4xx.c` is not compiled and the USB stack
  derives PHY turnaround timing from it.

#### Clock

- **FR-014**: Clock bring-up MUST configure HSE in **bypass** mode against the ST-Link MCU's
  8 MHz MCO on OSC_IN, with PLL M=4, N=168, P=2, Q=7, yielding 168 MHz SYSCLK and exactly
  48 MHz on PLLQ (**D6**).
- **FR-015**: PLL-lock failure MUST be treated as **fatal**. The firmware MUST NOT fall back
  to the internal oscillator and MUST NOT proceed to USB initialization (**D7**).
- **FR-015a**: A fatal clock failure MUST be made observable by blinking the on-board LD2
  (PA5) in a distinct fault pattern before halting (Clarifications 2026-08-23). This is the
  only channel available: without a locked PLL, USB cannot enumerate, so neither CDC nor MIDI
  can carry the fault. The LED driver required for this MUST be the minimum needed to signal
  the pattern, consistent with FR-003.
- **FR-016**: Documentation MUST state that the ST-Link USB cable is **required at runtime**,
  not merely for debugging, because it supplies the clock — two cables, always.
- **FR-017**: Documentation MUST state the required USB-C breakout wiring to PA11/PA12 on the
  CN10 morpho header, and MUST note that the Arduino-labelled `D11`/`D12` pins are PA7/PA6 and
  are **not** the USB pins.

#### USB device and descriptors

- **FR-018**: The device MUST enumerate as a composite device grouping a UAC2 audio function
  and a USB MIDI function via Interface Association Descriptors (**D5**).
- **FR-018a**: The composite device MUST additionally expose a **CDC serial function**, also
  IAD-grouped, carrying transport telemetry and diagnostics (Clarifications 2026-08-23). This
  extends **D5**'s composite arrangement by one function; TinyUSB's `cdc_uac2` example — the
  descriptor template the design record already names — carries CDC, and the function stays
  driverless on macOS, Linux, and Windows 10+.
- **FR-019**: The device MUST be class-compliant, requiring no host driver installation.
- **FR-020**: The device MUST advertise **48 kHz, 16-bit, stereo only**, with one streaming
  alt-setting per streaming interface in addition to the zero-bandwidth alt-setting (**D4**).
- **FR-021**: The UAC2 descriptor MUST be written locally from the `TUD_AUDIO20_DESC_*`
  primitives (**D10**), because no shipped template provides stereo-in with stereo-out.
- **FR-022**: VBUS detection MUST be disabled so the USB session is forced valid, since the
  breakout's VBUS is deliberately not wired (**D17**). Breakout VBUS MUST NOT be fed into the
  board's 5 V rail.
- **FR-023**: The audio data path MUST be serviced by polling the stack's read and write
  entry points. The removed `rx_done`/`tx_done` data-path callbacks MUST NOT be relied on —
  code written against them links silently and leaves the audio path dead with no diagnostic.

#### Transport contract

- **FR-024**: The host's SOF MUST be treated as the only sample clock; the device MUST NOT
  assert a rate of its own (**D20**).
- **FR-025**: The OUT (host → device) stream MUST behave as an **adaptive sink** — whatever
  arrives is consumed (**D20**).
- **FR-026**: The IN (device → host) stream MUST behave as an **asynchronous source**,
  producing one host-paced frame per SOF (**D20**).
- **FR-027**: There MUST be **no feedback endpoint** (**D20**). Its absence is a design
  consequence of having no local clock, not an omission, and MUST be documented as such.
- **FR-028**: The implementation MUST accept packet payloads carrying **0 to 49 stereo frames
  inclusive**, including short and zero-length packets (**D21**). No code path may assume a
  fixed 48-frame payload.
- **FR-029**: The host opening the **capture stream alone** (mic at its streaming
  alt-setting, speaker at zero-bandwidth) MUST be handled explicitly: the IN endpoint emits
  silence and `inputStarved` increments (**D22**).

#### Audio buffering and error accounting

- **FR-030**: Audio MUST be buffered through a **statically sized** ring buffer. No heap
  allocation and no locks may occur in the audio path (**D16**), consistent with the existing
  no-allocation test discipline.
- **FR-030a**: The ring buffer MUST sit between the USB packet cadence and an **independent,
  fixed 48-frame DSP block cadence** (Clarifications 2026-08-23). Variable packet sizes
  (FR-028) are absorbed by the ring and MUST NOT propagate into the block size. This decoupling
  is what gives FR-035's startup fill and water marks their meaning and what gives FR-031's
  four counters a distinct producer and consumer on each side.
- **FR-031**: Input underflow MUST emit silence; input overflow MUST drop the oldest frames;
  output underflow MUST emit silence; output overflow MUST drop the oldest frames (**D24**).
- **FR-032**: Every substitution in FR-031 MUST increment its corresponding counter. No
  substitution may be silent (**D24**). acfx's "raise a descriptive error rather than fall
  back" rule cannot be applied literally in a path that must emit something in bounded time
  and cannot throw; the correct reading is that the substitution is **defined and observable**,
  not absent.
- **FR-033**: The adapter MUST expose an `AudioTransportStats` record carrying
  `inputUnderruns`, `inputOverruns`, `outputUnderruns`, `outputOverruns`, `inputStarved`,
  `blocksProcessed`, and `worstBlockMicros`.
- **FR-033a**: The full `AudioTransportStats` set MUST be readable at runtime over the CDC
  serial function (FR-018a), in a form a host-side harness can parse (Clarifications
  2026-08-23). Reading the counters MUST NOT allocate, block, or otherwise perturb the audio
  path.
- **FR-034**: `blocksProcessed` MUST be maintained as a denominator so counters can be
  expressed as rates, and `worstBlockMicros` MUST be maintained so the CPU budget is directly
  observable rather than inferred from dropout symptoms.
- **FR-035**: Ring-buffer **capacity, water marks, and startup fill** MUST be derived from
  hardware-in-the-loop measurement and pinned in the implementation plan with their measured
  justification (**D23**). They MUST NOT be invented ahead of measurement. Ring-buffer
  *semantics* (FR-031, FR-032) are fixed here; only these three quantities are measurement-derived.

#### Effect integration

- **FR-036**: The effect MUST be prepared with a maximum block size of **49 frames**, at
  48 kHz, 2 channels (**D15**). 48 frames is the nominal rate, not a guarantee.
- **FR-036a**: The DSP MUST process **fixed 48-frame blocks** drawn from the ring
  (Clarifications 2026-08-23). The 49-frame prepare of FR-036 is headroom that keeps the
  effect's allocation sized for the largest payload the transport can deliver; it is not the
  working block size.
- **FR-037**: Audio MUST be presented to `process()` as non-interleaved `float*` per channel
  in an `acfx::AudioBlock`, processed in place, with no heap allocation — matching the adapter
  contract `adapters/daisy/daisy-main.cpp` exemplifies.
- **FR-038**: The support library MUST convert interleaved 16-bit USB payloads to
  non-interleaved float channel buffers on the way in, and reverse that on the way out,
  without allocating, for any payload size permitted by FR-028.
- **FR-038a**: The conversion MUST scale by **32768**, round to nearest on the way out, and
  **clamp** the result to [-32768, 32767] (Clarifications 2026-08-23). The clamp is
  load-bearing rather than defensive: an effect that overshoots 1.0 would otherwise wrap to
  the opposite rail and emit loud broadband noise, which is precisely the class of silent,
  uncounted degradation FR-032 exists to prevent.

#### Parameter control

- **FR-039**: Parameter changes MUST arrive through a `ParameterSource` abstraction, with
  **USB MIDI** as the first implementation (**D2**).
- **FR-040**: The `ParameterSource` contract MUST be shaped to accommodate physical
  peripherals — pots, encoders, buttons — as a known-implied requirement, so a sampled-state
  source and an event-driven source both plug into the same seam (**D3**).
- **FR-041**: The parameter seam MUST be a **per-`ParamId` shadow block with dirty flags**,
  bounded by construction at the effect's parameter count (**D25**).
- **FR-042**: Once per audio block, the adapter MUST walk the dirty flags and call
  `setParameter` exactly once for each dirty parameter, then clear the flags (**D25**).
- **FR-043**: The parameter seam MUST NOT be a bounded FIFO of change events (**D25**). Any
  drop policy on a shared queue is lossy in a way that matters: drop-newest can strand a
  parameter at an intermediate value after a knob sweep, and drop-oldest lets one fast-moving
  control evict another control's single pending update. Per-`ParamId` slots make both failure
  modes structurally impossible.
- **FR-044**: The adapter MUST document that this seam is correct for state-valued parameters
  and wrong for event-valued controls (a momentary trigger, tap tempo); acfx's parameter model
  currently has only normalized continuous values, and an event-valued control would need its
  own mechanism regardless.
- **FR-045**: MIDI CC messages MUST be mapped to `ParamId` values by a mapping that lives in
  the host-testable support library.

#### Execution model

- **FR-046**: Class servicing and DSP MUST both run in the single main-loop execution context;
  the interrupt handler MUST only enqueue (**D26**). Verified against the pinned stack version:
  transfer callbacks dispatch from the task loop while the device-controller event handler only
  queues.
- **FR-047**: The single-context assumption MUST be documented as load-bearing for FR-041's
  lock-free shadow block, with the explicit trigger for revisiting it named: sampling
  peripherals from a timer interrupt would break it (**D26**).

#### Verification

- **FR-048**: CI MUST cross-compile and link every declared Nucleo firmware target (**D18**,
  layer 1).
- **FR-049**: The host doctest suite MUST cover `acfx_nucleo_support` — the conversion and
  de-interleaving, the ring buffer's defined behaviours and counters, and the parameter shadow
  block (**D18**, layer 2).
- **FR-050**: A hardware-in-the-loop harness MUST exist that streams audio through an attached
  board and asserts against `AudioTransportStats` directly — read over the CDC serial function
  per FR-033a — rather than inferring glitches from signal correlation (**D18**, layer 3). It
  requires a physical board and therefore MUST NOT be wired into the normal CI job.

### Key Entities

- **Nucleo firmware image**: One built binary per effect, produced by `acfx_add_effect_nucleo`;
  carries the effect type injected at configure time.
- **Hardware shim (`nucleo-main.cpp`)**: The silicon-touching layer — clock, GPIO, USB stack
  initialization, interrupt handler, main service loop. Not host-testable, and therefore kept
  minimal.
- **Support library (`acfx_nucleo_support`)**: The host-compilable layer — format conversion,
  de-interleaving, ring buffer, parameter shadow block, MIDI-CC mapping. The unit of host-side
  test coverage.
- **Audio ring buffer**: Statically sized, lock-free, no-allocation buffer between the USB
  packet cadence and the DSP's independent fixed 48-frame block cadence. Semantics fixed by
  FR-031/FR-032; capacity, water marks, and startup fill measurement-derived per FR-035.
- **`AudioTransportStats`**: The seven-field observability record (four error counters, the
  capture-only starvation counter, the block denominator, and the worst-case block time)
  against which the HIL harness asserts.
- **`ParameterSource`**: The abstraction through which parameter changes reach the effect;
  USB MIDI is the first implementation, physical peripherals shape the contract.
- **Parameter shadow block**: One slot plus dirty flag per `ParamId`, bounded at the effect's
  parameter count, walked once per audio block.
- **USB descriptor set**: The locally authored composite descriptor — UAC2 audio function,
  MIDI function, and CDC serial function, IAD-grouped, advertising a single 48 kHz / 16-bit /
  stereo format per direction.
- **Telemetry channel**: The CDC serial function over which `AudioTransportStats` is read and
  diagnostics are emitted; the HIL harness's connection to the device's own accounting.
- **MIDI CC → `ParamId` mapping**: The correspondence between incoming control-change numbers
  and effect parameters; convention itself is an open question.

## Success Criteria *(mandatory)*

### Measurable Outcomes

- **SC-001**: A user connects the board with two cables and, with **zero** driver
  installations, sees a stereo-in / stereo-out 48 kHz audio device, a MIDI port, and a serial
  port; a class-compliant audio client opens it duplex as 2-in / 2-out.
- **SC-002**: A known signal streamed through a firmware whose effect has a deterministic
  transfer characteristic returns transformed as expected, confirming audio reaches and leaves
  `process()` intact.
- **SC-003**: Every packet size the transport can deliver — 0 through 49 frames, including
  short and zero-length packets — is handled with correct frame counts and no out-of-bounds
  access, demonstrated by host tests over the full range.
- **SC-004**: All seven `AudioTransportStats` fields are readable from a host during operation
  without a debug probe, and error counts can be expressed as a rate against `blocksProcessed`
  rather than as bare totals.
- **SC-005**: A host opening the capture stream alone receives silence with `inputStarved`
  incrementing — never a hang, never stale audio — and duplex operation resumes when the
  playback stream is later opened.
- **SC-006**: Parameter changes are lossless in the sense that matters: after any burst of
  changes within one block period, every changed parameter reaches its **last** value at the
  next block boundary, and no parameter's pending change is evicted by another's.
- **SC-007**: A clock that cannot be brought up to the required configuration is distinguishable
  from an unpowered board by eye alone, with no debug probe attached, and there is **no**
  configuration in which the device enumerates on a degraded clock.
- **SC-008**: CI cross-compiles and links every declared Nucleo firmware target, and a
  toolchain missing its C++ standard library fails configuration with a message naming the
  cause.
- **SC-009**: The host doctest suite exercises the conversion path — including that
  beyond-full-scale output clips rather than wrapping — every defined ring-buffer behaviour and
  its counter, and the parameter shadow block, closing the untested-glue gap the Daisy and
  Teensy adapters currently carry.
- **SC-010**: No heap allocation and no lock occurs in the audio path, demonstrated by the
  existing no-allocation test discipline.
- **SC-011**: `worstBlockMicros` is recorded for every shipped Nucleo firmware, making each
  effect's CPU headroom against the frame period directly observable rather than inferred.
- **SC-012**: Every shipped source file remains within the ~300–500 line budget, and
  `nucleo-main.cpp` contains no logic that could have lived in the host-testable support
  library.

## Assumptions

- `acfx_core` remains header-only and C++17, and is unchanged by this feature. Any gap found in
  core is surfaced rather than worked around in the adapter.
- The adapter contract exemplified by `adapters/daisy/daisy-main.cpp` — `prepare` /
  non-interleaved `AudioBlock` / in-place `process` / normalized `setParameter` — is stable and
  is the contract this adapter implements.
- The ST-Link cable is attached at runtime and its MCU emits the 8 MHz MCO on OSC_IN. Without
  it there is no accurate clock and therefore no USB.
- A USB-C breakout is wired to PA11/PA12 on the CN10 morpho header, with VBUS unwired.
- The board is powered from the ST-Link connection.
- Driverless UAC2 enumeration is hardware-verified on macOS. Other hosts are expected to work
  by virtue of class compliance but are not yet verified. The same expectation applies to the
  added CDC serial function, which is driverless on macOS, Linux, and Windows 10+.
- Adding the CDC function keeps the device within full-speed bandwidth and interface-count
  limits alongside the two isochronous streams and the MIDI function. If measurement shows
  otherwise, that is a finding to surface, not to work around.
- The SAI, I2S, ADC, and DAC peripherals exist on the MCU and are deliberately unused by this
  target. This is a choice, not a limitation of the silicon.
- STM32Cube HAL and the CubeMX-generated clock configuration are not used; register-level setup
  is roughly 150 lines and the HAL would be more code to carry, not less.
- The spike repository is not imported. Its value is its hardware-verified findings, already
  folded into the requirements above.
- The spike's measured ~0.2% dropout figure was taken under a naive single buffer and is not a
  prediction for the tuned design.
- Existing repository conventions carry over unchanged: CPM pinning with a lock entry,
  descriptive names, no git hooks, small modules.

## Open Questions

Third-party review resolved two of the design record's original eleven questions into
decisions — the isochronous synchronization model became **D20–D22**, and the `ParameterSource`
contract became **D25**. The following remain open and are carried forward deliberately, not
resolved by omission.

1. **Ring-buffer capacity, water marks, and startup fill.** Deliberately unpinned (**D23**,
   FR-035) — these come from HIL measurement and land in the plan with measured justification.
   Inventing them before the harness exists would be false precision.
2. **The acceptable glitch bar.** Now directly measurable via `AudioTransportStats` rather than
   inferred from signal correlation, so the question narrows to: what counter rate constitutes
   a failing build, and which verification layer enforces it?
3. **Does the single-context assumption (D26) survive physical peripherals?** The single-context
   assumption is what lets the shadow block skip lock-free discipline. Sampling ADCs from a timer
   interrupt would break it and require revisiting FR-041's memory ordering. That is the explicit
   trigger to reopen this — not a reason to pre-pay for it now.
4. **Reusable transport seam.** When a second STM32 board appears, does the UAC2 transport
   extract cleanly out of `adapters/nucleo/`? The three-layer variant was rejected *for now* on
   the grounds that the correct board/transport seam is guesswork until a second board reveals
   it. Revisit at board two.
5. **Additional audio formats.** 24-bit, 44.1 kHz, and 96 kHz all fit inside full-speed
   bandwidth for stereo. The operator scoped the advertised matrix to 48/16 (**D4**); 44.1 in
   particular forces variable packet sizes and is more work than it appears.
6. **Where the HIL harness lives and how it is invoked.** It needs a physical board, so it
   cannot be a normal CI job. In-repo with a manual target? A dedicated runner? The spike's
   `tools/loopback_test.py` is a starting point. Its *readback channel* is no longer open —
   CDC serial, per FR-033a — but its home and invocation are.
7. **MIDI CC → `ParamId` mapping convention.** Fixed CC numbers per parameter index? A learn
   mode? Which channel? Should it match how the workbench already consumes MIDI CC, so one
   mapping serves both?
8. **Which effects get Nucleo firmwares, and what the CPU budget at 168 MHz allows.**
   `worstBlockMicros` (FR-034) makes this measurable rather than speculative, and it is a live
   question for the convolution and physical-modeling phases against 512 KB flash / 128 KB SRAM.
9. **Two cables is awkward.** The ST-Link cable is mandatory only because it supplies the clock.
   Is a single-cable arrangement — populating X3, or an external oscillator — wanted later?
10. **Should `daisy` and `teensy` get roadmap nodes retrofitted?** They are currently ungoverned,
    having arrived under Milestone 1. Considered and deferred by the operator when the
    `hardware-targets` parent node was created.
11. **Denormal handling.** Not addressed by any existing adapter. `TASK-1 svf-no-denormal-flush`
    is already open in the backlog and applies to an MCU target with an FPU just as much as to
    the desktop ones.
