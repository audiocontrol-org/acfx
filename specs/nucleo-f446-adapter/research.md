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

> **SUPERSEDED IN PART BY R13 (T028).** R13 records the API read directly off the pinned
> TinyUSB tree `d34550b3aaa115e7ec09bea0c9e676531bf95dfb`. Three claims below are corrected
> there: (a) `rx_done`/`tx_done` were *renamed* to `tud_audio_rx_done_isr` /
> `tud_audio_tx_done_isr`, not simply deleted — the deleted family is the older
> `*_pre_read_cb` / `*_post_load_cb` set; (b) the close callback is
> `tud_audio_set_itf_close_ep_cb`, lower-case `ep`; (c) the `_n_` variants are **not** needed
> for the duplex case. R13's file:line citations win over anything written here.

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

> **DISCHARGED BY R14 (T029).** The budget **closes** on both axes: 4 IN / 3 OUT non-control
> endpoints against 5 / 5 available, and 225 of 320 FIFO words. No function is dropped and no
> finding is escalated. R14 carries the endpoint table and the FIFO arithmetic T027 builds
> against.

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
requires no framing logic on either end. JSON buys structure this record does not need — eight
scalar counters — at the cost of a serializer on an MCU.

**Non-perturbation (FR-033c/FR-033d)**: statistics *updating* happens on the audio path;
*snapshotting, serializing, and writing* happen in a separate main-loop diagnostic service,
outside the audio path and outside `worstBlockMicros`. That service's write must still be
non-blocking and allocation-free — **D26** gives the firmware a single execution context, so
moving the work out of the audio path relocates it rather than giving it somewhere else to
stall. TinyUSB's CDC write is
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

---

## R13 — TinyUSB 0.21.0 audio/MIDI/CDC API, READ OFF THE PINNED TREE (T028)

**Date**: 2026-08-23 · **Task**: T028 [US4] · **Status**: verified by reading source, not recalled

**Provenance — this is a pinned-source record, not a memory dump.** Every name, signature and
line number below was read from the CPM cache of the exact pin in `cmake/dependencies.cmake`:

- Tree: `external/.cpm-cache/tinyusb/d34550b3aaa115e7ec09bea0c9e676531bf95dfb`
- Commit: **`d34550b3aaa115e7ec09bea0c9e676531bf95dfb`**, TinyUSB **0.21.0**
  (`version.yml` records `"0-latest": "0.21.0"`)
- Paths below are relative to that tree root. Nothing here was taken from training data,
  upstream `master`, or any other TinyUSB version. If a name below disagrees with anything
  written earlier in this document, **the line citation wins**.

### R13.0 — CORRECTION TO R1: the `rx_done`/`tx_done` story is more dangerous than "removed"

R1 records that 0.21.0 "removed the `rx_done`/`tx_done` data-path callbacks". Reading the pinned
tree, that is **half right, and the wrong half is the trap**:

1. **Genuinely gone.** The pre-0.19 four-callback family —
   `tud_audio_rx_done_pre_read_cb`, `tud_audio_rx_done_post_read_cb`,
   `tud_audio_tx_done_pre_load_cb`, `tud_audio_tx_done_post_load_cb` — does **not exist
   anywhere in the pinned tree**. A recursive grep of the whole checkout for those four names
   returns zero hits. So does a grep for their enabling macros `CFG_TUD_AUDIO_ENABLE_ENCODING`
   and `CFG_TUD_AUDIO_ENABLE_DECODING`. Code written against them cannot work.
2. **But a `rx_done`/`tx_done` pair DOES still exist under new names**, with an `_isr` suffix
   and a different signature: `tud_audio_rx_done_isr` (`src/class/audio/audio_device.h:251`)
   and `tud_audio_tx_done_isr` (`src/class/audio/audio_device.h:243`). A reader who greps the
   0.21.0 tree for `rx_done`, sees hits, and concludes "R1 was wrong, the callbacks are still
   here" will write against a *different* contract than the one they remember.
3. **The silent-link mechanism is broader than R1 states, and this is the real finding.**
   *Every* audio application callback in 0.21.0 is declared `TU_ATTR_WEAK` with a permissive
   default body in `src/class/audio/audio_device.c:277-336`. `tud_audio_set_itf_cb`
   (`audio_device.c:325`) and `tud_audio_set_itf_close_ep_cb` (`audio_device.c:332`) both have
   weak stubs that ignore their arguments and `return true;`. So a **typo or a stale name in
   ANY audio callback** — not just the data-path pair — links cleanly against the weak stub,
   enumerates fine, and leaves the adapter deaf with no diagnostic. There is no `-Wmissing-*`
   that catches it. The only defence is spelling the names off this record.

**Consequence for later tasks:** do not treat "it compiled and linked" as any evidence that a
callback is wired. The acceptance evidence for an alt-setting callback must be an observed
*side effect* (LED, counter, log line) when the host opens the stream.

### R13.1 — Alt-setting open / close callbacks (CONFIRMED, exact names)

Both exist, both take **only** `rhport` and the raw control request — there is no direction
argument, no interface argument, no alt argument in the signature.

| Callback | Declared | Weak stub | Signature |
|---|---|---|---|
| open / set-alt | `audio_device.h:359` | `audio_device.c:325` | `bool tud_audio_set_itf_cb(uint8_t rhport, tusb_control_request_t const *p_request)` |
| close / clear-alt | `audio_device.h:362` | `audio_device.c:332` | `bool tud_audio_set_itf_close_ep_cb(uint8_t rhport, tusb_control_request_t const *p_request)` |

**Spelling note that matters.** R1 guessed `tud_audio_set_itf_close_EP_cb` (capital `EP`). In
0.21.0 the canonical name is **lower-case `ep`**; the capitalised form survives only as a
backward-compatibility `#define` at `audio_device.h:365`:

    #define tud_audio_set_itf_close_EP_cb   tud_audio_set_itf_close_ep_cb

Either spelling therefore links, but new code should use the lower-case canonical name.

**Where the driver invokes them** (this is what fixes the semantics):

- `tud_audio_set_itf_close_ep_cb` is called **once per direction being torn down**, from inside
  the per-direction branches of `audiod_set_interface()`: for the IN/microphone endpoint at
  `audio_device.c:1102` (inside `if (audio->ep_in_as_intf_num == itf)`, line 1091) and for the
  OUT/speaker endpoint at `audio_device.c:1126` (inside `if (audio->ep_out_as_intf_num == itf)`,
  line 1115). In both cases the driver has already closed the endpoint and cleared the FIFO
  (`tu_fifo_clear`, lines 1099 / 1123) before the callback runs.
