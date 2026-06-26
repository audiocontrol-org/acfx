### `SvfEffect::prepare` accepts invalid run conditions that can violate the SVF primitive contract

Finding-ID: AUDIT-BARRAGE-codex-01
Status:     open
Severity:   medium
Surface:    core/effects/svf/svf-effect.h:61-66, core/effects/svf/svf-effect.h:148-157

`prepare()` copies `ctx.sampleRate` and clamps only the upper channel count, but it does not enforce the lower-bound invariants needed by the primitive: positive sample rate and non-negative channel count. `clampedCutoff()` then computes `maxFreq = sampleRate_ * 0.32f`, applies that as an upper clamp, and finally forces the result back up to `20.0f`. For `sampleRate_ <= 62.5`, zero, negative, or NaN-ish bad input, the returned cutoff can no longer satisfy the documented DaisySP requirement of `0 < f < sampleRate/3`, yet `applyCutoff()` still sends it into `SvfPrimitive::setFreq()`.

The blast radius is medium because this is a boundary-contract defect: normal desktop/audio-device paths probably pass valid sample rates, but `ProcessContext` is a plain public struct with no constructor or validator, and downstream adapters/tests can reasonably treat `prepare()` as the enforcement point. A reasonable fix would make the run-condition invariant explicit in code: reject or sanitize invalid `sampleRate`/`numChannels` before initializing filters, and ensure `clampedCutoff()` never returns a value outside the primitive’s stated frequency domain.
