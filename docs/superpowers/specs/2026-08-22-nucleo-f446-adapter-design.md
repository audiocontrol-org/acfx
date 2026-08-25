# design:gap/nucleo-f446-adapter — NUCLEO-F446RE hardware layer with USB audio I/O

Roadmap item: `design:gap/nucleo-f446-adapter`
Parent: `multi:feature/hardware-targets` → `multi:feature/progressive-dsp-platform`
House rules: `stack-control-design-v1` (capture over YAGNI — nothing below is
scoped out; scoping is a separate operator pass)

> ## ⚠ Amended 2026-08-23 — read this before D1–D26
>
> This record was approved 2026-08-22. Authoring `specs/nucleo-f446-adapter/` from it
> surfaced questions D1–D26 did not answer, and one place where a later decision
> **retired an earlier one's premise while the earlier one kept its number**. Those are
> resolved here rather than left to drift.
>
> **D1–D26 keep their numbers.** 79 functional requirements cite them, so renumbering
> would break the spec's traceability. Amendments are marked inline and new decisions
> append as **D27–D36**.
>
> | Change | Where | Kind |
> |---|---|---|
> | CDC serial telemetry function added to the composite device | **D5** amended, **D27** | Extends |
> | Effect prepared at **48** frames, not 49 | **D15 SUPERSEDED by D28** | **Contradicts** |
> | `ParameterSource` made a concrete seam | **D3** amended, **D29** | Concretizes |
> | Ring state model (Stopped/Priming/Running) | **D30** | New |
> | Counter semantics: wrap, exclusivity, denominator | **D31** | New |
> | Torn payloads; `AudioTransportStats` gains an 8th field | **D32** | Extends |
> | int16 conversion: scale, rounding, clamping | **D33** | New |
> | Fatal-clock indicator made concrete | **D34** | New |
> | USB suspend / resume / bus reset | **D35** | New |
> | Statistics update separated from reporting | **D36** | New |
>
> Provenance: the 2026-08-23 clarification pass, the real-time/transport requirements
> review (`specs/nucleo-f446-adapter/checklists/realtime-transport.md`), and a
> third-party review of the resulting spec. Every one was an operator decision.
>
> **This record remains the design-level source. `specs/nucleo-f446-adapter/spec.md` is
> the operative artifact for implementation** — where the two differ, the spec is newer
> and the difference is recorded here.

## Problem domain

acfx compiles one platform-independent core into four adapters today: the JUCE
workbench, the JUCE plugin, Daisy, and Teensy. Both existing MCU adapters share
two assumptions that this target breaks:

1. **A vendor HAL owns the board.** libDaisy and the Teensy core each provide
   clock setup, an audio callback, and peripheral access. The adapter is then a
   ~100-line shim.
2. **Audio arrives through an on-board codec.** Both boards carry an I2S codec,
   so `AudioCallback` is handed float buffers that already exist.

A NUCLEO-F446RE has neither. It is a general-purpose development board with no
audio hardware at all. The proposition is that **USB Audio Class is the audio
interface** — the host becomes the codec, and the board needs no analog parts.
That makes a very cheap, very available board into an acfx target, and it
establishes a target class distinct from "dev board with a codec on it".

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

### What acfx imposes

- `acfx_core` is header-only and needs only C++17; dependencies point strictly
  inward. The adapter may depend on core; core must never learn about USB.
- No heap allocation and no locks in any `process()` / audio-callback path.
- Descriptive names, never numeric prefixes (Commandment 3).
- CMake with CPM-pinned dependencies and a `cpm-package-lock.cmake` entry.
  *(Amended 2026-08-23: `cpm-package-lock.cmake` holds only its header comment —
  no package entries exist for any dependency. The repo's real pinning mechanism is
  an explicit `GIT_TAG` per `CPMAddPackage` in `cmake/dependencies.cmake`. This
  target follows what the repo actually does; the convention-vs-reality gap is
  repo-wide and pre-existing, and is captured in the backlog rather than fixed here.)*
- Scope is the operator's call (Commandment 5).

### The adapter contract

Every adapter does the same four things, and `adapters/daisy/daisy-main.cpp` is
the exemplar:

```cpp
AppEffect effect;                                    // ACFX_EFFECT_TYPE, CMake-injected
effect.prepare(acfx::ProcessContext{sr, blockSize, channels});
float* channels[2] = { left, right };                // NON-interleaved float
acfx::AudioBlock block(channels, 2, numFrames);
effect.process(block);                               // in place, no heap
effect.setParameter(acfx::ParamId{n}, normalized);   // 0..1
```

### The two frictions this design has to resolve

**Data shape.** `AudioBlock` wants non-interleaved `float*` per channel. USB
Audio carries interleaved `int16`. Something must de-interleave and convert on
the way in, and reverse it on the way out, every 1 ms frame, without allocating.

**Verifiability.** `core/` has a host-side doctest suite. The Daisy and Teensy
adapters have **no behavioural tests at all** — CI proves only that they
cross-compile and link. A single-file adapter offers no seam a host test can
reach, so matching that precedent would import its blind spot.

## Solution space

### Chosen — Two-layer adapter: hardware shim plus a host-testable support library

`adapters/nucleo/` splits into a thin part that touches silicon and a thicker
part that does not:

- `nucleo-main.cpp` — clock bring-up, GPIO alternate-function setup, TinyUSB
  init, `OTG_FS_IRQHandler`, and the `tud_task()` loop. Cannot be host-tested and
  is kept as small as that implies.
- `acfx_nucleo_support` — a separate CMake target holding the int16↔float
  conversion, de-interleaving, the audio ring buffer, and the MIDI-CC→`ParamId`
  mapping. Compiles on the host and is covered by doctest. *(Amended 2026-08-23:
  also the per-`ParamId` shadow block, the `ParameterSource` seam (**D29**), and
  the `AudioTransportStats` record — six headers in all. The target is declared
  **unconditionally**, not behind the board's build option, because the host test
  preset sets no toolchain and would otherwise never see it — which would silently
  reopen the untested-glue gap this decomposition exists to close.)*

This applies acfx's own "platform-independent core, thin adapter" principle
recursively inside the adapter, and it is the only option that satisfies the
chosen verification strategy. Cost: two targets and a slightly less
conventional-looking adapter than the two that already exist.

### Rejected — Single file, mirroring `daisy-main.cpp` and `teensy-main.cpp`

Most conventional; a reader who knows the Daisy adapter would recognise it
immediately. Rejected because it leaves no seam for host-side unit tests, and
the operator explicitly chose host-side adapter tests as one of three
verification layers. Matching precedent would mean inheriting the precedent's
untested-glue problem, in an adapter that has strictly more glue than either
existing one.

### Rejected (deferred, not discarded) — Three layers with a reusable USB-audio transport

As chosen, but with the UAC2/TinyUSB transport factored so other STM32 boards
could reuse it. The new `multi:feature/hardware-targets` parent makes further
boards likely, so this is a genuine direction rather than speculation. Rejected
**for now** on the grounds that the correct seam between "board" and "transport"
is guesswork until a second board exists to reveal it; extracting later against
two real users is cheaper than guessing against one. Recorded as an open
question, not cut.

### Rejected — I2S codec instead of USB for audio I/O

Attach a PCM5102/WM8731-class codec to SAI/I2S and treat USB purely as control.
This is the arrangement both existing MCU adapters use, so it would need no new
transport work. Rejected because it defeats the purpose: the value of this target
is that a bare, cheap board with no analog hardware becomes an acfx target, and
because the operator chartered the node as "Nucleo F446 + USB audio I/O". Also
records a second clock domain and a sample-rate reconciliation problem that USB-
only avoids entirely.

### Rejected — Build on STM32Cube HAL rather than bare metal

Use ST's HAL/LL drivers and the CubeMX-generated clock configuration instead of
writing register-level setup. Faster initial bring-up and a well-trodden path.
Rejected because it drags a large vendor scaffolding into a repo whose stated
principle is thin adapters over a clean core, and because the spike demonstrated
the register-level clock and OTG_FS setup is roughly 150 lines — the HAL would be
more code to carry, not less.

## Decisions