- `tud_audio_set_itf_cb` is called **exactly once per SET_INTERFACE**, after all endpoints for
  the new alt setting have been opened and the first transfer scheduled —
  `audio_device.c:1251`, commented in-source as "Invoke one callback for a final set interface".
- Returning `false` from either callback aborts the SET_INTERFACE via `TU_VERIFY` and the
  request is stalled. Both weak defaults return `true`.

### R13.2 — How the two directions are distinguished (answers the stereo-in AND stereo-out question)

The callbacks carry **no** direction parameter. The device distinguishes speaker/OUT from
microphone/IN by decoding the control request itself, exactly as the driver does internally at
`audio_device.c:1078-1079`:

    uint8_t const itf = tu_u16_low(p_request->wIndex);   // audio streaming interface number
    uint8_t const alt = tu_u16_low(p_request->wValue);   // alternate setting; 0 == stream closed

The application then compares `itf` against **its own** streaming-interface numbers from its
descriptor. The shipped pattern is in `examples/device/uac2_headset/src/main.c:526-556`, which
tests `ITF_NUM_AUDIO_STREAMING_SPK == itf` in both callbacks; those constants come from that
example's own interface enum at `examples/device/uac2_headset/src/usb_descriptors.h:29-35`
(`ITF_NUM_AUDIO_CONTROL = 0, ITF_NUM_AUDIO_STREAMING_SPK, ITF_NUM_AUDIO_STREAMING_MIC`).

**Design consequence for this adapter (both directions, one audio function).** A single audio
function (`CFG_TUD_AUDIO = 1`) carries both directions: `audiod_function_t` holds `ep_in`,
`ep_in_as_intf_num`, `ep_in_alt` *and* `ep_out`, `ep_out_as_intf_num`, `ep_out_alt`
independently, and the open path fills them in separate `CFG_TUD_AUDIO_ENABLE_EP_IN` /
`CFG_TUD_AUDIO_ENABLE_EP_OUT` blocks (`audio_device.c:1185-1226`). So:

- The `_n_` multi-instance variants are **not** needed for stereo-in-with-stereo-out — R1's
  open question is answered **no**. `_n_` indexes *audio functions*, not directions.
  `func_id` 0 is correct for this design and the non-`_n_` inlines are the right entry points.
- The host opens and closes the two streams **independently**, so the adapter must track two
  separate "streaming?" flags keyed on its own SPK and MIC interface numbers. Treating
  `set_itf_close_ep_cb` as a single global "audio stopped" event is a bug: it fires for one
  direction while the other may still be live.
- There is **no public accessor** for the driver's internal `ep_in_alt` / `ep_out_alt`. The
  only public state query is `tud_audio_n_mounted(uint8_t func_id)` / `tud_audio_mounted(void)`
  (`audio_device.h:177`, `:202`; implementation `audio_device.c:421`), which reports the audio
  *function* mounted, not per-direction streaming. Per-direction state is the application's
  to keep.

### R13.3 — Polled read/write entry points (CONFIRMED, plan's assumption holds)

`tud_audio_read()` and `tud_audio_write()` **do exist under exactly those names**. They are
`TU_ATTR_ALWAYS_INLINE static inline` wrappers over the `_n_` forms with `func_id` 0.

| Entry point | Declared | Defined | Signature / semantics |
|---|---|---|---|
| `tud_audio_read` | `audio_device.h:208` | `audio_device.h:403-405` | `uint16_t tud_audio_read(void *buffer, uint16_t bufsize)` → `tud_audio_n_read(0, ...)` |
| `tud_audio_n_read` | `audio_device.h:182` | `audio_device.c:448-451` | `uint16_t tud_audio_n_read(uint8_t func_id, void *buffer, uint16_t bufsize)` → `tu_fifo_read_n(&ep_out_ff, ...)`; returns **bytes** read |
| `tud_audio_available` | `audio_device.h:206` | `audio_device.h:399-401` | `uint16_t tud_audio_available(void)` → bytes currently in the OUT FIFO |
| `tud_audio_n_available` | `audio_device.h:181` | `audio_device.c:443-446` | `uint16_t tud_audio_n_available(uint8_t func_id)` |
| `tud_audio_write` | `audio_device.h:213` | `audio_device.h:419-421` | `uint16_t tud_audio_write(const void *data, uint16_t len)` → `tud_audio_n_write(0, ...)` |
| `tud_audio_n_write` | `audio_device.h:188` | `audio_device.c:500-503` | `uint16_t tud_audio_n_write(uint8_t func_id, const void *data, uint16_t len)` → `tu_fifo_write_n(&ep_in_ff, ...)`; returns **bytes** accepted |
| `tud_audio_clear_ep_out_ff` | `audio_device.h:207` | `audio_device.h:407-409` | `bool tud_audio_clear_ep_out_ff(void)` |
| `tud_audio_clear_ep_in_ff` | `audio_device.h:214` | `audio_device.h:423-425` | `bool tud_audio_clear_ep_in_ff(void)` |

Both are **compiled out unless the matching direction is enabled**: `tud_audio_read` and
friends live inside `#if CFG_TUD_AUDIO_ENABLE_EP_OUT` (`audio_device.h:397-415`) and
`tud_audio_write` inside `#if CFG_TUD_AUDIO_ENABLE_EP_IN` (`audio_device.h:417-441`). Getting
the config macros wrong produces a *compile* error here, not a silent one — which is the good
case.

**Two return-value traps to code against:**

- Both `_n_` bodies open with `TU_VERIFY(func_id < CFG_TUD_AUDIO && _audiod_fct[func_id].p_desc
  != NULL)` (`audio_device.c:449`, `:501`). In a `uint16_t`-returning function that expands to
  `return 0`. So **a read or write issued before the audio function is opened returns 0, not an
  error** — indistinguishable from "no data". Do not read a 0 return as "the stream is idle".
- The FIFOs are **byte** FIFOs with no frame framing. `tud_audio_read` will happily return a
  partial frame. The adapter must handle non-frame-multiple returns rather than assume
  alignment (`examples/device/uac2_headset/src/main.c:573-599` shows the byte-count arithmetic).

The polled loop shape is exactly what R1 assumed and is demonstrated in
`examples/device/uac2_headset/src/main.c:564-601`: once per millisecond, `tud_audio_read()`
into a buffer, transform, `tud_audio_write()` out.

### R13.4 — Data-transfer-complete hooks that replaced the old pre/post family

Present, but **optional and not needed for a polled design**. Recorded so nobody reaches for
them by half-memory:

| Hook | Declared | Weak stub | Signature |
|---|---|---|---|
| TX complete | `audio_device.h:243-244` | `audio_device.c:282-289` | `bool tud_audio_tx_done_isr(uint8_t rhport, uint16_t n_bytes_sent, uint8_t func_id, uint8_t ep_in, uint8_t cur_alt_setting)` |
| RX complete | `audio_device.h:251-252` | `audio_device.c:294-301` | `bool tud_audio_rx_done_isr(uint8_t rhport, uint16_t n_bytes_received, uint8_t func_id, uint8_t ep_out, uint8_t cur_alt_setting)` |

