# Phase 0 Research — NUCLEO-F446RE adapter with USB audio I/O

**Feature**: `specs/nucleo-f446-adapter` · **Date**: 2026-08-23

**Inputs**: the operator-approved design record
`docs/superpowers/specs/2026-08-22-nucleo-f446-adapter-design.md` (decisions **D1–D26**), the
2026-08-23 clarification session (`spec.md` § Clarifications), and the in-repo Daisy adapter,
which is the adapter-contract exemplar.

Most of this feature's research was already done and recorded: the design record carries 26
settled decisions and a provenance section of **hardware-verified** spike findings. This
document does not restate those. It resolves what the plan additionally needs, and — equally
important — states plainly which assertions are *verified* versus which are *expected and must
be confirmed during implementation*. An unverified claim recorded as fact is how a silent
failure gets built in.

---

## R1 — TinyUSB 0.21.0 audio data path

**Decision**: Service the audio data path by **polling** `tud_audio_read()` /
`tud_audio_write()` from the main loop. Do not write against `rx_done` / `tx_done`.

**Rationale**: Hardware-verified in the spike and recorded as a design-record provenance
finding: TinyUSB 0.21.0 **removed** the `rx_done`/`tx_done` data-path callbacks. Code written
against them links *silently* — the symbols simply vanish and the audio path is dead with no
diagnostic. This is the single most expensive trap in this feature, because it produces a
board that enumerates perfectly and passes no audio, with nothing to grep for.

**Must be confirmed during implementation**: the exact names and signatures of the
alt-setting/close callbacks (`tud_audio_set_itf_cb`, `tud_audio_set_itf_close_EP_cb` or their
0.21.0 equivalents) and whether the stereo case needs the `_n_` (multi-instance) variants.
These are read off the pinned 0.21.0 tree, not from memory — the version is pinned precisely so
this is a lookup rather than a guess.

**Alternatives rejected**: tracking TinyUSB `master` (**D9** — its `uac2_headset` example
references `TUD_AUDIO_HEADSET_STEREO_DESC_LEN`, defined only in a *different* example's header,
so it is broken on master and on 0.21.0 alike).

---

## R2 — Composite descriptor: UAC2 + MIDI + CDC

**Decision**: Author the descriptor locally from the `TUD_AUDIO20_DESC_*` primitives (**D10**),
adapting TinyUSB's `examples/device/cdc_uac2/src/usb_descriptors.h`, and grow it to three
IAD-grouped functions: UAC2 audio, USB MIDI, and CDC serial (FR-018, FR-018a).

**Rationale**: No shipped TinyUSB template provides stereo-in **with** stereo-out; the headset
templates are mono-mic/stereo-out. Writing the descriptor from the primitives is therefore
required regardless. The chosen starting template already carries CDC + UAC2, so the CDC
telemetry function added by the 2026-08-23 clarification is close to free — MIDI is the
function actually being added relative to the template.

**Interface budget**: full speed allows ample interfaces; the constraint that actually binds is
**endpoints**. OTG_FS provides a limited number of bidirectional endpoints, and this device
wants: audio OUT (iso), audio IN (iso), MIDI IN + OUT (bulk, one endpoint pair), CDC notify
(interrupt) + CDC data IN/OUT (bulk). **Must be confirmed during implementation** against the
F446 OTG_FS endpoint count. If the budget does not close, that is a finding to surface — per
the spec's Assumptions, not something to quietly work around by dropping a function.

**Alternatives rejected**: a vendor-specific control request for telemetry (risks a Windows
driver prompt, cutting against the driverless proposition); MIDI SysEx telemetry (mixes
telemetry into the control path and its packet framing is fiddly); debugger-only readback (puts
a probe in the loop for every HIL run).

---

## R3 — Clock bring-up

**Decision**: Register-level HSE-bypass configuration against the ST-Link MCU's 8 MHz MCO, PLL
M=4 N=168 P=2 Q=7 → 168 MHz SYSCLK and exactly 48 MHz on PLLQ (**D6**). The adapter defines
`SystemCoreClock` itself (**D14**).

