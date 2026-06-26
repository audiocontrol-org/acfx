### Low/invalid sample rates can violate the SVF primitive’s cutoff precondition

Finding-ID: AUDIT-BARRAGE-codex-01
Status:     open
Severity:   medium
Surface:    core/effects/svf/svf-effect.h:73-78, core/effects/svf/svf-effect.h:160-169

`prepare()` accepts `ctx.sampleRate` without validating it, and `clampedCutoff()` first caps cutoff to `sampleRate_ * 0.32f` but then raises anything below `20.0f` back to `20.0f`. For any `sampleRate_ <= 62.5`, zero, negative, or non-finite sample rate, the final cutoff can be outside the stated DaisySP bound of strictly below `sampleRate / 3`. That means the wrapper’s own “safe band” claim is false for malformed or not-yet-valid host/device contexts.

The blast radius is medium: normal adapters likely pass 44.1k/48k rates, but a downstream adapter or host lifecycle edge that calls `prepare()` with `0` or an invalid rate can put the DSP primitive into an unsupported state before the first audio block. A reasonable fix would make `ProcessContext` validation explicit at the effect boundary: reject/ignore invalid sample rates, clamp cutoff with a lower bound that never exceeds the upper bound, or define a minimum supported sample-rate invariant and enforce it before `SvfPrimitive::init()` / `setFreq()`.