Call sites: `audio_device.c:487` (RX, from `audiod_rx_xfer_isr`, *after* the received packet has
already been copied into the OUT FIFO and the next receive scheduled) and `audio_device.c:553`
(TX, from `audiod_tx_xfer_isr`, *after* the next packet has been loaded from the IN FIFO). Both
run in **ISR context**; the in-source comment at `audio_device.h:241-242` explicitly says they
are "normally not needed" because the data transfer should be driven by the audio clock.
Returning `false` from either aborts the driver's transfer chain via `TU_VERIFY` — a live
hazard if one is defined carelessly. **D-decision stands: this adapter defines neither.**

### R13.5 — `CFG_TUD_AUDIO_*` macros T026 must set in `tusb_config.h`

Read off the configuration block at `src/class/audio/audio_device.h:38-162`. Split by whether
omission is caught at compile time.

**Hard-required — omission is a `#error`, so T026 cannot silently miss these:**

| Macro | Where enforced | What it gates |
|---|---|---|
| `CFG_TUD_AUDIO` | default `0` at `src/tusb_option.h:653` | Number of audio functions. Must be `1`; the whole class driver is compiled out at `0`. |
| `CFG_TUD_AUDIO_ENABLE_EP_IN` | default `0`, `audio_device.h:47-49` | Microphone/IN direction. Gates `tud_audio_write` (`:417`) and all IN state. **Must be `1`.** |
| `CFG_TUD_AUDIO_ENABLE_EP_OUT` | default `0`, `audio_device.h:51-53` | Speaker/OUT direction. Gates `tud_audio_read` (`:397`). **Must be `1`.** |
| `CFG_TUD_AUDIO_FUNC_1_EP_IN_SZ_MAX` | `#error` at `audio_device.h:57-59` | Largest IN packet across all alt settings; drives the linear buffer. |
| `CFG_TUD_AUDIO_FUNC_1_EP_OUT_SZ_MAX` | `#error` at `audio_device.h:73-75` | Largest OUT packet across all alt settings. |
| `CFG_TUD_AUDIO_FUNC_1_EP_IN_SW_BUF_SZ` | defaults to `0` (`:89-91`), which then trips the `#error` at `:110-112` | Software IN FIFO depth. Must be `>= ..._EP_IN_SZ_MAX`; the default of 0 always fails the check, so this is effectively mandatory. |
| `CFG_TUD_AUDIO_FUNC_1_EP_OUT_SW_BUF_SZ` | defaults to `0` (`:99-101`), trips `#error` at `:128-130` | Software OUT FIFO depth. Same rule. |

**Soft — silently defaulted, so T026 should set them deliberately:**

| Macro | Default | Line | Note |
|---|---|---|---|
| `CFG_TUD_AUDIO_CTRL_BUF_SZ` | `64` | `audio_device.h:42-44` | EP0 control buffer for class requests. Interacts with `CFG_TUD_ENDPOINT0_BUFSIZE` at `audio_device.c:428-435`. |
| `CFG_TUD_AUDIO_EP_IN_FLOW_CONTROL` | **`1`** | `audio_device.h:147-149` | **Defaults ON.** Selects `audiod_tx_packet_size()` (rate-varying packets driven by FIFO level vs. the IN FIFO threshold) instead of the plain `tu_min16(fifo_count, ep_in_sz)` path — see `audio_device.c:538-543`. It also makes `audiod_parse_flow_control_params()` scan the descriptor (`:1198-1200`). This is a real behavioural choice for a device with no local clock and it is ON unless explicitly set to `0`. **Flagged for the operator — not decided here.** |
| `CFG_TUD_AUDIO_ENABLE_FEEDBACK_EP` | `0` | `audio_device.h:152-154` | See R13.6. |
| `CFG_TUD_AUDIO_ENABLE_INTERRUPT_EP` | `0` | `audio_device.h:157-159` | UAC2 status interrupt EP. Off is correct here; leaving it off also saves an endpoint against the R2 budget. |

Sizing helper: `TUD_AUDIO_EP_SIZE(_is_highspeed, _maxFrequency, _nBytesPerSample, _nChannels)`
at `src/device/usbd.h:809` — the shipped examples compute their `*_SZ_MAX` from it.

