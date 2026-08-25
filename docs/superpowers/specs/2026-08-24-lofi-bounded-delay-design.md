# Bounded lo-fi delay — design record

- Relates to: `core/effects/modulated-delay/` (the existing effect), `adapters/nucleo/`
  (the target that cannot run it today), and the future `design:feature/reverb-engines`
  roadmap node (the reusable lo-fi pieces are meant to serve it).
- Date: 2026-08-24
- Backlog origin: **TASK-34** (`nucleo-delay-firmware-heap-overshoot`), hardware-confirmed
  this session — `acfx_nucleo_delay` does not enumerate on the F446.
- Rate approach (operator-selected): **internal decimation, effect-local** — the USB device
  stays 48 kHz / 16-bit; each effect degrades its own processing.
- Controls (operator-selected): **live over MIDI CC** (the working US6 parameter path).
- Fidelity/length coupling (operator-selected): **Approach A — decimation trades bandwidth
  for delay time** (a fixed buffer holds decimated-rate samples, so lower fidelity ⇒ longer,
  darker echoes from the same memory).
- Delay-line storage (operator-selected): **int16**.

## Problem domain

The shipped `ModulatedDelayEffect` builds and links for the Nucleo (`acfx_nucleo_delay.elf`)
but **cannot boot on the STM32F446RE**. `prepare()` computes a 2.0-second delay line —
`capacity = sampleRate * 2.0f + 2 = 96002` floats per channel
(`modulated-delay-effect.h:152`) — and heap-allocates it with `std::vector::assign`
(`:155`), i.e. **~750 KB** across the stereo context against the part's **128 KB** of SRAM.
The image aborts in `PrepareEffect()` under `-fno-exceptions` before `tud_task()`, so USB
never enumerates. Confirmed live 2026-08-24: board alive over SWD, no `acfx Audio` device,
no acfx CDC (research `§R15`). The wow/flutter stage (`:172`) also heap-allocates — any
remaining `new` on this target has the same effect.

Two goals, one mechanism:

1. **Make a delay that runs on the device.** Bound the memory and remove all heap use.
2. **Experiment with lo-fi digital audio.** The operator wants to *hear* memory-hog effects
   (delay now, reverb later) at reduced sample rate and bit depth. Because the buffer is
   fixed, the same lever that controls the lo-fi character also controls how much delay time
   fits — the aesthetic and the memory budget are the same knob.

### Constraints carried from the codebase

- **Platform-independent core.** The effect lives in `core/` and must acquire no board/USB
  dependency — now enforced by the `C-CORE-INWARD` portability gate (T067). The per-target
  memory bound therefore cannot come from a Nucleo `#define`; it must enter through the type
  system.
- **Real-time safety.** No heap, no locks in `process()` or `prepare()` on the RT path. The
  T066 allocation sentinel is the mechanical check.
- **Fixed device format (D4).** 48 kHz / 16-bit stereo, one advertised format. Lo-fi is
  strictly *internal* to the effect; the transport is untouched.
- **Live parameters already work (US6).** MIDI CC → parameter index, omni, `value/127`
  (`midi-cc-map.h`); extending the map is a one-line table edit (T043).
- **Small modules (~300–500 lines).** New DSP goes in focused units.

## Solution space

### Chosen — templated static buffer, int16 storage, internal sample-and-hold decimation and bit-crush as live CC params

**Bounding foundation (all platforms).** Template `ModulatedDelayEffect` on a compile-time
maximum-delay-samples bound (non-type template parameter, default = today's 2.0 s so desktop
and Daisy are byte-for-byte unchanged). Replace the heap `std::vector` delay buffers — and
the wow/flutter scratch — with in-object `std::array`, making the whole effect heap-free.
The Nucleo firmware target instantiates a small variant through `ACFX_EFFECT_TYPE`. The bound
is a plain `int` template argument, so `core/` stays board-agnostic.

**Storage (int16).** The delay line stores `int16_t` samples. Half the memory of float ⇒ ~2×
the delay time per KB; 16-bit is transparent at the clean end; the device already speaks
int16. Conversion happens at the read/write boundary of the delay line.

**Decimation — bandwidth for time (Approach A).** The delay line stores samples at the
*internal* rate `48000 / D`, `D ∈ {1, 2, 4, 8}`. Maximum delay time is therefore
`N · D / 48000` for a per-channel buffer of `N` samples — **lower fidelity yields a longer
echo from the same memory**. Decimation is naive **sample-and-hold** on the way in and on the
way out: the aliasing and stairstep reconstruction *are* the lo-fi character and are wanted,
not filtered away.

**Bit-crush (live).** A quantizer in the feedback loop reduces each sample to `B ∈ {16…4}`
effective bits. `B = 16` is transparent; lower `B` accumulates regeneratively through the
feedback path, so the grit intensifies the longer a repeat rings.

**Sizing.** Start the clean end (`D = 1`) at **~300 ms stereo**: `N = 14400` samples/ch,
`14400 × 2 bytes × 2 ch ≈ 58 KB` — comfortably inside the ~80–100 KB of SRAM left after the
adapter's ~24 KB bss, the rings, the stack, and the (separate) OTG-FS FIFO RAM. The same
buffer then gives longer echoes as fidelity drops:

| Internal rate `D` | Internal rate | Max delay (300 ms buffer) | Character            |
|-------------------|---------------|---------------------------|----------------------|
| 1                 | 48 kHz        | ~300 ms                   | clean                |
| 2                 | 24 kHz        | ~600 ms                   | slightly dark        |
| 4                 | 12 kHz        | ~1.2 s                    | telephone / gritty   |
| 8                 | 6 kHz         | ~2.4 s                    | dark, aliased, long  |

Sizing is a measurement decision (as in T062): confirm the fit and headroom on the board with
the T058/T059 HIL rig, and push the clean end to ~400 ms if there is room.

**Live params on the US6 path.** Add two parameters — `lofi_rate` (discrete 1/2/4/8) and
`lofi_bits` (16→4) — to the effect's parameter list, and extend `midi-cc-map.h` so two CCs
bind to their indices. All existing character (LFO-modulated delay time, wow/flutter, the
mode filter) is retained; the lo-fi controls are additive.

**Reuse.** The decimator and the bit-crush are written as small, independently testable units
under `core/primitives/lofi/` so the future reverb composes the exact same pieces rather than
re-deriving them. The bounded-static-buffer pattern generalizes the same way.

### Rejected — lower the whole USB device rate

Re-advertising the device at 24 k/12 k would make the entire pipeline lo-fi, but it rewrites
the USB descriptors and the fixed 48/16 format matrix (D4), is global rather than per-effect,
and is far more invasive than the goal warrants. The operator selected effect-local
decimation.

### Rejected — rate as pure tone control, fixed maximum delay time (Approach B)

Sizing the buffer for the worst case (`D = 1`) and letting a lower rate only darken/alias is
simpler and keeps the delay-time knob's range stable, but it forfeits the "cheap long echoes"
trick that makes the experiment interesting and buys no memory back. The operator chose A.

### Rejected — separate Nucleo-only lo-fi effect, existing delay merely bounded

Cleanest separation (the clean delay stays clean everywhere), but it ships two delay effects
and shares no code. The operator wants the lo-fi capability on the existing effect.

### Rejected — float32 storage

Cleanest clean-end and no int/float round-trip in the loop, but half the delay time per KB
and the storage itself never contributes lo-fi. Superseded by int16.

### Rejected — int8 storage

Quarters the memory (longest echoes), but 8-bit is already crunchy with no truly-clean
setting. Kept as a possible future build variant, not the default.

### Rejected — compile-time lo-fi variants (flash-to-compare)

Simple, but switching means reflashing, and reflashing degrades host CoreAudio (observed this
session). The operator wants live control so the range is sweepable in real time.

## Decisions

1. **Bounding via a compile-time template parameter**, default 2.0 s; Nucleo instantiates a
   ~300 ms variant. Core acquires no board dependency.
2. **The whole effect becomes heap-free** — delay buffers and wow/flutter scratch both move to
   in-object `std::array`. Verified by the T066 allocation sentinel.
3. **int16 delay-line storage.**
4. **Internal decimation, `D ∈ {1,2,4,8}`, sample-and-hold both directions**, aliasing kept.
5. **Approach A**: buffer holds decimated-rate samples ⇒ max delay `= N·D/48000`; lower
   fidelity ⇒ longer, darker echo.
6. **Live bit-crush, `B ∈ {16…4}`**, applied in the feedback loop.
7. **Two new live parameters** (`lofi_rate`, `lofi_bits`) on the US6 MIDI-CC path.
8. **Decimator and bit-crush as reusable isolated primitives** under `core/primitives/lofi/`.
9. **Host TDD first, then hardware acceptance** — flash, confirm enumeration (the fix for
   TASK-34), sweep CC to audition, and capture `worstBlockMicros` + counters over the
   T058/T059 HIL rig.

### Captured but scoping-deferred (capture-over-YAGNI)

- **Reverb.** The reason the lo-fi pieces are being factored into primitives. Building the
  reverb itself is a separate roadmap item (`design:feature/reverb-engines`) and not in this
  work.
- **int8 build variant** for extreme-length / smallest-footprint delays — a later option.
- **~400 ms clean-end** — adopt if board measurement shows the headroom.
- **Anti-aliased decimation** (a real low-pass before decimation) — deliberately *not* done;
  a future "clean divisor" mode could add it, but it fights the aesthetic here.

## Open questions

1. **Delay-time-range coupling.** Under Approach A the delay-time knob's maximum shifts with
   `lofi_rate`. Do we (a) express delay time in seconds and clamp to the current max, or
   (b) express it as a fraction of the current max? (a) is the leaning default; to be settled
   in implementation.
2. **CC assignments.** Which specific CC numbers bind to `lofi_rate` and `lofi_bits`, relative
   to the existing workbench convention (CC 74 → index 0, CC 71 → index 1).
3. **CPU headroom.** Decimation interacting with the existing delay-time modulation and mode
   filter is the trickiest DSP; confirm `worstBlockMicros` stays well inside the 1000 µs/block
   budget (SVF baseline was 65 µs).
4. **Build path.** Whether to author this as a formal acfx Spec Kit spec via
   `/stack-control:define` or implement it directly with host TDD, given it is a focused
   effect change already brainstormed.

## Provenance

Brainstormed 2026-08-24 with the operator after the Phase-12 (US9) hardware verification of
the nucleo-f446-adapter feature surfaced that neither a reverb nor the existing delay runs on
the F446. Operator selections captured inline above: effect-local decimation; live MIDI-CC
control; Approach A (bandwidth-for-time); int16 storage. The bounding half resolves backlog
TASK-34; the lo-fi half seeds reusable primitives for the future reverb.
