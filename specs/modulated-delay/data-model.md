# Phase 1 Data Model: Modulated Delay

**Feature**: [spec.md](./spec.md) | **Plan**: [plan.md](./plan.md) | **Date**: 2026-06-28

The "data model" for a DSP effect is its **state and parameter shapes** — the
entities the effect owns, the values it stores, and the single parameter table that
every adapter consumes. All state is fixed-size and preallocated; nothing here
implies heap use in `process()`.

---

## Entities

### DelayLine (new primitive — `core/primitives/delay-line.h`)

An allocation-free circular buffer supporting fractional (interpolated) reads.

| Field | Type | Notes |
|-------|------|-------|
| `buffer_` | pointer + capacity to caller-owned/`prepare`-owned storage | sized at `prepare()`; never reallocated in `process()` |
| `capacity_` | `int` (samples) | ≥ max delay samples + 1 guard |
| `writePos_` | `int` | advances one per sample, wraps mod capacity |
| `sampleRate_` | `float` | for seconds↔samples conversion |

- **Operations**: `prepare(storage, capacity, sampleRate)`, `reset()` (zero-fill +
  write pos reset), `write(x)`, `readFractional(delaySamples)` (linear interp),
  `maxDelaySamples()`.
- **Invariants**: a read of `d` samples ago reads strictly within `[0, capacity_)`;
  `d` is clamped to `[0, capacity_ - 1]` before the interpolated fetch so reads can
  never go out of range (FR-007). No allocation, no locks, bounded work.

### Lfo (new primitive — `core/primitives/lfo.h`)

A low-frequency oscillator with selectable shape; one per modulation destination.

| Field | Type | Notes |
|-------|------|-------|
| `phase_` | `float` in [0,1) | phase accumulator |
| `inc_` | `float` | `rate / sampleRate` per tick |
| `shape_` | `LfoShape` | {sine, triangle, saw, random} |
| `rngState_` | `uint32_t` | xorshift state for the random shape |
| `randCurr_`, `randTarget_` | `float` | endpoints for smoothed sample-and-hold |
| `sampleRate_` | `float` | for sample-rate-independent rate |

- **Operations**: `prepare(sampleRate)`, `reset()`, `setRate(hz)`, `setShape(shape)`,
  `tick()` → bipolar value in [-1, 1].
- **Invariants**: output is bounded in [-1, 1]; `tick()` is allocation/lock-free and
  bounded; rate is sample-rate independent (same musical motion at 44.1/48/96 kHz).

### SvfPrimitive (existing — reused verbatim)

The per-channel feedback filter. No change. Cutoff (Hz, clamped < sr/3), resonance
(0..1), mode {lowpass, highpass, bandpass}. One instance per channel.

### WowFlutterStage (new — `core/effects/modulated-delay/wow-flutter.h`)

Input-path tape-instability processor; owns its own short `DelayLine` (per channel)
and two `Lfo`s (wow, flutter).

| Field | Type | Notes |
|-------|------|-------|
| `lines_[ch]` | `DelayLine` | short (tens of ms) input delay, per channel |
| `wow_` | `Lfo` | slow component |
| `flutter_` | `Lfo` | faster component |
| `wowDepth_`, `flutterDepth_` | `float` | displacement depth in samples (from normalized) |
| `nominalSamples_` | `float` | center tap so modulation is bipolar around it |

- **Operations**: `prepare(ctx, storage…)`, `reset()`, `setWowRate/Depth`,
  `setFlutterRate/Depth`, `process(x, ch)` → pitch-modulated sample.
- **Invariants**: depths at zero → output == input at the nominal tap (FR-019);
  reads stay in range; no allocation in `process()`.

### ModulatedDelayEffect (new — `core/effects/modulated-delay/modulated-delay-effect.h`)

The top-level effect satisfying the `Effect` contract. Owns: per-channel main
`DelayLine` + `SvfPrimitive`; three modulation `Lfo`s (delay, cutoff, resonance);
the `WowFlutterStage`; applied parameter state; and the lock-free pending-edit
atomics (same pattern as `SvfEffect`).

| Applied-state field | Type | Drives |
|---------------------|------|--------|
| `delaySeconds_` (smoothed base) | `float` | main delay read position |
| `feedback_` | `float` (0..<1) | feedback scale |
| `mix_` | `float` (0..1) | dry/wet blend |
| `cutoffHz_`, `resonance_`, `mode_` | float/float/`SvfMode` | feedback filter |
| `delayMod{Rate,Depth,Shape}_` | float/float/`LfoShape` | delay-time LFO |
| `cutoffMod{Rate,Depth,Shape}_` | float/float/`LfoShape` | cutoff LFO |
| `resMod{Rate,Depth,Shape}_` | float/float/`LfoShape` | resonance LFO |
| `wow{Rate,Depth}_`, `flutter{Rate,Depth}_` | float | wow & flutter stage |

