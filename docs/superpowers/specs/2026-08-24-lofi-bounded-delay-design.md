# Bounded lo-fi delay — design record

- Relates to: `core/effects/modulated-delay/` (the existing effect), `adapters/nucleo/`
  (the target that cannot run it today), and the future `design:feature/reverb-engines`
  roadmap node (the reusable lo-fi pieces are meant to serve it).
- Date: 2026-08-24 (revised same day after a third-party design review — see
  **Third-party review resolutions**).
- Backlog origin: **TASK-34** (`nucleo-delay-firmware-heap-overshoot`), hardware-confirmed
  this session — `acfx_nucleo_delay` does not enumerate on the F446.
- Rate approach (operator-selected): **internal decimation, effect-local**.
- Controls (operator-selected): **live over MIDI CC** (the US6 parameter path).
- Fidelity/length coupling (operator-selected): **Approach A — decimation trades bandwidth
  for delay time**.
- Storage (operator-selected): **template policy — float32 default (desktop/Daisy), int16 on
  Nucleo**.
- Live rate-change semantics (operator-selected): **reinterpret buffer at the new rate
  (tape-speed jump)** — a defined, tested feature.
- Decimation scope (operator-selected): **the whole wet loop runs at the internal rate**.

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
  dependency — enforced by the `C-CORE-INWARD` portability gate (T067). The per-target memory
  bound therefore cannot come from a Nucleo `#define`; it enters through the type system.
- **Real-time safety.** No heap, no locks in `process()`/`prepare()`. Verified by the T066
  allocation sentinel.
- **Fixed device format (D4).** 48 kHz / 16-bit stereo, one advertised format. Lo-fi is
  strictly *internal* to the effect; the transport is untouched.
- **Live parameters already work (US6).** MIDI CC → parameter index, omni, `value/127`
  (`midi-cc-map.h`); extending the map is a one-line table edit (T043).
- **Existing conversion + delay primitives to reuse.** `interleaveToInt16` already implements
  the int16 convention (scale ×32768, round ties-away-from-zero, clamp `[-32768, 32767]`,
  FR-038a); the delay-line abstraction lives at `core/primitives/delays/delay-line.h`.
