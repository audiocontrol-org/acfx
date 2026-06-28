# Contract — ModulatedDelayEffect (core/effects/modulated-delay)

The top-level effect. Satisfies the existing `Effect` contract
([svf-vertical-slice/contracts/effect-concept.md](../../svf-vertical-slice/contracts/effect-concept.md))
so it runs unchanged on every adapter, exactly as `SvfEffect` does. No platform
headers. Composes the new `DelayLine` and `Lfo` primitives and reuses `SvfPrimitive`.

## Satisfies the Effect contract

```cpp
fx.prepare(ctx);                  // size 2 s buffer @ ctx.sampleRate; init filters/LFOs (stream stopped)
fx.process(io);                   // in-place; NO heap alloc; consumes pending edits at top
fx.reset();                       // clear buffers + LFO phase + filter state (stream stopped)
ModulatedDelayEffect::parameters(); // span<const ParameterDescriptor> over the constexpr table
fx.setParameter(id, norm);        // normalized 0..1 in; any thread; publishes pending atomic
```

## Parameter table (single source of truth — FR-022)

One `constexpr std::array<ParameterDescriptor, N>` with a build-time `static_assert`
over `isValidDescriptor` (as `SvfEffect`). Controls: delay time, feedback, mix,
feedback-filter cutoff/resonance/mode, and per-destination modulation rate/depth/shape
for delay/cutoff/resonance, plus wow rate/depth and flutter rate/depth. See
[data-model.md](../data-model.md#parameter-table-single-source-of-truth--fr-022) for
the full table. Parameter ids are a dense `enum Param : uint8_t` indexing the table.

## Signal flow (normative, per channel, per sample)

```text
x_in
  └─ WowFlutterStage.process(x_in)                → x'        (input tape instability, FR-017/020)
        baseDelay = smooth(delaySeconds) + delayLfo.tick()*delayDepth   (FR-008, FR-011/012)
        d = mainDelay.readFractional(baseDelay * sr)                    (in-range, FR-007/014)
        f = svf.process(d)            (cutoff/res driven by their LFOs)  (FR-003/004/011)
        mainDelay.write(x' + feedback * f)   feedback clamped < 1.0      (FR-010)
  out = (1-mix)*x' + mix*f                                              (dry/wet, FR-002)
```

Cutoff/resonance LFO updates are applied at the cadence in research Decision 4
(per-sample default). Wet tap is the post-filter delayed signal (research Decision 2).

## Normative guarantees

- **RT-safety (Constitution VI, FR-006, SC-007)**: `process()` performs no heap
  allocation and takes no locks; all buffers are preallocated in `prepare()`.
- **In-range reads (FR-007, FR-014, FR-021)**: every delay read (main + wow/flutter)
  stays within its buffer for all parameter/modulation settings.
- **Lock-free edits (FR-023)**: `setParameter` publishes a pending atomic per param
  (bit-pattern in `uint32`, provably lock-free), consumed in `process()` via
  `applyPending()` — identical pattern to `SvfEffect`; edits never race `process()`.
- **Depth-zero equivalence (FR-013)**: any modulation depth at 0 holds its destination
  static, indistinguishable from the unmodulated delay.
- **Sample-rate independence (FR-015, SC-009)**: a given delay time and modulation
  rate produce the same musical result at 44.1/48/96 kHz.
- **Reuse, not reimplementation (FR-005)**: the feedback filter is `SvfPrimitive`
  over DaisySP `Svf`; the per-sample filter math is unchanged from the SVF effect.

## Consumer obligations

| Consumer | Obligation |
|---|---|
| workbench / plugin | drive `prepare`/`process`/`reset` on the standard lifecycle; render a control per descriptor; route MIDI/automation to `setParameter` |
| Daisy / Teensy | same lifecycle; map ADC/encoder/analog to `setParameter`; ensure the prepared buffer fits device RAM (bound if needed, research Decision 8) |

## Test obligations (host-side, doctest)

- Feedback-filter progressively shapes successive echoes (level + brightness decay).
- Delay-time modulation produces periodic pitch/time movement at the set rate;
  depth 0 ⇒ identical to unmodulated.
- Cutoff/resonance modulation produces periodic tonal movement at the set rate.
- Wow & flutter imparts slow + fast pitch instability; both depths 0 ⇒ passthrough.
- Feedback at the max bound does not diverge; no out-of-range reads anywhere.
- `process()` performs zero allocations (reuse the allocation sentinel).
- The parameter table is present, valid (`isValidDescriptor`), and the sole control source.