Note that the `CFG_TUD_AUDIO_FUNC_1_N_CHANNELS_TX` / `_N_BYTES_PER_SAMPLE_*` / `_SAMPLE_RATE`
macros seen in the examples' `tusb_config.h` are **not driver macros** — a grep of
`audio_device.c` and `audio_device.h` for `CFG_TUD_AUDIO` yields only the twenty names in the
tables above. The examples define them purely to compute their own descriptors and EP sizes,
and say so in their own comments ("This value is not required by the driver, it parses this
information from the descriptor"). This adapter may adopt the same convention, but must not
expect the driver to read them.

**Adjacent, for the same file (T026):** MIDI takes `CFG_TUD_MIDI_RX_EPSIZE` /
`CFG_TUD_MIDI_TX_EPSIZE`, both defaulting to `TUD_EPSIZE_BULK_MAX` with the older
`CFG_TUD_MIDI_EP_BUFSIZE` honoured as a fallback (`src/class/midi/midi_device.h:37-50`). CDC
takes `CFG_TUD_CDC_TX_BUFSIZE` / `CFG_TUD_CDC_RX_BUFSIZE` and `CFG_TUD_CDC_TX_EPSIZE` /
`CFG_TUD_CDC_RX_EPSIZE`, same default and same legacy `CFG_TUD_CDC_EP_BUFSIZE` fallback
(`src/class/cdc/cdc_device.h:39-60`), plus `CFG_TUD_CDC_NOTIFY` defaulting to `0` (`:39-41`).

### R13.6 — No feedback endpoint: CONFIRMED permitted (FR-027, D20)

The pinned driver **does not require a feedback endpoint for an async OUT stream**. Absence is
expressed by simply leaving `CFG_TUD_AUDIO_ENABLE_FEEDBACK_EP` at its default of `0`
(`audio_device.h:152-154`) and omitting the feedback EP from the descriptor. Evidence:

- Every feedback code path in the driver is inside `#if CFG_TUD_AUDIO_ENABLE_FEEDBACK_EP` or
  `#if CFG_TUD_AUDIO_ENABLE_EP_OUT && CFG_TUD_AUDIO_ENABLE_FEEDBACK_EP` — the feedback state,
  `audiod_fb_send`, `audiod_fb_params_prepare`, the SOF-interrupt enable, and the
  `tud_audio_n_fb_set` / `tud_audio_feedback_update` API all vanish at `0`. There is **no
  `#error`, `TU_ASSERT` or runtime check anywhere that demands a feedback EP when
  `CFG_TUD_AUDIO_ENABLE_EP_OUT` is `1`.**
- The endpoint-open loop classifies descriptors as data vs. feedback at
  `audio_device.c:1168-1180` (UAC2: `bmAttributes.usage == 0 || == 2` is data, `== 1` is
  feedback). With the macro off, the `is_feedback_ep` result is explicitly discarded —
  `(void) is_feedback_ep;` at `audio_device.c:1237` and `:1240`. A descriptor with no feedback
  EP simply never produces that classification.
- The teardown path's feedback cleanup is likewise guarded (`audio_device.c:1131-1137`), and
  the SOF interrupt is only enabled when some function actually has `ep_fb != 0`
  (`audio_device.c:1266-1274`).
- **Shipped precedent in the pinned tree:** `examples/device/uac2_headset` runs UAC2 with *both*
  an OUT and an IN stream and **never defines `CFG_TUD_AUDIO_ENABLE_FEEDBACK_EP`** — see its
  `src/tusb_config.h:101-165`, which sets `CFG_TUD_AUDIO_ENABLE_EP_IN 1` (`:134`) and
  `CFG_TUD_AUDIO_ENABLE_EP_OUT 1` (`:151`) and no feedback macro at all. By contrast
  `examples/device/uac2_speaker_fb/src/tusb_config.h:146` sets it to `1`, and that is the
  example whose name advertises feedback. So the no-feedback duplex configuration is not a
  clever corner — it is the shipped default of the closest example.

D20 / FR-027 therefore stand as written. (Whether a *host* is happy with an async OUT stream
carrying no feedback is a host-behaviour question the pinned source cannot answer; that belongs
to HIL, not to this record.)

### R13.7 — MIDI and CDC entry points for the composite (CONFIRMED)

MIDI, from `src/class/midi/midi_device.h`:

| Entry point | Line | Signature |
|---|---|---|
| `tud_midi_mounted` | `:101-103` (over `:68`) | `bool tud_midi_mounted(void)` → `tud_midi_n_mounted(0)` |
| `tud_midi_available` | `:105-107` (over `:71`) | `uint32_t tud_midi_available(void)` → `tud_midi_n_available(0, 0)`; **bytes**, not packets |
| `tud_midi_packet_read` | `:123-125` (over `:87`) | `bool tud_midi_packet_read(uint8_t packet[4])` — one 4-byte USB-MIDI event packet; `false` when none |
| `tud_midi_packet_read_n` | `:127-129` (over `:90`) | `uint32_t tud_midi_packet_read_n(uint8_t packets[], uint32_t max_packets)` → packets read |
| `tud_midi_packet_write` | `:131-133` (over `:93`) | `bool tud_midi_packet_write(const uint8_t packet[4])` |
| `tud_midi_packet_write_n` | `:135-137` (over `:96`) | `uint32_t tud_midi_packet_write_n(const uint8_t packets[], uint32_t n_packets)` |
| `tud_midi_stream_read` | `:109-111` (over `:74`) | `uint32_t tud_midi_stream_read(void *buffer, uint32_t bufsize)` — byte-stream form |
| `tud_midi_stream_write` | `:118-121` (over `:84`) | `uint32_t tud_midi_stream_write(uint8_t cable_num, const uint8_t *buffer, uint32_t bufsize)` |
| `tud_midi_rx_cb` | `:60` | `void tud_midi_rx_cb(uint8_t itf)` — optional arrival notification |

For FR-045's CC handling the **packet** form (`tud_midi_packet_read`) is the right entry point:
one call yields one complete 4-byte event, which removes running-status and partial-message
reassembly from the adapter entirely. Note the in-source warning at `midi_device.h:79-80`: the
demux stream reader shares internal state with `tud_midi_n_stream_read` and the two must not be
mixed on one interface — a reason to pick one form and stay on it.

CDC, from `src/class/cdc/cdc_device.h`:

| Entry point | Line | Signature |
|---|---|---|
| `tud_cdc_connected` | `:209-211` (over `:108`) | `bool tud_cdc_connected(void)` — host has asserted DTR |
| `tud_cdc_write` | `:249-251` (over `:138`) | `uint32_t tud_cdc_write(void const *buffer, uint32_t bufsize)` → bytes queued |
| `tud_cdc_write_str` | `:253-255` (over `:146`) | `uint32_t tud_cdc_write_str(char const *str)` |
| `tud_cdc_write_flush` | `:257-259` (over `:151`) | `uint32_t tud_cdc_write_flush(void)` → bytes actually sent |
| `tud_cdc_write_available` | `:261-263` (over `:154`) | `uint32_t tud_cdc_write_available(void)` — free TX FIFO space |
| `tud_cdc_write_clear` | `:265-267` (over `:157`) | `bool tud_cdc_write_clear(void)` |
| `tud_cdc_available` | `:225-227` (over `:120`) | `uint32_t tud_cdc_available(void)` |
| `tud_cdc_read` | `:233-235` (over `:123`) | `uint32_t tud_cdc_read(void *buffer, uint32_t bufsize)` |

`tud_cdc_write` only fills the TX FIFO; `tud_cdc_write_flush` is what pushes it to the wire.
For a diagnostics channel, check `tud_cdc_write_available()` before writing and treat a short
return from `tud_cdc_write` as dropped diagnostics to be counted rather than blocked on — a
blocking retry in the main loop would starve the audio poll.

### R13.8 — Examples: no shipped stereo-in-with-stereo-out template (plan's claim CONFIRMED)

The tree ships eight audio examples (`examples/device/`): `audio_test`,
`audio_test_freertos`, `audio_test_multi_rate`, `audio_4_channel_mic`,
`audio_4_channel_mic_freertos`, `uac2_headset`, `uac2_speaker_fb`, `cdc_uac2`. Their
`tusb_config.h` channel counts were read directly:

- IN-only: `audio_test` (1 ch TX), `audio_test_freertos` (1 ch), `audio_test_multi_rate` (1 ch),
  `audio_4_channel_mic` / `_freertos` (4 ch TX).
- OUT-only: `uac2_speaker_fb` (2 ch RX, **with** feedback EP).
- Both directions: **`uac2_headset`** — `CFG_TUD_AUDIO_FUNC_1_N_CHANNELS_TX 1`,
  `..._N_CHANNELS_RX 2` (`examples/device/uac2_headset/src/tusb_config.h:118-119`); and
  **`cdc_uac2`** — TX 1, RX 1 (`examples/device/cdc_uac2/src/tusb_config.h:117-118`).

So **no shipped example is stereo-in *and* stereo-out**. The plan's claim holds.

**Closest example: `examples/device/uac2_headset`.** It is duplex UAC2 in a single audio
function with no feedback EP, and it demonstrates the exact three things this adapter needs —
per-interface direction dispatch in the alt-setting callbacks
(`src/main.c:526-556`), the polled `tud_audio_read` → transform → `tud_audio_write` loop
(`src/main.c:564-601`), and a hand-rolled duplex descriptor. Its gap from this design is
channel count on the IN side (1 vs. 2) and the interrupt EP it enables (`tusb_config.h:109`),
which this design does not want.

**Second reference: `examples/device/cdc_uac2`** — the only shipped composite that combines CDC
with UAC2 in one device (`CFG_TUD_CDC 1`, `CFG_TUD_AUDIO 1`, `CFG_TUD_MIDI 0` at
`examples/device/cdc_uac2/src/tusb_config.h:97-101`). It is the right model for the
descriptor/IAD layout of the CDC half, and confirms nothing in the audio driver objects to
sharing a device with CDC. **No shipped example combines audio + MIDI + CDC**, so the
three-function composite descriptor is this feature's own work.

**Correction to D9's premise.** R1 records that `uac2_headset` "references
`TUD_AUDIO_HEADSET_STEREO_DESC_LEN`, defined only in a *different* example's header, so it is
broken on master and on 0.21.0 alike". In the pinned tree that reference exists but is
**dead**: `examples/device/uac2_headset/src/usb_descriptors.c:78` defines `CONFIG_TOTAL_LEN` in
terms of that undefined macro, and `CONFIG_TOTAL_LEN` is then **never used anywhere in the
file** — a grep for it returns only line 78. The live descriptors use the versioned macros
`TUD_AUDIO10_HEADSET_STEREO_DESCRIPTOR` and `TUD_AUDIO20_HEADSET_STEREO_DESCRIPTOR`, both
defined in the example's **own** header (`usb_descriptors.h:199` and `:90`, with lengths at
`:173` and `:51`). Because an unused `#define` is never expanded, the example compiles and is
usable as a reference. D9's *conclusion* (pin 0.21.0, don't track master) is unaffected and
still stands on its other grounds — but the "the headset example is broken" reasoning should
not be repeated, because it would wrongly discourage the one example worth copying.

