# Contract — The `Effect` concept (core/dsp)

The compile-time contract every effect satisfies. No base class, no vtable in the
audio path. Satisfied by plain structs/classes.

## C++20 form (desktop, tests, Daisy)

```cpp
template <typename T>
concept Effect = requires(T fx, const ProcessContext& ctx, AudioBlock& io,
                          ParamId id, float norm) {
    { fx.prepare(ctx) }        -> std::same_as<void>;   // set sr, maxBlock, channels
    { fx.process(io) }         -> std::same_as<void>;   // in-place; NO heap alloc
    { fx.reset() }             -> std::same_as<void>;   // clear internal state
    { T::parameters() }        -> std::convertible_to<std::span<const ParameterDescriptor>>;
    { fx.setParameter(id, norm) } -> std::same_as<void>; // normalized 0..1 in
};
```

## C++17 degradation (Teensy)

Where concepts are unavailable, the same member signatures are required by plain
template instantiation (duck typing). The named `Effect` check is compiled out via
a feature guard (`__cpp_concepts`); the effect code is identical. A
`static_assert`-based fallback check MAY be provided for C++17 to preserve a
diagnostic, but it is best-effort and never gates the build on that target.

## Behavioral guarantees (normative)

- **RT-safety**: `process` performs no heap allocation, takes no locks, and does
  bounded work per sample (Constitution VI). Verified by the no-allocation test
  (FR-014).
- **Normalized I/O**: `setParameter` receives a normalized `0..1` value; the effect
  maps it to plain units via the matching `ParameterDescriptor` skew.
- **Lifecycle**: `process`/`setParameter` are only valid after `prepare`. A
  sample-rate or channel change is signaled by a new `prepare`; `reset` clears
  state without changing the prepared configuration.
- **Block bounds**: `process` never receives `numSamples > ctx.maxBlockSize` or
  `numChannels > ctx.numChannels`.

## Consumers

- `EffectNode<T>` (host boundary) requires `T` to satisfy `Effect`.
- MCU adapters instantiate the concrete effect directly (no node).
- Tests instantiate concrete effects and exercise the contract.