| # | Decision | Rationale |
|---|---|---|
| D1 | Two-layer decomposition: `nucleo-main.cpp` + `acfx_nucleo_support` | Only option satisfying the chosen verification strategy |
| D2 | **USB MIDI** is the first parameter transport, via a `ParameterSource` abstraction | Operator's choice; physical peripherals are coming and must plug into the same seam |
| D3 | Physical peripheral support (pots/encoders/buttons) is a **known-implied requirement**, shaping the `ParameterSource` contract now — *amended 2026-08-23: the contract is made concrete in **D29**, because "shaped now" with nothing to point at is an intention, not a seam* | Operator stated it explicitly; captured rather than deferred out of the record |
| D4 | Advertise **48 kHz / 16-bit stereo only**, one alt-setting per streaming interface | Operator-scoped. Simplest descriptor and buffering; proven by the spike |
| D5 | Composite device: UAC2 audio function + USB MIDI function, grouped by IAD — *amended 2026-08-23: a third function, **CDC serial**, is added; see **D27*** | Needed for D2 without a second cable or extra hardware |
| D6 | Clock: HSE bypass on the ST-Link 8 MHz MCO, PLL M=4 N=168 P=2 Q=7 → 168 MHz SYSCLK, **exactly 48 MHz** on PLLQ | 180 MHz is the part maximum but 360/48 is not integral and would force USB onto PLLSAI |
| D7 | PLL-lock failure is **fatal**, never a silent fall back to HSI | USB cannot work on the HSI's ±1%; a silent fallback yields a device that enumerates erratically |
| D8 | Dependencies via **CPM** with `cpm-package-lock.cmake` entries: TinyUSB pinned **0.21.0**, `cmsis_device_f4`, CMSIS core | Matches how acfx pins everything else. The spike used submodules; that is not this repo's convention |
| D9 | Do **not** track TinyUSB `master` | Its `uac2_headset` example references `TUD_AUDIO_HEADSET_STEREO_DESC_LEN`, defined only in a *different* example's header — broken on master and on 0.21.0 alike |
| D10 | Write the UAC2 descriptor locally from the `TUD_AUDIO20_DESC_*` primitives | Consequence of D9; also required for stereo-in/stereo-out, which no shipped template provides |
| D11 | `cmake/toolchains/nucleo-f446.cmake` + an `acfx_add_effect_nucleo` factory | Mirrors `daisy.cmake` and `acfx_add_effect_daisy`. Cortex-M4, `fpv4-sp-d16`, hard float, `-fno-exceptions -fno-rtti` |
| D12 | Reuse the daisy toolchain's **libstdc++ probe** | It already fails loud on Homebrew's C-only `arm-none-eabi-gcc`; the spike hit exactly that trap |
| D13 | Generate the interrupt vector table from the CMSIS `IRQn_Type` enum | `OTG_FS_IRQn` is 67; a core-exceptions-only table sends the NVIC past the end of the array on the first USB interrupt |
| D14 | The adapter **owns `SystemCoreClock`** | ST's `system_stm32f4xx.c` is not compiled. TinyUSB derives the USB PHY turnaround time from it, so a wrong value degrades timing silently rather than failing |
| D15 | ~~Effect prepared with `maxBlockSize` = **49 frames**; the nominal block is 48~~ — **SUPERSEDED 2026-08-23 by D28** (prepare at **48**) | The rationale held only while the block followed the packet. **D30**'s ring decoupled them, and 49 outlived its own premise. `TUD_AUDIO_EP_SIZE`'s 49 frames remains correct for the *transport-side packet buffer*, which is what it always actually described |
| D16 | Statically sized ring buffer; **no heap and no locks** in the audio path | acfx real-time-safety principle; enforced by the existing no-allocation test discipline |
| D17 | VBUS is **not wired**; `CFG_TUD_VBUS_DETECT_HW 0` forces session-valid | The board is ST-Link powered; feeding breakout VBUS into the 5 V rail puts two supplies in contention |
| D18 | Three verification layers: CI cross-compile+link, host doctest over `acfx_nucleo_support`, and a hardware-in-the-loop harness | Operator chose all three. The HIL harness cannot run in normal CI |
| D19 | One firmware binary per effect, via the factory | Matches `acfx_add_effect_daisy`, which builds `acfx_daisy` and `acfx_daisy_delay` separately |
| D20 | **USB SOF is the only clock.** OUT is an adaptive sink, IN is an asynchronous source, and there is **no feedback endpoint — explicitly absent by design** | The device has no independent audio clock. A feedback endpoint reports the rate a device consumes at; a device that consumes exactly what the host sends has nothing to report |
| D21 | Packet payload size is **not fixed**. Nominal 48 frames; the endpoint is sized for 49; the implementation accepts 0–49 frames per packet, including short and zero-length packets | `TUD_AUDIO_EP_SIZE` computes `((48000+999)/1000 + 1) * 2 * 2` = 196 bytes = 49 frames. A 48-frame assumption is contradicted by the stack's own descriptor arithmetic |
| D22 | The host may open the **capture stream alone** (mic `alt=1`, speaker `alt=0`). In that state the IN endpoint emits **silence**, counted via `inputStarved` | Legal host behaviour that a topology deriving IN from OUT cannot otherwise answer. Undefined behaviour here would surface as a mysterious hang or stale audio |
| D23 | Ring-buffer **semantics** are fixed here; **capacity, water marks and startup fill are not** — those are derived from HIL measurement and pinned in the spec | Pre-measurement numbers would be invented. The spike's ~0.2% figure was measured under a naive single buffer and does not predict the tuned design |
| D24 | Underflow emits **silence**, overflow **drops the oldest**, and both increment a counter. Never silent | acfx's "no fallbacks, raise descriptive errors" cannot be applied literally in an audio path, which must emit something in bounded time and cannot throw. The correct reading: the substitution is defined and observable, not absent |
| D25 | The parameter seam is a **per-`ParamId` shadow block with dirty flags**, not a FIFO of change events | `setParameter(id, normalized)` is idempotent state, not an event. Bounded by construction at `parameters().size()`, structurally cannot overflow, and immune to the cross-parameter starvation a shared FIFO allows |
| D26 | Single execution context: TinyUSB class servicing and DSP both run in the main loop; the ISR only enqueues | Verified in TinyUSB 0.21.0 — `driver->xfer_cb` is dispatched from `tud_task_ext` via `osal_queue_receive`, while `dcd_event_handler` only queues. No lock-free discipline is needed today |