Separately, the shipped descriptor **template macros** in `src/device/usbd.h` are only four and
none of them is duplex: `TUD_AUDIO10_MIC_ONE_CH_DESCRIPTOR` (`:562`),
`TUD_AUDIO20_MIC_ONE_CH_DESCRIPTOR` (`:679`), `TUD_AUDIO20_MIC_FOUR_CH_DESCRIPTOR` (`:728`),
`TUD_AUDIO20_SPEAKER_MONO_FB_DESCRIPTOR` (`:776`). Any duplex descriptor is hand-assembled from
the per-element `TUD_AUDIO20_DESC_*` pieces, which is what `uac2_headset` does.

### R13.9 — What this changes for downstream tasks

- **T026 (`tusb_config.h`)**: the seven hard-required and four soft `CFG_TUD_AUDIO_*` macros in
  R13.5, plus the MIDI/CDC macros noted there. Decide `CFG_TUD_AUDIO_EP_IN_FLOW_CONTROL`
  explicitly rather than inheriting its default of `1`.
- **T035 and the alt-setting tasks**: use `tud_audio_set_itf_cb` /
  `tud_audio_set_itf_close_ep_cb` (lower-case `ep`), decode `wIndex`/`wValue` per R13.2, and
  keep **two** independent per-direction streaming flags. Non-`_n_` entry points throughout;
  `func_id` 0.
- **Any task that "verifies" a callback fired**: linking is not evidence (R13.0 item 3).
  Require an observed side effect.

**Still not answered by source, and deliberately left open**: whether a macOS/Windows host will
run an async OUT stream with no feedback EP at an acceptable glitch rate (HIL, OQ2), and the
FIFO depths to give `..._SW_BUF_SZ` (HIL, OQ1 / **D23**). The shipped examples use
`4 × EP_SZ` for full speed as a starting point
(`examples/device/uac2_headset/src/tusb_config.h:148,165`), which is a starting point and not a
measurement.

---

## R14 — OTG_FS endpoint and FIFO-RAM budget for the three-function composite (T029)

**Date**: 2026-08-23 · **Task**: T029 [US4] · **Status**: verified against ST's own device
header and the pinned TinyUSB FIFO allocator — **the budget CLOSES on both constraints**

**Decision**: The three-function composite (UAC2 duplex audio + USB MIDI + CDC ACM) **fits** the
STM32F446 OTG_FS core with room to spare. It needs **4 IN and 3 OUT** non-control endpoints
against **5 and 5** available, and **225 of 320** FIFO words (900 of 1280 bytes). T027 may write
the full descriptor — including the CDC notification endpoint — without dropping, sharing, or
shrinking anything. R2's "**Must be confirmed during implementation** against the F446 OTG_FS
endpoint count" is hereby discharged, and the spec's Assumptions entry ("Adding the CDC function
keeps the device within full-speed bandwidth and interface-count limits") is confirmed on the
endpoint and FIFO-RAM axes.

**Rationale for recording it rather than just doing it**: the failure mode of an over-subscribed
budget is not a compile error. `dfifo_alloc` fails via `TU_ASSERT`
(`src/portable/synopsys/dwc2/dcd_dwc2.c:237`), which in a release build returns `false` up
through `dcd_edpt_open` and manifests as a *partial* enumeration — some interfaces alive, one
silently dead. That is exactly the confusing symptom the arithmetic below buys us out of, and
it is why the numbers are written down instead of merely satisfied.

**Provenance**: every figure below is a file:line citation from a pinned tree in
`cmake/dependencies.cmake`. Nothing is from training data.

- ST CMSIS device headers, `cmsis_device_f4` **v2.6.11** (`cmake/dependencies.cmake:118`),
  cached at `external/.cpm-cache/cmsis_device_f4/a45b3a84e2ff802229afbb6a7c26dd2abff6ca38`.
- TinyUSB **0.21.0** (`cmake/dependencies.cmake:112`), cached at
  `external/.cpm-cache/tinyusb/d34550b3aaa115e7ec09bea0c9e676531bf95dfb` — the same tree R13
  was read from. Paths below are relative to those two roots.
- `adapters/nucleo/tusb_config.h` as landed by **T026** (commit `d97c827`) for the packet sizes.

### R14.1 — How many device-mode endpoints the silicon provides

**Primary source — ST's own device header**, `Include/stm32f446xx.h`, under the banner
`/****************************** USB Exported Constants ************************/`:

```
15911:#define USB_OTG_FS_HOST_MAX_CHANNEL_NBR                12U
15912:#define USB_OTG_FS_MAX_IN_ENDPOINTS                    6U    /* Including EP0 */
15913:#define USB_OTG_FS_MAX_OUT_ENDPOINTS                   6U    /* Including EP0 */
15914:#define USB_OTG_FS_TOTAL_FIFO_SIZE                     1280U /* in Bytes */
```

So: **6 IN and 6 OUT endpoints, EP0 inclusive** → endpoint *numbers* 0–5, of which **EP1–EP5
(5 numbers) are available for non-control use in each direction**.

**Corroboration 1 — the pinned TinyUSB port derives its limit from that same header**, so the
driver and the silicon cannot disagree:
`src/portable/synopsys/dwc2/dwc2_stm32.h:50-52` (the `OPT_MCU_STM32F4` arm) sets
`#define EP_MAX_FS  USB_OTG_FS_MAX_IN_ENDPOINTS` and `#define DFIFO_DEPTH_FS  320`, and
`:148-150` wires those into the controller table as
`{ ..., .ep_count = EP_MAX_FS, .otg_dfifo_depth = DFIFO_DEPTH_FS }`. The file's own header
comment (`:34-35`) defines the units: "`EP_MAX` : Max number of bi-directional endpoints
including EP0" and "`DFIFO_DEPTH_FS/HS` : DFIFO depth in 32-bit words (`OTG_DFIFO_DEPTH`)".

**Corroboration 2**: `src/common/tusb_mcu.h:262-267` independently sets
`#define TUP_DCD_ENDPOINT_MAX 6` for `OPT_MCU_STM32F4`.

**RM0390 note — read this before trusting a recollection of "6 endpoints".** ST's prose counts
the same silicon differently: the reference-manual/datasheet wording for the F446 OTG_FS is
"1 bidirectional control endpoint0, 5 IN endpoints, 5 OUT endpoints" (plus 12 host channels and
1.25 Kbyte of dedicated RAM). That is **the same core** — 1 + 5 = 6 per direction — but the
"5" in the prose is the *non-control* count while the "6" in the header is the *inclusive*
count. Getting these two confused in either direction is a one-endpoint error in a budget with
one endpoint of slack, so the table in R14.3 states both. *Honest gap*: the RM0390 PDF itself
was not fetched — two attempts at
`st.com/resource/en/reference_manual/rm0390-…pdf` and at the F446 datasheet timed out — so the
prose figures above are secondhand, and the header at `stm32f446xx.h:15912-15914` is the
citation this record actually rests on. It is ST-authored and machine-checkable in-tree, which
is the stronger source for this purpose anyway.

**One important thing the header does NOT constrain**: STM32F4 is *not* in the
`CFG_TUD_ENDPOINT_ONE_DIRECTION_ONLY` list (`src/common/tusb_mcu.h:774-776`, default `0`), so
**the same endpoint number may carry both an IN and an OUT endpoint**. TinyUSB's own composite
example relies on this — the generic (non-exception) branch of
`examples/device/cdc_uac2/src/usb_descriptors.c:123-127` assigns `EPNUM_AUDIO_IN 0x01` and
`EPNUM_AUDIO_OUT 0x01`. This is what makes the budget comfortable rather than tight.

### R14.2 — The FIFO RAM budget

**1280 bytes = 320 words of 32 bits**, dedicated SRAM shared between the single RX FIFO and all
TX FIFOs. Sources: `stm32f446xx.h:15914` (`USB_OTG_FS_TOTAL_FIFO_SIZE 1280U /* in Bytes */`)
and `dwc2_stm32.h:52` (`DFIFO_DEPTH_FS 320`, in 32-bit words). 320 × 4 = 1280. The two agree.

Do **not** confuse this with `stm32f446xx.h:1056` `USB_OTG_FIFO_SIZE 0x1000UL` — that is the
4 KB *address window* stride used to reach each endpoint's FIFO through the peripheral aperture
(`USB_OTG_FIFO_BASE 0x1000UL` at `:1055`), not the amount of RAM behind it.

**The sizing rules, quoted, not guessed.** `src/portable/synopsys/dwc2/dcd_dwc2.c:187-201`
states them and cites the RM:

> According to "FIFO RAM allocation" section in RM, FIFO RAM are allocated as follows (each
> word 32-bits):
> - Each EP IN needs at least max packet size
> - All EP OUT shared a unique OUT FIFO which uses […]
>   - 13 for setup packets + control words (up to 3 setup packets).
>   - 1 for global NAK (not required/used here).
>   - Largest-EPsize/4 + 1. (FS: 64 bytes, HS: 512 bytes). Recommended is "2 x (Largest-EPsize/4 + 1)"
>   - 2 for each used OUT endpoint.
>
> Therefore, GRXFSIZ = 13 + 1 + 2 x (Largest-EPsize/4 + 1) + 2 x EPOUTnum

Implemented at `:200-202`:

```c
static inline uint16_t calc_device_grxfsiz(uint16_t largest_ep_size, uint8_t ep_count) {
  return (uint16_t)(13 + 1 + 2 * ((largest_ep_size / 4) + 1) + 2 * ep_count);
}
```

Three details of the implementation that change the arithmetic and are easy to get wrong:

1. The `2 x EPOUTnum` term is fed the controller's **total** `ep_count` (**6**), not the number
   of OUT endpoints this device actually uses — see the call sites at `:216` and `:256`, both
   passing `dwc2_controller->ep_count`. So that term is a fixed **12 words** here regardless of
   how many OUT endpoints the descriptor declares.
2. Each TX FIFO is `ceil(packet_size / 4)` words (`:213`), allocated top-down; the RX FIFO grows
   upward from 0 and the free space sits between them (`:170-186`).
3. Double buffering would double a *bulk* IN FIFO (`:230-234`), but it is **off**: the shipped
   default is `.bm_double_buffered = 0` (`src/device/usbd.h:48`,
   `CFG_TUD_CONFIGURE_DWC2_DEFAULT`).

**When the allocation happens** — this matters, because it means all three functions'
endpoints are resident simultaneously and the sum below is the right model, not a worst case
that never occurs. `dfifo_device_init` (`:253-268`) runs on reset and on
`dcd_edpt_close_all`, seeding `grxfsiz` from `CFG_TUD_ENDPOINT0_SIZE` and allocating EP0 IN.
Bulk/interrupt FIFOs are allocated in `dcd_edpt_open` (`:611`) when a class driver opens its
endpoints at *set-configuration* time. Isochronous FIFOs are allocated **once, at
set-configuration, and never resized by an alt-setting change** — the audio driver walks its
own descriptor and calls `usbd_edpt_iso_alloc(rhport, ep_in, ep_in_size)` /
`(…, ep_out, ep_out_size)` with the **maximum `wMaxPacketSize` across all alt settings**
(`src/class/audio/audio_device.c:930-962`, sizes accumulated with `TU_MAX` at `:936` and
`:944`), reaching `dcd_edpt_iso_alloc` → `dfifo_alloc` at `dcd_dwc2.c:642-645`. This path is
live for dwc2: `TUP_DCD_EDPT_ISO_ALLOC` is defined for every USBIP that is not
IP3511/RUSB2 (`src/common/tusb_mcu.h:768-771`).

