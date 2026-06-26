### `normalize()` yields silent NaN for degenerate linear `ParameterDescriptor` with `min == max`

Finding-ID: AUDIT-BARRAGE-claude-01
Status:     open
Severity:   medium
Surface:    core/dsp/parameter.h:88

`normalize()` for a continuous linear parameter falls through to:

```cpp
return (plain - d.min) / (d.max - d.min);
```

When `d.min == d.max` this is a division by zero (yielding `NaN` or `±inf` in IEEE 754 arithmetic). Unlike the logarithmic branches at lines 78 and 82, there is no `assert` guarding this path. The NaN would then be passed to `setParameter` → `pendingNorm_[i].store()` → `denormalize()` → `SvfPrimitive::setFreq()` where DaisySP may compute coefficients from NaN, silently producing silence or unbounded output. Because parameter descriptors are compile-time constants, the degenerate case can only arise from an authoring mistake, but when it happens the failure mode is completely invisible at runtime (no assert fires in release builds, no error surface). A debug-build `assert(d.max > d.min, "linear parameter requires min < max")` immediately before the return would make the mistake diagnosable with zero runtime cost in release.

---

### `SvfEffect::process()` silently passes audio unchanged before `prepare()` is called

Finding-ID: AUDIT-BARRAGE-claude-02
Status:     open
Severity:   low
Surface:    core/effects/svf/svf-effect.h:67–79

`numChannels_` is initialized to `0` (line 165) and is only set to a positive value inside `prepare()`. If an adapter calls `process()` before `prepare()` — or after a `prepare()` with `ctx.numChannels == 0` — the channel loop condition `ch < channels` is never satisfied and the audio block passes through the effect as if no processing occurred:

```cpp
const int channels = io.numChannels() < numChannels_ ? io.numChannels() : numChannels_;
// channels == 0 when numChannels_ == 0
for (int ch = 0; ch < channels; ++ch) { ... }   // body never executes
```

No assert, no diagnostic, and no indication to the caller that the effect is uninitialized. This is an implicit passthrough — exactly the kind of silent fallback that the project constitution explicitly prohibits ("raise descriptive errors for missing functionality instead"). The RT constraint makes exceptions off-limits, but a debug-build `assert(numChannels_ > 0, "SvfEffect::process called before prepare")` would catch adapter integration mistakes early.

---

### Discrete `ParameterDescriptor::min` and `max` fields are dead data — misleading to adapter authors

Finding-ID: AUDIT-BARRAGE-claude-03
Status:     open
Severity:   low
Surface:    core/dsp/parameter.h:18–28, core/effects/svf/svf-effect.h:43–45

Both `denormalize()` and `normalize()` take an early-return path for `ParamKind::discrete` that uses only `discreteCount` for bucket mapping and never reads `d.min` or `d.max`:

```cpp
// denormalize, line 38-47: uses discreteCount, ignores min/max
// normalize,   line 66-72: same
```

The `kMode` descriptor (svf-effect.h:43–45) sets `min=0.0f, max=2.0f` — values that have zero effect on runtime behavior. An adapter author reading the descriptor table for the first time could reasonably infer that `[0.0, 2.0]` is the usable range of the mode parameter and try to feed or display values in that range, only to discover the actual bucket mapping is driven entirely by `discreteCount`. The struct-level documentation in `parameter.h` does not call out that `min`/`max` are undefined/unused for discrete parameters. Either a comment on `ParameterDescriptor` noting "min/max are only meaningful for `ParamKind::continuous`", or using `0.0f`/`0.0f` as a clear sentinel in the discrete descriptor, would eliminate the false-signal.

---

### `SvfPrimitive::reset()` destroys DaisySP freq/res state but primitive cannot self-recover

Finding-ID: AUDIT-BARRAGE-claude-04
Status:     open
Severity:   informational
Surface:    core/primitives/svf-primitive.h:37

`reset()` calls `svf_.Init(sampleRate_)`, which clears DaisySP's internal filter state including its cached frequency and resonance coefficients. `SvfPrimitive` stores `sampleRate_` and `mode_` but does not cache `freq_` or `res_`. After `reset()`, the primitive's caller must explicitly call `setFreq()` and `setRes()` to restore coefficients or the filter will operate at DaisySP's initialization defaults:

```cpp
void reset() noexcept { svf_.Init(sampleRate_); }  // clears freq/res inside DaisySP
```

`SvfEffect::reset()` (svf-effect.h:60–63) compensates by calling `applyAll()` after iterating over the primitives, so the current integration is correct. However, `SvfPrimitive` is in `core/primitives/` — a public, reusable surface — and its `reset()` contract is not documented: nothing in the header warns that coefficients are erased and must be reapplied. A future effect that uses `SvfPrimitive` directly and calls `reset()` without the compensating re-apply will silently filter at the wrong frequency. A one-line comment on `reset()` stating that callers must re-apply `setFreq`/`setRes` after a reset would close this trap.