### Transport contract

Restating D20–D22 as the runtime contract the support library is written and
tested against:

- The host's SOF is the sample clock. The device never asserts a rate.
- **OUT (host → device)** is an adaptive sink. Whatever arrives is consumed.
- **IN (device → host)** is an asynchronous source, produced one host-paced frame
  per SOF.
- **No feedback endpoint.** Not an omission — with no local clock there is no
  rate to feed back.
- Packet payloads carry 0–49 stereo frames. Code that assumes 48 is wrong.
- Capture-only operation is legal and yields counted silence (D22).
- *(Amended 2026-08-23)* The DSP does **not** consume packets directly — it draws fixed
  48-frame blocks from the ring (**D28**), which is what stops packet framing from
  reaching the effect at all.
- *(Amended 2026-08-23)* A payload that is not a whole number of frames is truncated to
  whole frames and the truncation counted (**D32**).
- *(Amended 2026-08-23)* Suspend, resume and bus reset are part of this contract, not
  exceptional cleanup (**D35**).

### Buffer and error accounting

```cpp
struct AudioTransportStats {
    uint32_t inputUnderruns;    // DSP wanted a block, ring was short
    uint32_t inputOverruns;     // USB filled faster than DSP drained
    uint32_t outputUnderruns;   // USB polled IN, ring was empty
    uint32_t outputOverruns;    // DSP produced faster than USB drained
    uint32_t inputStarved;      // capture-only: silence emitted (D22)
    uint32_t malformedPayloads; // torn payload truncated (D32) -- an EVENT count
    uint32_t blocksProcessed;   // denominator -- lets counters become a rate
    uint32_t worstBlockMicros;  // makes the CPU budget directly observable
};
```

*(Amended 2026-08-23)* `malformedPayloads` is the eighth field (**D32**). It counts
**payloads truncated**, not frames or bytes discarded: a stereo 16-bit frame is 4 bytes,
so a torn remainder is always 1–3 bytes and never a whole frame — a counter named for
discarded *frames* would have read zero forever, which is worse than no counter because
it would have been believed. Counter wrap, mutual exclusivity and the meaning of the
denominator are pinned in **D31**.

`blocksProcessed` and `worstBlockMicros` are additions beyond a plain error
count: without a denominator the counters cannot be turned into a quality bar,
and without a worst-case block time the CPU budget can only be inferred from
dropout symptoms after the fact. The HIL harness asserts against these directly
rather than inferring glitches from signal correlation.