`CFG_TUD_DWC2_DMA_ENABLE` reserves a further `2 × ep_count = 12` words for EPInfo
(`:260-262`), but only when `ghwcfg2.arch == GHWCFG2_ARCH_INTERNAL_DMA` (`:130-135`); F446
OTG_FS is a slave-mode core, so this does not apply. It is included in the sensitivity check at
R14.6 anyway.

### R14.3 — What this design needs

Packet sizes are **not assumed** — they are the values T026 fixed in
`adapters/nucleo/tusb_config.h` (commit `d97c827`): `CFG_TUD_ENDPOINT0_SIZE 64` (`:77`),
`CFG_TUD_AUDIO_FUNC_1_EP_IN_SZ_MAX` / `…_EP_OUT_SZ_MAX` = 196 (`:193-197`),
`CFG_TUD_MIDI_RX_EPSIZE` / `…_TX_EPSIZE` 64 (`:290-291`), `CFG_TUD_CDC_RX_EPSIZE` /
`…_TX_EPSIZE` 64 (`:318-319`). Two endpoints this design does **not** carry are likewise
config-fixed, not assumed: `CFG_TUD_AUDIO_ENABLE_FEEDBACK_EP 0` (`:270`, FR-027 / D20,
confirmed permitted in R13.6) and `CFG_TUD_AUDIO_ENABLE_INTERRUPT_EP 0` (`:279`).

**Audio packet size, with the arithmetic shown.** 48 kHz, 16-bit, stereo at full speed is one
isochronous packet per 1 ms frame, so the *nominal* payload is
48 frames × 2 channels × 2 bytes = **192 bytes**. But D21 / FR-028 require accepting up to
**49** stereo frames — `kMaxPacketFrames = 49` and `kChannels = 2` in
`adapters/nucleo/support/sample-format.h:18,24` — so the endpoint must be declared at

> 49 frames × 2 channels × 2 bytes/sample = **196 bytes**, **per direction**.

196 is used below for both the IN and the OUT iso endpoint. The extra frame is 4 bytes over
nominal on each side and costs 1 TX word and 2 RX words versus 192 — noted so nobody is tempted
to "save" it.

**Endpoint table** (addresses follow the generic branch of TinyUSB's `cdc_uac2` example, with
MIDI slotted into the free EP2; T027 may renumber, the totals are what bind):

| Address | Number | Dir | Transfer type | wMaxPacketSize | Owning function | Descriptor source |
|---|---|---|---|---|---|---|
| `0x00`/`0x80` | 0 | both | Control | 64 | device (EP0) | mandatory |
| `0x01` | 1 | OUT | Isochronous, adaptive sink (FR-025) | **196** | UAC2 AS-OUT (host → device) | hand-rolled from `TUD_AUDIO20_DESC_*` (D10 / FR-021) |
| `0x81` | 1 | IN | Isochronous, async source (FR-026) | **196** | UAC2 AS-IN (device → host) | hand-rolled from `TUD_AUDIO20_DESC_*` |
| `0x02` | 2 | OUT | Bulk | 64 | USB MIDI | `TUD_MIDI_DESCRIPTOR` (`src/device/usbd.h:419-424`) |
| `0x82` | 2 | IN | Bulk | 64 | USB MIDI | same |
| `0x83` | 3 | IN | Interrupt (notification) | 8 | CDC ACM comm interface | `TUD_CDC_DESCRIPTOR` (`src/device/usbd.h:262-283`, EP at `:275`) |
| `0x04` | 4 | OUT | Bulk | 64 | CDC ACM data | same, EP at `:281` |
| `0x84` | 4 | IN | Bulk | 64 | CDC ACM data | same, EP at `:283` |

No feedback endpoint (FR-027 / D20) and no audio interrupt endpoint — deliberate absences, both
confirmed in R13.6 and pinned in `tusb_config.h`. MIDI contributes exactly one bulk pair:
`TUD_MIDI_DESCRIPTOR` expands to `TUD_MIDI_DESC_EP(_epout, …)` + `TUD_MIDI_DESC_EP(_epin, …)`
and nothing more (`src/device/usbd.h:419-424`, `:407-411`).

**Totals, by direction** (the IN side is the binding one, as expected):

| | IN | OUT |
|---|---|---|
| Non-control endpoints required | **4** (`0x81` `0x82` `0x83` `0x84`) | **3** (`0x01` `0x02` `0x04`) |
| Non-control endpoints available (EP1–EP5) | 5 | 5 |
| Including EP0 — required / available | 5 / 6 | 4 / 6 |
| **Spare** | **1 IN** | **2 OUT** |

Distinct endpoint *numbers* used: **4** (EP1–EP4) of 5. EP5 is entirely free in both
directions, as is EP3 OUT. The driver's only hard guard on this axis is
`TU_ASSERT(epnum < ep_count)` at `dcd_dwc2.c:211`, i.e. `epnum ≤ 5`; the separate
`allocated_epin_count` ceiling at `:225-229` is inert on STM32 because `ep_in_count` is left `0`
in the controller table (`dwc2_stm32.h:148-151`) and the check is gated on it being non-zero.
**Endpoint-count verdict therefore rests on the descriptor being correct, not on a runtime
guard catching a mistake** — one more reason to fix the numbers here, before T027.

### R14.4 — FIFO RAM arithmetic

**RX FIFO** (single, shared by all OUT endpoints). Largest OUT packet = 196 (audio iso OUT);
`ep_count` = 6 per R14.2 detail 1:

```
GRXFSIZ = 13 + 1 + 2 × ((196 / 4) + 1) + 2 × 6
        = 13 + 1 + 2 × (49 + 1)        + 12
        = 13 + 1 + 100                 + 12
        = 126 words   (504 bytes)
```

**TX FIFOs**, one per IN endpoint, `ceil(mps / 4)` words each:

| IN endpoint | Function | mps (bytes) | words |
|---|---|---:|---:|
| `0x80` EP0 IN | control | 64 | 16 |
| `0x81` | audio iso IN | 196 | **49** |
| `0x82` | MIDI bulk IN | 64 | 16 |
| `0x83` | CDC notify | 8 | 2 |
| `0x84` | CDC bulk IN | 64 | 16 |
| | | **TX total** | **99** (396 bytes) |

