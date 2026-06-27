### C++17 effect contract accepts wrong return types

Finding-ID: AUDIT-BARRAGE-codex-01
Status:     open
Severity:   medium
Surface:    core/dsp/effect.h:9-13, core/dsp/effect.h:42-63, host/processor-node/processor-node.h:34-41

The C++17 path says “the same member signatures are enforced by plain template instantiation,” but `ACFX_EFFECT_CONCEPT` becomes plain `typename` and the diagnostic trait only checks expression existence via `decltype(...)`, not return types. `EffectNode` then calls `fx_.prepare(ctx)`, `fx_.process(io)`, `fx_.reset()`, and `fx_.setParameter(id, n)` in `void` wrappers, so an effect whose methods return `int`, `bool`, or another ignored value still compiles on the C++17/Teensy path even though the C++20 concept requires `std::same_as<void>`.

The blast radius is medium because current `SvfEffect` satisfies the intended shape, but the core contract is a cross-platform adoption surface. A downstream embedded effect can compile while violating the stated contract, and the failure will only appear as behavioral drift between C++20 and C++17 targets. A reasonable fix is to make the C++17 trait check exact signatures with `std::is_same_v<decltype(...), void>` and gate `EffectNode` with `std::enable_if_t<is_effect_v<T>>` or equivalent.

### Continuous normalize can still propagate NaN

Finding-ID: AUDIT-BARRAGE-codex-02
Status:     open
Severity:   medium
Surface:    core/dsp/parameter.h:70-95

`denormalize()` explicitly neutralizes non-finite normalized values through `detail::clamp01()`, but the inverse `normalize()` does not do the same for plain values. For continuous params, `plain = std::clamp(plain, d.min, d.max)` leaves NaN unchanged because comparisons with NaN are false, then the linear path returns NaN and the logarithmic path computes `log(NaN / min)`, also NaN.

The blast radius is medium because this is a public parameter mapping primitive consumed by adapters, not just an internal helper. Current tests cover non-finite input only for `denormalize()`, so a malformed host/default/plain value can still become a NaN normalized parameter where consumers expect this layer to be the pure safe mapping boundary. A reasonable fix is to apply a finite guard before the `std::clamp` in `normalize()` and add symmetric tests for NaN/+inf/-inf plain inputs.