### Parameter seam

Both sources converge on data, not on an execution model:

```
ADC / encoder (sampled state)  ─┐
                                ├→ per-ParamId shadow block + dirty flags
USB MIDI (asynchronous events) ─┘        → effect.setParameter(...) once per block
```

A physical control polls its ADC and writes its slot when the value moves past a
dead-band; USB MIDI writes its slot when a CC arrives. Once per audio block the
adapter walks the dirty flags and calls `setParameter` for each. Last-value-wins
is not a lossy compromise — it is the semantically correct operation for a
state-valued parameter.

*(Amended 2026-08-23)* The box on the left of that diagram is now a named seam rather
than a shape: **D29** makes `ParameterSource` a duck-typed `poll(shadow)` contract, and
makes the dead-band a **requirement** rather than an implementation habit — without it a
sampled source dirties every slot every block and the flush degenerates into applying
every parameter at audio rate, which is the exact waste the dirty flags exist to avoid.

This is deliberately **not** a bounded FIFO of change events. Any drop policy on
a shared queue is lossy in a way that matters: drop-newest can strand a parameter
at an intermediate value after a knob sweep, and drop-oldest lets one fast-moving
control evict another control's single pending update. Per-`ParamId` slots make
both failure modes structurally impossible.

The limitation, stated plainly: this is wrong for event-valued controls (a
momentary trigger, tap tempo). acfx's parameter model has none — normalized
continuous values only — and such a control would need its own mechanism
regardless.

## Open questions

Third-party review resolved two of the original eleven into decisions: the
isochronous synchronization model became D20–D22, and the `ParameterSource`
contract became D25. What remains — **all eleven are still open after the 2026-08-23
amendment**; D27–D36 answered questions that were never on this list, and only
question 6 narrowed (its readback channel is now CDC per D27, its *home and
invocation* remain open):

1. **Ring-buffer capacity, water marks and startup fill.** Deliberately not
   pinned here (D23) — these come from HIL measurement and land in the spec with
   measured justification. Inventing them pre-harness would be false precision.
2. **The acceptable glitch bar.** Now directly measurable via
   `AudioTransportStats` rather than inferred from signal correlation, so the
   question narrows to: what counter rate constitutes a failing build, and which
   verification layer enforces it? Note the spike's ~0.2% was a naive
   single-buffer figure and is not a prediction.
3. **When physical peripherals arrive, does D26 still hold?** The single-context
   assumption is what lets the shadow block skip lock-free discipline. Sampling
   ADCs from a timer ISR would break it and require revisiting D25's memory
   ordering. That is the explicit trigger to reopen this — not a reason to
   pre-pay for it now.
4. **Reusable transport seam.** When a second STM32 board appears, does the
   UAC2/TinyUSB transport extract cleanly out of `adapters/nucleo/`? Revisit at
   board two rather than guessing now.
5. **Additional audio formats.** 24-bit, 44.1 kHz and 96 kHz all fit inside
   full-speed bandwidth for stereo. The operator scoped the advertised matrix to
   48/16; 44.1 in particular forces variable packet sizes and is more work than it
   appears.
6. **Where the HIL harness lives and how it is invoked.** It needs a physical
   board, so it cannot be a normal CI job. In-repo with a manual target? A
   dedicated runner? The spike's `tools/loopback_test.py` is a starting point.
7. **MIDI CC → `ParamId` mapping convention.** Fixed CC numbers per index? A
   learn mode? Which channel? Does this want to match how the workbench already
   consumes MIDI CC, so one mapping serves both?
8. **Which effects get Nucleo firmwares**, and what the CPU budget at 168 MHz
   allows — `worstBlockMicros` now makes this measurable rather than speculative,
   and it is a live question for the convolution and physical-modeling phases.
9. **Two cables is awkward.** The ST-Link cable is mandatory only because it
   supplies the clock. Is a single-cable arrangement (populate X3, or an external
   oscillator) wanted later?
10. **Should `daisy` and `teensy` get roadmap nodes retrofitted?** They are
    currently ungoverned, having arrived under Milestone 1. Considered and
    deferred by the operator when the parent node was created.
11. **Denormal handling.** Not addressed by any existing adapter;
    `TASK-1 svf-no-denormal-flush` is already open in the backlog and applies to
    an MCU target with an FPU just as much as to the desktop ones.