**Rationale**: Hardware-verified in the spike — `HSERDY=1`, `PLLRDY=1`, `RCC_CFGR.SWS=PLL`,
`RCC_PLLCFGR=0x07402a04`, USB measured at exactly 48.0000 MHz and cross-checked against
wall-clock time via SysTick. 180 MHz is the part maximum, but 360/48 is not integral and would
force USB onto PLLSAI. `SystemCoreClock` matters because TinyUSB derives the USB PHY turnaround
time from it: a wrong value degrades timing *silently* rather than failing.

**Failure handling**: PLL-lock failure is fatal (**D7**, FR-015). Because USB cannot come up
without the PLL, the fault cannot be reported over CDC or MIDI — the on-board LD2 (PA5) blink
pattern is the only channel (FR-015a). Sequencing follows from this: the LED GPIO must be
initialized *before* the clock is validated, which means it runs on the reset-default HSI. That
is acceptable precisely because the LED is not timing-critical; the blink cadence will be
approximate on HSI and that is fine.

**Alternatives rejected**: STM32Cube HAL / CubeMX (**D6** rationale in the record — register
setup is ~150 lines; the HAL is more code to carry, not less, and drags vendor scaffolding into
a repo whose principle is thin adapters).

---

## R4 — Vector table

**Decision**: Generate the interrupt vector table from the CMSIS `IRQn_Type` enum (**D13**).

**Rationale**: `OTG_FS_IRQn` is 67. A core-exceptions-only table sends the NVIC past the end of
the array on the *first* USB interrupt — a hard fault whose cause is several layers away from
its symptom. Deriving the table's length from the enum makes the correct size structural rather
than a number someone has to maintain.

---

## R5 — Ring buffer: semantics fixed, capacity deliberately not

**Decision**: Implement a statically sized, single-producer/single-consumer ring with the
semantics fixed by FR-031/FR-032, sitting between the USB packet cadence and an **independent
fixed 48-frame DSP block cadence** (FR-030a). Capacity, water marks, and startup fill are
**left as template/compile-time parameters with a documented measurement procedure**, not
pinned to invented numbers (**D23**, FR-035).

**Rationale**: This is the requirement that most invites a fabricated number, so it deserves an
explicit method instead of one:

1. Build with an instrumented capacity generous enough not to clip the distribution (the point
   is to *observe* the excursion, not to survive it).
2. Stream from a host for a sustained run and record, over `blocksProcessed`, the ring
   occupancy's minimum, maximum, and distribution, plus every counter in `AudioTransportStats`.
3. Derive the startup fill from the observed *lower* excursion and the capacity from the
   observed *upper* excursion, each with headroom justified by the measured spread.
4. Re-run and confirm the counters stay at the operator's chosen bar (which is itself open
   question 2).

The design record is explicit that the spike's ~0.2% dropout figure was measured under a naive
single buffer and does **not** predict the tuned design. Carrying it forward as a target would
be false precision.

**Why decoupled cadences at all**: with the DSP drawing fixed 48-frame blocks and USB delivering
0–49 frames per packet, the ring absorbs the difference — which is exactly what gives startup
fill and water marks meaning, and what gives the four under/overrun counters a distinct producer
and consumer to sit between.

---

## R6 — Block timing source for `worstBlockMicros`

**Decision**: Use the Cortex-M4 **DWT cycle counter** (`DWT->CYCCNT`), converting cycles to
microseconds against the known 168 MHz core clock.

**Rationale**: It is a free-running 32-bit counter incremented once per core cycle — the
finest-grained, lowest-overhead timing source on the part, and reading it costs a single load.
SysTick is the alternative but it is a 24-bit down-counter typically already committed to a
system tick, and reusing it for measurement means reasoning about wrap against the tick period.
At 168 MHz, `CYCCNT` wraps every ~25.6 s, which is far longer than any single block and is
handled correctly by unsigned subtraction regardless.