- **Static ≠ free on an MCU.** Moving from a heap `std::vector` to an in-object `std::array`
  makes the object large and linker-placed. A `std::array` also reserves *every* channel slot
  (today's vector allocates only `numChannels` at runtime), so the channel count must be
  bounded too — otherwise `kMaxChannels(8) × 14400 × int16 = 230 KB` overflows SRAM.

## Solution space

### Chosen — templated static buffer (size, storage type, channels), whole-wet-loop internal decimation, int16 quantize + bit-crush as live CC params

**Bounding foundation (all platforms).** Template the effect on three compile-time policy
parameters and replace the heap `std::vector` delay buffers — and the wow/flutter scratch —
with in-object `std::array`, making the whole effect heap-free:

```cpp
template <std::size_t MaxDelaySamples,
          typename    Sample     = float,   // desktop/Daisy keep float32
          std::size_t MaxChannels = 8>
class ModulatedDelayEffect;
```

- **Desktop / Daisy:** `Sample = float`, `MaxChannels = 8`, `MaxDelaySamples = 96000`
  (2 s at 48 kHz). *Intended* to preserve today's numerical behavior at the clean settings
  (`D=1`, `B=16`) — same float math, storage moved heap→static (Daisy must link the object
  into SDRAM, per the memory contract below). This is an **acceptance property, verified by a
  test** (§Decisions 12), not assumed; the float path's `B=16` crush bypass (below) is the
  mechanism that makes it hold.
- **Nucleo:** `ModulatedDelayEffect<14400, std::int16_t, 2>` ≈ **57.6 KB** static.
- The bound is a sample count, so the achievable maximum is `MaxDelaySamples / sampleRate`
  seconds — 2 s at 48 kHz, 1 s at 96 kHz. No universal "2 seconds" is claimed.
- Core stays board-agnostic: the parameters are plain `std::size_t`/type args.

**Named target policy (not opaque CMake).** The adapter declares a readable alias and points
`ACFX_EFFECT_TYPE` at it, so the memory policy is discoverable in code, not buried in CMake
quoting:

```cpp
// adapters/nucleo/…  (board-side, not core)
using NucleoModulatedDelay = acfx::ModulatedDelayEffect<14400, std::int16_t, 2>;
```

**Decimation — bandwidth for time (Approach A), whole wet loop at the internal rate.** The
delay line stores samples at the *internal* rate `48000 / D`, `D ∈ {1,2,4,8}`, so maximum
delay time is `MaxDelaySamples · D / 48000` — **lower fidelity yields a longer echo from the
same memory**. The processing clock is explicit:

```
48 kHz outer clock, per output sample:
    dry = input sample                              (full rate)
    if this is an internal tick (every D-th sample):
        in_i   = input sample                       (sample-and-hold decimation in)
        d      = delayline.read(readPosInternal)    (internal rate; wow/flutter + LFO
                                                      modulate readPos, computed in
                                                      internal samples)
        d      = modeFilter(d)                       (internal rate — inside the loop)
        w      = in_i + feedback * d
        code   = int16_quantize(w)                   (see quantization contract)
        code   = bitcrush(code, B)                   (B effective bits)
        delayline.write(code); delayline.advance()
        held   = to_float(code_read_for_output)      (the delayed sample to emit)
    wet = held                                       (sample-and-hold reconstruction out)
    output = mix(dry, wet)                            (full rate)
```

CPU for the wet path therefore falls ~linearly with `D`; the only full-rate work is the dry
path and the S&H mix. Decimation and reconstruction are naive **sample-and-hold** — the
aliasing and stairstep *are* the lo-fi character and are wanted, not filtered away.

**Bit-crush + storage contract.** Bit-crush is a **mid-tread quantization onto a `B`-bit
grid** — zero is always a grid point, so silence stays silent and no idle offset is injected.
`lofi_bits = B ∈ {4…16}`. The sign-safe round-to-grid, with a hard bypass at `B = 16`:

```
crush_grid(v, q) = clamp( round_ties_away(v / q) * q, lo, hi )   // mid-tread, saturate; B=16 ⇒ bypass
```

Storage then applies each policy's own quantization:

- **int16 policy (Nucleo)** — reuse FR-038a for the container:
  ```
  store:  x_f (finite; non-finite → 0)
          code16 = clamp( round_ties_away(x_f * 32768), -32768, 32767 )   // saturate, no wrap
          code_B = (B == 16) ? code16 : crush_grid(code16, 2^(16-B))      // q ∈ {2^12 … 2}
          store code_B
  read:   x_f = code_stored / 32768.0f
  ```
  So `B=8 ⇒ q=256`, `B=4 ⇒ q=4096`: the low bits are **rounded away while the 16-bit
  container scale is retained** — `code_B / 32768` stays normalized and the signal level is
  unchanged (it does *not* shrink by `2^(16-B)`, the trap in a literal right-shift).
- **float policy (desktop/Daisy)** — no container quantization; crush in the normalized domain:
  ```
  store:  x_c = (B == 16) ? x_f : crush_grid(x_f, 2^(1-B))               // step 2^(1-B) over [-1,1)
          store x_c                                                       // raw float; verbatim when B=16
  read:   x_f = stored
  ```
  The **`B=16` hard bypass is load-bearing**: it stores raw float (no grid imposed), which is
  what makes the clean float path bit-exact with today's delay — the numerical-identity
  acceptance test (§Decisions 12) asserts exactly this.

Crush sits **inside** the feedback loop, so it is recursive (grittier the longer a repeat
rings). No dither. On the int16 policy the clean end (`B=16`) is **full-resolution 16-bit**
(the always-on container quantization is the lo-fi floor); on the float policy the clean end
is bit-exact float.

**Live rate-change semantics (reinterpret / tape-speed).** Read and write positions are in
internal-sample units. On a live `D` change the buffer contents are **retained and
reinterpreted at the new rate**: the `N` stored internal samples now represent
`N / (48000/D_new)` seconds, so existing repeats pitch/time-smear like a tape-speed change
and the available delay range jumps. The read offset is clamped to the new realizable max.
On the change the **decimator phase is reset** — the sample on which the new `D` takes effect
is an internal tick — giving immediate, deterministic control response; sub-cycle phase
continuity is intentionally *not* preserved (the change is already a deliberate
discontinuity, so preserving it buys nothing and only complicates tests). This is a defined
feature with a dedicated test (write at `D=1`, switch to `D=2`, assert the expected
time-stretch/reinterpretation and that the new value takes effect on an internal tick), not
accidental behavior.

**Delay-time parameter (OQ1 resolved).** Delay time is **physical seconds, clamped to the
current rate's realizable maximum**. The normalized `[0,1]` control maps to the *fixed*
semantic maximum (the longest, at `D=8`: `MaxDelaySamples · 8 / 48000`) and is then clamped
to the current `D`'s realizable max (`MaxDelaySamples · D / 48000`). Consequence, documented:
at low `D` the knob saturates over its upper travel — the deliberate price of a stable
physical meaning. Available *range* is coupled to fidelity (intended); the current *setting*
is not additionally coupled.

**Memory-layout contract.** The object size is compile-time bounded and observable:
`sizeof(ModulatedDelayEffect<N,S,C>) ≈ C · N · sizeof(S) + fixed overhead`. Each target is
responsible for linking that object into an appropriate RAM region (Daisy → SDRAM; Nucleo →
main SRAM). The Nucleo build adds a `static_assert(sizeof(NucleoModulatedDelay) <=
kNucleoEffectRamBudget)` plus a map-file check.

**Bit-crush (live).** `lofi_bits` = `B ∈ {16…4}` effective bits, applied per the quantization
contract above, in the feedback loop.

**Live params on the US6 path.** Two new parameters — `lofi_rate` (discrete 1/2/4/8) and
`lofi_bits` (16→4) — added to the effect's parameter list; extend `midi-cc-map.h` so two CCs
bind to their indices. All existing character (LFO-modulated delay time, wow/flutter, the
mode filter) is retained and now runs on the internal clock inside the loop.

**Reuse, with the right seam.** The **bounded static delay line is a storage primitive**
(a variant/extension under `core/primitives/delays/`, e.g. a fixed-capacity int16-or-float
ring), because "bounded memory" is general and a future reverb will consume the same
machinery. Only the **decimator** and the **bit-crush** — genuine lo-fi *policies* with
independent semantics — live under `core/primitives/lofi/`.

### Rejected — lower the whole USB device rate

Rewrites the USB descriptors and the fixed 48/16 matrix (D4), global rather than per-effect,
far more invasive than the goal. Operator chose effect-local decimation.

### Rejected — rate as pure tone control, fixed maximum delay time (Approach B)

Simpler and keeps the delay-time range stable, but forfeits the "cheap long echoes" trick and
buys no memory back. Operator chose A.

### Rejected — separate Nucleo-only lo-fi effect

Cleanest separation but ships two delay effects and shares no code. Operator wants the lo-fi
capability on the existing effect.

### Rejected — int16 storage on all platforms

Would change the desktop/plugin delay's sound (recursive int16 quantization). The storage
policy keeps float32 the default so existing platforms stay numerically identical; int16 is
the Nucleo policy only.

### Rejected — float32 storage on Nucleo / int8 storage / compile-time-only variants

float32 halves the Nucleo delay time per KB and never contributes lo-fi; int8 has no clean
setting (kept as a possible future Nucleo build variant); compile-time-only lo-fi needs
reflashing to compare (and reflashing degrades host CoreAudio, observed this session) — the
operator wants live control.

## Decisions

1. **Template on `<MaxDelaySamples, Sample = float, MaxChannels = 8>`**; whole effect
   heap-free (delay buffers + wow/flutter scratch move to in-object `std::array`).
2. **Storage policy:** float32 default (desktop/Daisy numerically unchanged), **int16 on
   Nucleo** via `NucleoModulatedDelay = ModulatedDelayEffect<14400, int16_t, 2>`.
3. **Bound is a sample count**; achievable seconds = `MaxDelaySamples / sampleRate`. No
   universal "2 s" claim.
4. **Whole wet loop runs at the internal rate** `48000/D`; sample-and-hold decimation in /
   reconstruction out; dry path + mix at 48 kHz. Delay-time LFO and wow/flutter compute in
   seconds→internal-samples.
5. **Approach A:** buffer holds decimated-rate samples ⇒ max delay `= MaxDelaySamples·D/48000`.
6. **Bit-crush = mid-tread round-to-`B`-bit-grid inside the feedback loop**, per the explicit
   contract (`round(v/q)*q` keeping the container scale, `B=16` hard bypass, saturate,
   non-finite→0, no dither, 0 preserved). int16 clean end is **full-resolution 16-bit**; float
   clean end is bit-exact.
7. **Live `D` change = reinterpret buffer (tape-speed jump)** with the **decimator phase
   reset** (new `D` takes effect on an internal tick), a tested feature contract.
8. **Delay time in physical seconds**, normalized to the fixed `D=8` maximum, clamped to the
   current `D`'s realizable max (OQ1 resolved).
9. **Memory contract:** `sizeof` bounded + observable; Nucleo `static_assert` on footprint +
   map-file check; each target links the object into the right RAM region.
10. **Two live params** (`lofi_rate`, `lofi_bits`) on the US6 MIDI-CC path.
11. **Seam:** bounded static delay line = storage primitive under `primitives/delays/`;
    decimator + bit-crush = lo-fi policies under `primitives/lofi/`.
12. **Host TDD first, then hardware acceptance** — flash, confirm enumeration (the TASK-34
    fix), sweep CC to audition, capture `worstBlockMicros` + counters over the T058/T059 HIL
    rig. Named acceptance tests: (a) **float clean-path numerical identity** — the
    `float`/`D=1`/`B=16` path is bit-exact against the pre-change float delay on a reference
    signal; (b) the **tape-speed reinterpret** on a live `D` change; (c) the **no-alloc**
    guarantee (T066 sentinel); (d) the **crush level-preservation** (a B-bit-crushed
    full-scale tone keeps its RMS, guarding against the right-shift level-drop trap).

### Captured but scoping-deferred (capture-over-YAGNI)

- **Reverb** (`design:feature/reverb-engines`) — the reason the pieces are factored; the
  reverb itself is separate work.
- **int8 Nucleo build variant** for extreme-length / smallest-footprint delays.
- **~400 ms clean-end** — adopt if board measurement shows the headroom (current plan 300 ms).
- **Anti-aliased ("clean divisor") decimation mode** — deliberately not done; it fights the
  aesthetic here.

## Open questions

1. **CC assignments.** Which specific CC numbers bind to `lofi_rate` and `lofi_bits`, relative
   to the existing convention (CC 74 → index 0, CC 71 → index 1). Settled in implementation.
2. **CPU headroom.** Confirm `worstBlockMicros` stays well inside the 1000 µs/block budget at
   `D=1` (worst case for the wet loop); SVF baseline was 65 µs.
3. **Build path.** Formal acfx Spec Kit spec via `/stack-control:define`, or direct host-TDD
   implementation, given it is a focused change already brainstormed and reviewed.

## Third-party review resolutions (2026-08-24)

### Round 1 (eight points)

Disposition:

1. **"Byte-for-byte unchanged" is impossible** — accepted, and strengthened: storage is a
   **policy**, float32 default so desktop/Daisy stay numerically identical (heap→static only);
   the bound is a sample count with seconds derived as `MaxDelaySamples/sampleRate`.
2. **Live `D`-change semantics undefined** — accepted: defined as **reinterpret / tape-speed
   jump**, a tested feature; delay time defined physically in seconds → internal samples.
3. **int16 quantize/saturate boundary underspecified** — accepted: explicit contract reusing
   FR-038a (×32768, round ties-away, clamp/saturate, sign-preserving B-bit mask, non-finite→0,
   no dither); clean end renamed **full-resolution 16-bit**.
4. **What is decimated was ambiguous** — accepted: **whole wet loop at the internal rate**;
   explicit processing-clock pseudocode; CPU falls with `D`.
5. **Static-storage memory-layout contract** — accepted: `sizeof` bounded/observable,
   `static_assert` + map-file check on Nucleo, per-target RAM-region responsibility. Also
   surfaced that `std::array` forces bounding `MaxChannels` (added as a template arg).
6. **Named type, not opaque template in CMake** — accepted: `NucleoModulatedDelay` alias.
7. **Resolve OQ1 now** — accepted: physical seconds, normalized to fixed `D=8` max, clamped to
   current realizable max.
8. **Keep the buffer out of `lofi/`** — accepted: bounded delay line is a storage primitive
   under `primitives/delays/`; only decimator + bit-crush under `primitives/lofi/`.

### Round 2 (two clarifications + a wording point) — "approve with clarifications"

1. **Bit-crush stored representation** — accepted, and it fixed a latent bug: the earlier
   `arithmetic_shift_round(code16, 16-B)` wording, taken literally, is a level-dropping fader,
   not a bit-crusher. Replaced with mid-tread round-to-grid keeping the container scale
   (`crush_grid`), int16 and float variants, `B=16` bypass. A crush-level-preservation
   acceptance test guards the trap.
2. **Decimator phase on a live `D` change** — accepted: phase is **reset**; the new `D` takes
   effect on an internal tick. Deterministic, immediate, consistent with the intentional
   tape-speed discontinuity.
3. **Wording: "numerically identical" → "intended to preserve"** — accepted, and made a
   *tested* acceptance property; the float-path `B=16` crush bypass is the mechanism that
   makes clean-path bit-exactness actually hold, so the two are specified together.

No substantive disagreement with round 2; both clarifications were correct and the wording
point exposed a real correctness dependency (float clean-path bypass) rather than only a
phrasing nuance.

## Provenance

Brainstormed 2026-08-24 with the operator after the Phase-12 (US9) hardware verification of
the nucleo-f446-adapter feature surfaced that neither a reverb nor the existing delay runs on
the F446. Operator selections captured inline: effect-local decimation; live MIDI-CC control;
Approach A (bandwidth-for-time); float-default/int16-Nucleo storage policy; reinterpret
(tape-speed) live rate changes; whole-wet-loop internal decimation. Revised the same day to
resolve a third-party review (three blocking contracts pinned: compatibility/fidelity,
live-rate semantics, quantization math). The bounding half resolves backlog TASK-34; the
lo-fi half seeds reusable primitives for the future reverb. A second review round approved the
design with two clarifications (bit-crush grid math; decimator phase on a live rate change),
both folded in above, leaving only the three non-blocking open questions (CC assignments, CPU
headroom, build path).