## Decisions added 2026-08-23

Numbered from D27 so D1–D26 keep the identities the spec's 79 requirements cite.

| # | Decision | Rationale |
|---|---|---|
| D27 | The composite device carries a **third function: CDC serial**, IAD-grouped alongside UAC2 and MIDI, and **every** effect firmware carries it | D5 gave the HIL harness no channel to read `AudioTransportStats` over. CDC is driverless on macOS/Linux/Windows 10+, and TinyUSB's `cdc_uac2` example — already the descriptor template here — carries it. Rejected: MIDI SysEx (mixes telemetry into the control path, fiddly framing), a vendor control request (risks a Windows driver prompt, defeating the driverless proposition), debugger-only (puts a probe in every HIL run). One device shape, not two, so telemetry is never a property a developer has to remember |
| D28 | The DSP processes **fixed 48-frame blocks** drawn from the ring, and the effect is prepared with `maxBlockSize` = **48**. **Supersedes D15** | Once the ring is the decoupling boundary, a 49-frame packet changes ring occupancy and nothing else — `process()` still receives 48, so 48 *is* the maximum block. Preparing at 49 would let transport framing leak across the boundary that exists to stop it. D15's premise was retired by this decision; its number outlived its reasoning |
| D29 | `ParameterSource` is a **duck-typed** `void poll(ParameterShadow<N>&) noexcept` seam — not a base class — with `MidiParameterSource` as the first implementation. Sources MUST **dead-band** their writes | Matches how acfx already expresses seams (`Effect`, `CompanionSupply`). One seam serves a sampled-state source and an event source because both converge on *data*, which is what makes D3's peripherals a later addition rather than a later redesign. The dead-band is load-bearing, not a nicety: without it a sampled source dirties every slot every block |
| D30 | The ring has three states — **Stopped / Priming / Running**. The consumer waits for the startup fill before its first block, and underruns count **only in Running**. The ring never **re-centres** | Without the states, "ring holds a partial block" is simultaneously normal (priming) and an underrun (running), and two conforming implementations disagree about whether every stream open emits a burst of underruns. No re-centring because both directions are paced by the same SOF clock: persistent drift is a real fault that belongs in the counters, not something to mask with frame drops that are themselves audible and uncounted |
| D31 | Counters **wrap** modulo 2^32 (consumers take deltas), are **mutually exclusive** (one event, one counter), are **not resettable** at runtime, and survive suspend/reset. `blocksProcessed` is a **normalization denominator**, not a per-event opportunity count | `blocksProcessed` wraps after ~49 days of continuous streaming; saturating would permanently break every derived rate, while wrapping plus deltas is what a rate-over-an-interval needs anyway. Exclusivity matters because `inputStarved` and `outputUnderruns` both described capture-only silence — a counter whose meaning is ambiguous is worse than no counter, because it will be believed. And `inputOverruns` originates in packet writes, not DSP blocks, so the denominator normalizes health rather than proportioning failures |
| D32 | A payload that is **not a whole number of frames** is truncated to whole frames; the remainder is discarded and counted as `malformedPayloads`, an **event** count. `AudioTransportStats` therefore has **eight** fields | Truncation preserves L/R alignment downstream — discarding 47 good frames because the 48th was torn is the worse trade, and misaligned stereo is a distinctive, hard-to-diagnose symptom. The count must be of events, not frames: a torn remainder is 1–3 bytes and never a whole frame |
| D33 | int16↔float conversion scales by **32768** both directions, rounds to nearest with **ties away from zero**, and **clamps** to [-32768, 32767] | 32768 is an exact power of two, so the round trip is lossless. The clamp is load-bearing: an effect overshooting 1.0 would otherwise wrap to the opposite rail and emit loud broadband noise — the silent, uncounted degradation D24 exists to prevent. The spike's 0.999916 gain was CoreAudio's conversion, not the firmware's, and is not a target |
| D34 | A fatal clock failure blinks **LD2 (PA5): three short pulses, long gap, repeating**, then halts. The LED GPIO is initialized **before** clock validation | Without a locked PLL, USB cannot enumerate, so no USB channel — CDC included — can carry the fault; the single LED is the only signal that needs no debug probe. Quantifying the pattern is what makes "distinguishable from an unpowered board" checkable by eye instead of a matter of opinion. The LED therefore runs on the reset-default HSI and its cadence is approximate — acceptable, because the pattern's shape carries the signal |
| D35 | **Suspend** clears the rings and enters Stopped; **resume** enters Priming; **bus reset** clears and enters Priming. Counters survive all three | Clearing at suspend is what makes "no stale audio on resume" *structural* rather than a second mechanism bolted on at resume. Suspend and bus reset are routine host behaviour and the events a long HIL soak will actually hit — left undefined they surface as a mystery hang after a laptop sleeps |
| D36 | Statistics **updating** (audio path) is separated from statistics **reporting** (a main-loop diagnostic service that snapshots, serializes and writes to CDC). The write is still **non-blocking and allocation-free**, and drops rather than queues when the port is unread | Requiring a CDC write to be real-time-safe solves the wrong problem — it is not audio work. But D26 gives this firmware a **single execution context**: the main loop runs class servicing, the DSP *and* this service, so nothing absorbs a stall. Relocating the work out of the audio path removes it from `worstBlockMicros`; it does not relax its bounds |