**Must be confirmed during implementation**: DWT requires enabling the trace subsystem
(`CoreDebug->DEMCR |= TRCENA`, then `DWT->CTRL |= CYCCNTENA`). On some parts DWT is unavailable
when no debugger has ever attached. If `CYCCNT` reads back as stuck at zero after enabling,
that must surface as a loud failure rather than silently reporting `worstBlockMicros = 0` — a
zero that means "not measured" is indistinguishable from a zero that means "instantaneous",
which is precisely the observability failure FR-034 exists to prevent.

---

## R7 — Telemetry wire format over CDC

**Decision**: Emit `AudioTransportStats` as **line-oriented `key=value` text**, one snapshot per
line, newline-terminated, written from the main loop.

**Rationale**: It is trivially parseable by the spike's Python harness (`tools/loopback_test.py`
is the named starting point), human-readable when a developer just opens the serial port, and
requires no framing logic on either end. JSON buys structure this record does not need — seven
scalar counters — at the cost of a serializer on an MCU.

**Non-perturbation (FR-033a)**: the telemetry write must not allocate, must not block, and must
not stall the audio path when nothing has the serial port open. TinyUSB's CDC write is
non-blocking and drops when its FIFO is full, which is the correct behaviour here: an unread
telemetry channel is not an error condition. This is a *third* place where a bounded-substitution
policy appears, and it is deliberately consistent with FR-031/FR-032 — except that dropping
telemetry needs no counter, because the counters are the payload.

---

## R8 — Where `acfx_nucleo_support` lives in the build

**Decision**: Define `acfx_nucleo_support` in its own CMake target that is added **whenever
either** the Nucleo adapter or the host test suite is being built — not gated behind
`ACFX_BUILD_NUCLEO` alone. Its tests join the existing `acfx_core_tests` binary.

**Rationale**: This is a real structural constraint discovered by reading the build, not a
stylistic preference. The host suite builds under the `test` preset, which sets **no toolchain**
and enables only `ACFX_BUILD_TESTS`. The adapter builds under a new `nucleo` preset with the ARM
toolchain. If the support library were declared only inside `if(ACFX_BUILD_NUCLEO)`, the host
tests could never see it — and the entire point of **D1**'s two-layer split (FR-002) is that
this code is host-testable. Getting this wrong would silently reproduce the untested-glue blind
spot the decomposition exists to close.

The support library must therefore be **strictly platform-independent**: no TinyUSB headers, no
CMSIS, no board headers. Anything that needs them belongs in `nucleo-main.cpp` (FR-003).

**Precedent followed**: `acfx_core` and `acfx_analysis` are both declared unconditionally at the
top level and consumed by whichever targets are enabled.

---

## R9 — MIDI CC → `ParamId` mapping mechanism

**Decision**: Implement a **table-driven** mapping in the support library (FR-045): a
compile-time table from CC number to parameter index, resolved against the effect's
`parameters().size()` so an effect with fewer parameters leaves the surplus CCs unbound.

**The concrete convention remains OPEN (open question 7)** and is the operator's call. The
mechanism above is chosen so that settling the convention later is a table edit, not a rewrite.

**Proposed default, pending operator confirmation**: bind CC numbers to parameter indices
generically and omni-channel, mirroring how `daisy-main.cpp` already binds its three ADC knobs
to `ParamId{0,1,2}` regardless of which effect is compiled in — the repo already has a
"bind the first N controls to the first N parameters" precedent, and reusing it means one mental
model across adapters. This is offered as a recommendation, **not** applied as a decision: open
question 7 also asks whether the convention should match the workbench's existing MIDI CC
consumption so one mapping serves both, which is a question about another subsystem this
research did not examine.

**Implementation note**: an unmapped CC must be ignored without disturbing any parameter slot
(spec Edge Cases), and an out-of-range parameter index must never reach `setParameter` — the
Daisy adapter's `boundKnobs()` shows the shape.

---

## R10 — int16 ↔ float conversion

**Decision**: Scale by **32768**; on the way out, round to nearest and **clamp** to
[-32768, 32767] (FR-038a, Clarifications 2026-08-23).

