# Phase 1 Data Model — SVF Vertical Slice

The "data" of this feature is the set of compile-time/value types that form the
spine. No runtime persistence. Entities map to the spec's Key Entities; field
lists are conceptual (the exact C++ spelling lives in `contracts/`).

## ParamId

- **What**: a strongly-typed parameter identifier (not a bare int).
- **Fields**: `value: uint8` (small, stable, dense per effect).
- **Rules**: unique within an effect; indexes into the effect's descriptor table;
  out-of-range ids are a programming error surfaced by assertion in debug builds.

## ParameterDescriptor

- **What**: the single declared definition of one control, consumed by every
  adapter (FR-003).
- **Fields**: `id: ParamId`, `name: string-view (static)`, `unit: enum {none, hz,
  dB, percent, …}`, `min: float`, `max: float`, `defaultValue: float`,
  `skew: enum {linear, logarithmic}` (or a skew factor), `kind: enum {continuous,
  discrete}`, `discreteCount: uint8` (for enums like mode; 0 when continuous).
- **Rules**: `constexpr`-constructible (lives in a `static constexpr` table);
  `min < max`; normalized↔plain mapping defined by `skew`; for `discrete`,
  `discreteCount ≥ 2` and normalized values quantize to `[0, discreteCount)`.
- **Relationships**: an effect owns an ordered, `constexpr` array of these
  (`T::parameters()`); adapters iterate the array to build GUIs / automation /
  control maps.

## ProcessContext

- **What**: the prepared run conditions handed to `prepare`.
- **Fields**: `sampleRate: double`, `maxBlockSize: int`, `numChannels: int`.
- **Rules**: provided by the adapter before audio starts and on any device change;
  triggers re-tuning + `reset`; `maxBlockSize`/`numChannels` bound all subsequent
  `process` calls (no `process` may exceed them — no reallocation mid-stream).

## AudioBlock

- **What**: a fixed-size, non-owning, non-allocating view of multichannel audio
  passed to `process` (FR-002).
- **Fields**: `channels: span<float*>` (or fixed-capacity channel pointers),
  `numChannels: int`, `numSamples: int` (≤ `maxBlockSize`).
- **Rules**: never allocates; in-place processing (input and output alias);
  `numSamples` varies per call but never exceeds the prepared `maxBlockSize`.

## Effect (concept — the contract)

- **What**: any type satisfying the `Effect` C++20 concept; no base class, no
  vtable in the hot path (FR-001).
- **Operations**: `prepare(ProcessContext)`, `process(AudioBlock&)` [no heap
  alloc], `reset()`, `static constexpr parameters() -> span<const
  ParameterDescriptor>`, `setParameter(ParamId, float normalized)`.
- **Rules**: `process` honors Constitution VI (no alloc/lock/unbounded work);
  `setParameter` accepts normalized `0..1` and maps via the descriptor's skew;
  state cleared by `reset`/`prepare`.
- **State transitions**: `constructed → prepare(ctx) → [process|setParameter|reset]*
  → prepare(ctx')` (re-prepare on sample-rate/channel change). `reset` returns to a
  cleared-but-prepared state.

## SvfEffect (the concrete proving effect)

- **What**: the State-Variable Filter; satisfies `Effect`; wraps DaisySP `Svf`.
- **Parameters** (its `constexpr` descriptor table): `cutoff` (continuous,
  logarithmic, Hz, ~20–20000, default ~1000), `resonance` (continuous, linear,
  0..1, default low), `mode` (discrete, count 3: low/high/band, default low).
- **State**: the wrapped DaisySP `Svf` instance + cached prepared sample rate.
- **Rules**: `setParameter(mode, …)` quantizes to one of three modes; `process`
  pulls the selected mode's output per sample; no allocation; self-oscillation at
  high resonance must stay NaN/denormal-free (Edge Cases).

## ProcessorNode (desktop host boundary — NOT compiled on MCU)

- **What**: abstract interface letting a desktop host hold any `Effect`
  polymorphically at ≤1 virtual call per block (FR-004).
- **Operations**: `prepare(ctx)`, `processBlock(AudioBlock&)` [the single virtual
  call/block], `reset()`, `parameters()`, `setParameter(ParamId, float)`.
- **Realization**: a templated adapter `EffectNode<T: Effect>` stores a `T` by value
  and forwards each virtual call to the inlined concrete method.
- **Rules**: lives in `host/processor-node/`; never included by the Daisy/Teensy
  builds; the only point in the system where a virtual call touches the audio path.

## Relationships (summary)

```
ParameterDescriptor[]  ──owned by──>  Effect (e.g. SvfEffect)
        │                                   │ satisfies
        │ consumed by                       ▼
        ├─> workbench (GUI + MIDI)     EffectNode<SvfEffect> ──is-a──> ProcessorNode
        ├─> plugin (host automation)        (desktop only)
        └─> daisy/teensy (ADC/encoder map)  SvfEffect used directly (no node) on MCU
```