**Total**:

```
RX 126 + TX 99 = 225 words = 900 bytes
Available       = 320 words = 1280 bytes
Free            =  95 words =  380 bytes   (29.7 % headroom)
```

### R14.5 — Correction: `CFG_TUD_CDC_NOTIFY 0` does **not** remove the notification endpoint

This record was asked mid-task to redo the arithmetic on the basis that T026's
`CFG_TUD_CDC_NOTIFY 0` (`adapters/nucleo/tusb_config.h:305`) means the CDC interrupt IN endpoint
is not instantiated, dropping the IN count from 4 to 3. **That is not what the macro does**, and
the mistake is worth recording because it is a natural reading of the name.

Read off the pinned tree:

- `CFG_TUD_CDC_NOTIFY` gates **only the optional application-notification API** — the
  declarations `tud_cdc_n_notify_msg` / `tud_cdc_n_notify_uart_state`
  (`src/class/cdc/cdc_device.h:158-181`), the implementation
  (`src/class/cdc/cdc_device.c:160-181`), and the `epnotify` endpoint buffer
  (`src/class/cdc/cdc_device.c:71-73`). Default is `0` (`cdc_device.h:39-41`).
- **`cdcd_open` opens the notification endpoint unconditionally when the descriptor declares
  one** — `src/class/cdc/cdc_device.c:318-325` is plain `if (TUSB_DESC_ENDPOINT ==
  tu_desc_type(p_desc)) { … usbd_edpt_open(rhport, desc_ep) … }` with **no `#if
  CFG_TUD_CDC_NOTIFY` around it**. Whether the endpoint exists is a property of the
  **descriptor**, i.e. of **T027**, not of `tusb_config.h`.
- The only shipped CDC template, `TUD_CDC_DESCRIPTOR` (`src/device/usbd.h:262-283`), **always**
  emits the interrupt IN endpoint (`:275`) and hard-codes `bNumEndpoints = 1` on the comm
  interface (`:266`); its `TUD_CDC_DESC_LEN` of 66 bytes (`:258`, `8+9+5+5+4+5+7+9+7+7`)
  includes that 7-byte endpoint descriptor. There is no no-notify variant in this tree.

T026's own in-file comment (`adapters/nucleo/tusb_config.h:297-304`) already says exactly this
and is correct as written; it should not be "fixed".

**So the endpoint table in R14.3 stands as the normative case: 4 IN, 3 OUT.** Omitting the
notify endpoint would require T027 to hand-roll a CDC descriptor that departs from the shipped
template, with a host-compatibility risk that is out of scope here (the CDC-ACM notification
element is optional in the class spec but is present in essentially every reference
implementation, including the one this descriptor is modelled on).

**Both figures, as requested — and the answer is that the choice is nearly free:**

| | With notify EP (**normative**, `TUD_CDC_DESCRIPTOR` as shipped) | Without notify EP (hypothetical hand-rolled CDC descriptor) |
|---|---:|---:|
| Non-control IN endpoints | 4 of 5 | 3 of 5 |
| Non-control OUT endpoints | 3 of 5 | 3 of 5 |
| Distinct EP numbers used | 4 of 5 | 3 of 5 |
| RX FIFO | 126 words | 126 words |
| TX FIFOs | 99 words | 97 words |
| **Total** | **225 / 320 words (900 / 1280 B)** | **223 / 320 words (892 / 1280 B)** |
| Free | 95 words (380 B) | 97 words (388 B) |

**The notification endpoint costs 2 words — 8 bytes — of FIFO RAM and one IN endpoint slot out
of five.** Stated prominently because the instruction to this task was to flag it loudly if the
budget closed *only* because the notify endpoint was off: **it does not.** The budget closes
with ~30 % FIFO headroom and a spare endpoint pair **either way**. There is no constraint here
for the operator to spend, and no reason for T027 to deviate from the shipped CDC template on
budget grounds.

### R14.6 — Sensitivity: what would actually break this

The pessimistic variants all still fit, which is the useful form of the answer:

| Variant | Δ words | Total | Fits? |
|---|---:|---:|:--:|
| As designed (R14.4) | — | 225 | yes |
| Double-buffer both bulk IN EPs (`bm_double_buffered`, off by default) | +32 | 257 | yes |
| Internal DMA enabled (N/A — F446 FS is slave-mode) | +12 EPInfo | 237 | yes |
| Both of the above together | +44 | 269 | yes |
| CDC notify mps 64 instead of 8 | +14 | 239 | yes |

The one knob with real teeth is the audio packet size. With both iso endpoints at `m` bytes
(`m` a multiple of 4), total = `(28 + m/2) + (50 + m/4)` = `78 + 0.75 m` words. Setting that
≤ 320 gives **`m` ≤ 320 bytes = 80 stereo frames**. FR-028's 196 bytes / 49 frames sits at 61 %
of that ceiling, so raising `kMaxPacketFrames` — or adding a second, larger streaming alt
setting, since `usbd_edpt_iso_alloc` reserves the **max across alt settings**
(`audio_device.c:936,944`) — is the change that would need this arithmetic redone. Adding a
fourth function would consume endpoint numbers first: only EP5 is fully free.

### R14.7 — Verdict, and what T027 can rely on

- **Endpoint count: CLOSES.** 4 IN / 3 OUT non-control required, 5 / 5 available; 4 of 5
  endpoint numbers used; EP5 free in both directions. Source: `stm32f446xx.h:15912-15913`,
  corroborated by `dwc2_stm32.h:50-52,148-151` and `tusb_mcu.h:262-267`.
- **FIFO RAM: CLOSES.** 225 of 320 words (900 of 1280 bytes), **95 words / 380 bytes free
  (29.7 %)**. Source: `stm32f446xx.h:15914` and `dwc2_stm32.h:52` for the size,
  `dcd_dwc2.c:187-247` for the rules.
- **No function needs to be dropped, no endpoint shared, no audio packet shrunk below FR-028.**
  There is no finding to escalate to the operator on this axis.
- **T027 may build directly against the R14.3 table.** If it renumbers endpoints, the only hard
  constraints are `epnum ≤ 5` (`dcd_dwc2.c:211`) and — since STM32F4 permits an IN and an OUT on
  the same number — no more than 5 endpoints in either direction.

**Deliberately out of scope of this record** (budget ≠ behaviour): whether 196-byte iso packets
with a 2-packet-deep RX FIFO survive real host traffic, and what `..._SW_BUF_SZ` should be —
both are HIL measurements (OQ1 / **D23**), not arithmetic. The RX FIFO's `2 × (mps/4 + 1)` term
gives room for two 196-byte OUT packets, which is TinyUSB's recommended sizing, not a measured
one.
