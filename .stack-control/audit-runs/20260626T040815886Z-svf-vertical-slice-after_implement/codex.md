### C++17 effect fallback does not enforce the advertised contract

Finding-ID: AUDIT-BARRAGE-codex-01
Status:     open
Severity:   medium
Surface:    core/dsp/effect.h:43-64

The C++17 path says “Template instantiation still enforces the real member signatures” and exposes `ACFX_EFFECT_CONCEPT` as `typename`, but the fallback trait only checks that expressions exist. It does not verify return types match `void`, nor that `T::parameters()` is convertible to `span<const ParameterDescriptor>` like the C++20 concept does at lines 21-29. A type with `int prepare(...)`, `bool process(...)`, or a wrong descriptor surface can pass `is_effect_v` on Teensy while being rejected on C++20.

The blast radius is medium because this weakens the core cross-platform contract: downstream C++17 adopters can get a false positive from the diagnostic surface and then fail later in less local template code, or accidentally accept an effect shape that desktop builds reject. A reasonable fix is to make the C++17 trait compare exact return types with `std::is_same_v` / `std::is_convertible_v`, matching the C++20 concept as closely as possible.

### Prepare accepts invalid process context values into persistent effect state

Finding-ID: AUDIT-BARRAGE-codex-02
Status:     open
Severity:   medium
Surface:    core/effects/svf/svf-effect.h:73-78; core/dsp/process-context.h:9-12

`ProcessContext` is a plain public boundary type with no invariants, but `SvfEffect::prepare()` trusts `ctx.sampleRate` and only upper-clamps `ctx.numChannels`. A negative `ctx.numChannels` is stored directly because `-1 < kMaxChannels`, after which `process()` computes a negative channel count and silently processes nothing. Non-positive or non-finite `sampleRate` is also passed into `filters_[ch].init(sampleRate_)` before any validation.

The blast radius is medium because this is the adapter-to-core boundary for every platform; a bad device report or adapter bug can put the effect into a silent or invalid state without a clear failure. A reasonable fix is to encode the invariant at the boundary: clamp channels to `[0, kMaxChannels]` and reject/assert/fail predictably for invalid sample rates before mutating filter state.