- **Per-sample flow** (per channel): `x' = wowFlutter(x)` → `d = delayLine.readFractional(baseDelay + delayLfo*depth)` → `f = svf.process(d)` → `delayLine.write(x' + feedback*f)` → `out = mix(x', wetTap)` where the wet tap is the post-filter delayed signal (research Decision 2). Cutoff/resonance LFOs update the SVF coefficients at the chosen cadence (research Decision 4).
- **Threading**: `setParameter` publishes lock-free pending atomics; `process()`
  consumes them at the top via `applyPending()` (identical pattern to `SvfEffect`,
  FR-023). `prepare`/`reset` mutate coefficients/buffers only while the stream is
  stopped.

---

## Parameter Table (single source of truth — FR-022)

One `constexpr std::array<ParameterDescriptor, N>` on `ModulatedDelayEffect`, with a
build-time `static_assert` over `isValidDescriptor` (as `SvfEffect` does). Proposed
controls (exact defaults/ranges finalized in implementation):

| Param | Name | Unit | Skew | Kind | Range (plain) |
|-------|------|------|------|------|---------------|
| delayTime | `delay_time` | seconds* | logarithmic | continuous | ~0.001 … 2.0 s |
| feedback | `feedback` | percent/none | linear | continuous | 0 … 0.98 |
| mix | `mix` | percent/none | linear | continuous | 0 … 1 |
| cutoff | `fb_cutoff` | hz | logarithmic | continuous | 20 … 20000 |
| resonance | `fb_resonance` | none | linear | continuous | 0 … 1 |
| mode | `fb_mode` | none | linear | discrete | {LP, HP, BP} (count 3) |
| delayModRate | `delay_mod_rate` | hz | logarithmic | continuous | ~0.01 … 20 |
| delayModDepth | `delay_mod_depth` | none | linear | continuous | 0 … 1 |
| delayModShape | `delay_mod_shape` | none | linear | discrete | {sine,tri,saw,rand} (4) |
| cutoffModRate | `cutoff_mod_rate` | hz | logarithmic | continuous | ~0.01 … 20 |
| cutoffModDepth | `cutoff_mod_depth` | none | linear | continuous | 0 … 1 |
| cutoffModShape | `cutoff_mod_shape` | none | linear | discrete | 4 shapes |
| resModRate | `res_mod_rate` | hz | logarithmic | continuous | ~0.01 … 20 |
| resModDepth | `res_mod_depth` | none | linear | continuous | 0 … 1 |
| resModShape | `res_mod_shape` | none | linear | discrete | 4 shapes |
| wowRate | `wow_rate` | hz | logarithmic | continuous | ~0.1 … 2 |
| wowDepth | `wow_depth` | none | linear | continuous | 0 … 1 |
| flutterRate | `flutter_rate` | hz | logarithmic | continuous | ~5 … 12 |
| flutterDepth | `flutter_depth` | none | linear | continuous | 0 … 1 |

\* `seconds` is a small additive extension to `ParamUnit` (research Decision 7).

- Parameter ids are a dense `enum Param : uint8_t` indexing the table (as `SvfEffect`).
- `mode` and the three `*ModShape` params are discrete (`discreteCount` ≥ 2), so the
  build-time descriptor check enforces validity.

---

## State lifecycle

- **`prepare(ctx)`**: compute max delay samples for 2.0 s at `ctx.sampleRate`;
  bind/zero the preallocated main + wow/flutter buffers; init per-channel `SvfPrimitive`
  and all `Lfo`s with `ctx.sampleRate`; apply all current parameters. (Stream stopped.)
- **`reset()`**: zero all delay buffers, reset LFO phases and filter state, re-apply
  parameters. (Stream stopped.)
- **`process(io)`**: `applyPending()` then per-channel/per-sample flow above. No
  allocation, no locks, bounded work.
- **`setParameter(id, norm)`**: any thread; publish pending atomic only.

---

## Validation rules (mapped to requirements)

- Fractional read always in range for any base delay + modulation (FR-007, FR-014) —
  delay-line bounds test.
- Changing delay time / sweeping it is click-free (FR-008, Decision 5) — smoothing test.
- Zero depth ⇒ destination static (FR-013) — modulation off-equivalence test.
- Feedback never diverges uncommanded; bounded < 1.0 (FR-010) — feedback-stability test.
- Sample-rate independence of rates/times (FR-015) — cross-rate test.
- `process()` performs zero allocations (FR-006, SC-007) — reuse allocation sentinel.
- Single parameter table drives everything (FR-022, SC-008) — table presence/validity test.