## Provenance

**Experimental spike, `~/work/stm32` (a separate repository, not part of acfx).**
An exploratory bring-up conducted before this design, explicitly treated as a
spike. It is not proposed for import; its value is the findings below, each of
which is hardware-verified rather than assumed:

- **PA11/PA12 wiring confirmed electrically.** With internal pull-ups enabled,
  `GPIOA_IDR` moved `0x98e4 → 0x80e4` when the breakout was connected — a delta
  of exactly bits 11 and 12. Note the board carries two unrelated numbering
  schemes: the Arduino pins labelled `D11`/`D12` are **PA7/PA6**, not PA11/PA12.
- **The clock arrangement works.** `HSERDY=1`, `PLLRDY=1`, `RCC_CFGR.SWS=PLL`,
  `RCC_PLLCFGR=0x07402a04`, USB at exactly 48.0000 MHz, cross-checked against
  wall-clock time via SysTick. This closes the unpopulated-crystal risk.
- **A class-compliant UAC2 device enumerates on macOS with no driver** and
  streams both directions; PortAudio opens it duplex as 2-in/2-out at 48 kHz.
- **TinyUSB 0.21.0 removed the `rx_done`/`tx_done` data-path callbacks.** Code
  written against them links out *silently* — the symbols simply vanish and the
  audio path is dead with no diagnostic. The data path is polled via
  `tud_audio_read()` / `tud_audio_write()`.
- **Homebrew's `arm-none-eabi-gcc` ships without newlib.** Already documented in
  `cmake/toolchains/daisy.cmake`, which fails loud on exactly this; the spike hit
  it independently, which is corroboration that the probe earns its place.
- **Measured loopback quality**: correlation 0.998, 99.788% of samples within
  4 LSB, and a reproducible 0.999916 gain that is CoreAudio's int16↔float32
  conversion rather than anything in the firmware. Feeds open question 4.

**In-repo references**

- `adapters/daisy/daisy-main.cpp` — the adapter contract exemplar.
- `cmake/acfx-effect-targets.cmake` — `acfx_add_effect_daisy`, the factory shape
  an `acfx_add_effect_nucleo` should mirror (~18 lines).
- `cmake/toolchains/daisy.cmake` — the toolchain template, including the
  libstdc++ probe.
- `examples/device/cdc_uac2/src/usb_descriptors.h` in TinyUSB — the descriptor
  template to adapt from (mono-mic/stereo-out) to stereo/stereo.

**Operator decisions recorded in this session**

- Roadmap placement: a new `multi:feature/hardware-targets` parent rather than
  filing under `phase-reference-hardware`, which is about analog gear to emulate.
- Charter: NUCLEO-F446RE with USB audio I/O.
- Control: USB MIDI first, physical peripherals to follow.
- Verification: all three layers.
- Formats: 48 kHz / 16-bit stereo only.
- Decomposition: two layers.

**Also found, unrelated**: `TASK-20` — `stackctl curate --doc ROADMAP.md` fails
on clean `main` because terminal `phase-digital-fundamentals` is still referenced
via `part-of` by `svf-vertical-slice`. Captured to the backlog, not fixed here.
