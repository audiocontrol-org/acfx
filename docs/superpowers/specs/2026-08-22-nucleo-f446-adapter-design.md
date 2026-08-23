# design:gap/nucleo-f446-adapter — NUCLEO-F446RE hardware layer with USB audio I/O

Roadmap item: `design:gap/nucleo-f446-adapter`
Parent: `multi:feature/hardware-targets` → `multi:feature/progressive-dsp-platform`
House rules: `stack-control-design-v1` (capture over YAGNI — nothing below is
scoped out; scoping is a separate operator pass)

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
  mapping. Compiles on the host and is covered by doctest.

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
| D3 | Physical peripheral support (pots/encoders/buttons) is a **known-implied requirement**, shaping the `ParameterSource` contract now | Operator stated it explicitly; captured rather than deferred out of the record |
| D4 | Advertise **48 kHz / 16-bit stereo only**, one alt-setting per streaming interface | Operator-scoped. Simplest descriptor and buffering; proven by the spike |
| D5 | Composite device: UAC2 audio function + USB MIDI function, grouped by IAD | Needed for D2 without a second cable or extra hardware |
| D6 | Clock: HSE bypass on the ST-Link 8 MHz MCO, PLL M=4 N=168 P=2 Q=7 → 168 MHz SYSCLK, **exactly 48 MHz** on PLLQ | 180 MHz is the part maximum but 360/48 is not integral and would force USB onto PLLSAI |
| D7 | PLL-lock failure is **fatal**, never a silent fall back to HSI | USB cannot work on the HSI's ±1%; a silent fallback yields a device that enumerates erratically |
| D8 | Dependencies via **CPM** with `cpm-package-lock.cmake` entries: TinyUSB pinned **0.21.0**, `cmsis_device_f4`, CMSIS core | Matches how acfx pins everything else. The spike used submodules; that is not this repo's convention |
| D9 | Do **not** track TinyUSB `master` | Its `uac2_headset` example references `TUD_AUDIO_HEADSET_STEREO_DESC_LEN`, defined only in a *different* example's header — broken on master and on 0.21.0 alike |
| D10 | Write the UAC2 descriptor locally from the `TUD_AUDIO20_DESC_*` primitives | Consequence of D9; also required for stereo-in/stereo-out, which no shipped template provides |
| D11 | `cmake/toolchains/nucleo-f446.cmake` + an `acfx_add_effect_nucleo` factory | Mirrors `daisy.cmake` and `acfx_add_effect_daisy`. Cortex-M4, `fpv4-sp-d16`, hard float, `-fno-exceptions -fno-rtti` |
| D12 | Reuse the daisy toolchain's **libstdc++ probe** | It already fails loud on Homebrew's C-only `arm-none-eabi-gcc`; the spike hit exactly that trap |
| D13 | Generate the interrupt vector table from the CMSIS `IRQn_Type` enum | `OTG_FS_IRQn` is 67; a core-exceptions-only table sends the NVIC past the end of the array on the first USB interrupt |
| D14 | The adapter **owns `SystemCoreClock`** | ST's `system_stm32f4xx.c` is not compiled. TinyUSB derives the USB PHY turnaround time from it, so a wrong value degrades timing silently rather than failing |
| D15 | Effect prepared with `maxBlockSize` = **49 frames**; the nominal block is 48 | 48 is the nominal rate, NOT a guarantee — see D20. `TUD_AUDIO_EP_SIZE` sizes the endpoint at 49 frames by design |
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

### Buffer and error accounting

```cpp
struct AudioTransportStats {
    uint32_t inputUnderruns;    // DSP wanted a block, ring was short
    uint32_t inputOverruns;     // USB filled faster than DSP drained
    uint32_t outputUnderruns;   // USB polled IN, ring was empty
    uint32_t outputOverruns;    // DSP produced faster than USB drained
    uint32_t inputStarved;      // capture-only: silence emitted (D22)
    uint32_t blocksProcessed;   // denominator -- lets counters become a rate
    uint32_t worstBlockMicros;  // makes the CPU budget directly observable
};
```

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
contract became D25. What remains:

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