**Rationale**: Dividing by 32768 on the way in is an exact power-of-two operation that reverses
cleanly, so the round-trip property US2 asserts holds for every representable value. The clamp
is load-bearing rather than defensive: an effect that overshoots 1.0 would otherwise wrap to the
opposite rail and emit loud broadband noise — the exact class of silent, uncounted degradation
FR-032 exists to prevent.

**Context from the spike**: the measured loopback gain of 0.999916 was traced to **CoreAudio's**
int16↔float32 conversion, not to anything in the firmware. It is therefore not a target to
reproduce and not a defect to chase; it is a property of the host-side conversion and belongs in
the HIL harness's expectations, not in the firmware's conversion.

---

## R11 — LED fault signalling

**Decision**: Drive LD2 (PA5) directly via GPIO with a busy-wait blink pattern, initialized
before clock validation (see R3).

**Rationale**: The board has exactly one user LED and no display. The pattern must be
*distinguishable from both a dark board and a normally-running board*, which is what makes
SC-007 testable by eye. A single distinct pattern suffices for the one fatal condition specified
(FR-015a); if further fatal conditions appear later, distinct patterns per condition is the
natural extension and costs nothing to leave room for.

---

## R12 — `cpm-package-lock.cmake` is empty in practice

**Finding, surfaced rather than papered over**: FR-010 requires CPM pinning "with corresponding
`cpm-package-lock.cmake` entries". Reading the repo, `cpm-package-lock.cmake` contains only its
two-line header comment — **no package entries at all**. The real pinning mechanism in this repo
is the explicit `GIT_TAG` on each `CPMAddPackage` call in `cmake/dependencies.cmake`, which
carries full SHAs for every dependency plus a comment block recording which pins were
fetch-verified.

**Decision**: Pin the new dependencies the way the repo *actually* pins them — explicit
`GIT_TAG` in `cmake/dependencies.cmake`, gated behind `ACFX_BUILD_NUCLEO`, with the pin recorded
in that file's comment block and marked as fetch-verified once it has been. Do **not** invent a
lock-file format that nothing else in the repo uses.

**Surfaced for the operator**: the gap between the stated convention and the actual mechanism is
pre-existing and applies to every dependency, not just this feature's. It is not fixed here.
Whether `cpm-package-lock.cmake` should be populated (CPM can generate it via
`CPMUsePackageLock`) or removed as vestigial is a repo-wide call, and a backlog item is the right
home for it.

**New pins required**: TinyUSB at **0.21.0** (**D8**), `cmsis_device_f4`, and CMSIS core (CMSIS
5/6). All three are `DOWNLOAD_ONLY` — the adapter compiles the sources it needs directly, as the
Daisy and Teensy adapters already do with their vendor trees. The exact upstream refs must be
captured from the real repositories at implementation time and verified by an actual fetch, per
the file's own stated discipline: **never a fabricated version number**.

---

## Summary of what remains unverified

Recorded explicitly so implementation treats these as lookups, not assumptions:

| # | Item | Why it matters |
|---|---|---|
| R1 | TinyUSB 0.21.0 alt-setting callback names/signatures | Wrong names link silently; a dead audio path with no diagnostic |
| R2 | OTG_FS endpoint budget across audio + MIDI + CDC | If it does not close, a function must be reconsidered — an operator call, not an agent cut |
| R6 | DWT `CYCCNT` availability without a prior debugger attach | A stuck-at-zero counter would silently report `worstBlockMicros = 0` |
| R12 | Upstream refs for TinyUSB 0.21.0, `cmsis_device_f4`, CMSIS core | Pins must be real and fetch-verified, never fabricated |

## Open questions deliberately NOT resolved here

All 11 of the spec's open questions remain open. Three of them were within arm's reach of this
research and are called out so it is clear they were left alone on purpose:

- **Ring capacity, water marks, startup fill (OQ1)** — R5 supplies the *measurement procedure*
  and leaves the numbers to HIL, per **D23**.
- **The acceptable glitch bar (OQ2)** — R5 step 4 defers to it; the counters make it measurable,
  but what rate constitutes a failing build is the operator's.
- **MIDI CC mapping convention (OQ7)** — R9 supplies the *mechanism* and a recommendation, and
  explicitly does not apply it.
