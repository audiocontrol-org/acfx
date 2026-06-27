### `SvfEffect::setParameter` mutates filter state with no audio-thread ownership boundary

Finding-ID: AUDIT-BARRAGE-codex-01
Status:     open
Severity:   high
Surface:    core/effects/svf/svf-effect.h:64-76, core/effects/svf/svf-effect.h:110-121, core/primitives/svf-primitive.h:27-50

`SvfEffect::setParameter()` immediately calls `applyCutoff()`, `applyResonance()`, or `applyMode()`, and those methods write directly into each per-channel `SvfPrimitive` while `process()` reads and advances the same DaisySP state. The primitive setters at `core/primitives/svf-primitive.h:27-32` mutate the same `svf_`/`mode_` state that `process()` uses at `core/primitives/svf-primitive.h:40-50`, with no documented invariant that `setParameter()` must only run on the audio thread and no internal handoff/snapshot mechanism enforcing that boundary.

The blast radius is high because this core effect is advertised as the shared one-source-many-targets surface. A downstream adapter can reasonably call `setParameter()` from a control/UI loop while audio is processing, which creates a real data race / torn coefficient update against the filter state. The workbench happens to queue GUI/MIDI edits onto the audio callback, and the plugin applies automation inside `processBlock`, but the core contract itself does not encode that requirement; MCU adapters are especially likely to sample controls outside the audio callback.

A reasonable fix is to make the ownership invariant explicit and mechanically hard to violate: either require and document that `setParameter()` is audio-thread-only and update all adapters to honor that, or change `SvfEffect` so `setParameter()` only writes atomic/plain pending parameter values and the audio thread applies them at the start of `process()`.

### The portability gate claims `core/` has no platform dependency but does not check the dependency it actually admits

Finding-ID: AUDIT-BARRAGE-codex-02
Status:     open
Severity:   medium
Surface:    scripts/check-portability.sh:28-34, core/primitives/svf-primitive.h:5-10

The portability script’s core-boundary gate only greps for `juce`, `libDaisy`, `daisy_seed`, `<Audio.h>`, and `<Arduino.h>`. It will print `OK: core/ is platform-independent` even though `core/primitives/svf-primitive.h:5` publicly includes `Filters/svf.h` from DaisySP and `core/primitives/svf-primitive.h:54` stores `daisysp::Svf` directly. The implementation may intentionally classify DaisySP as portable DSP rather than a platform header, but the gate as written does not verify that invariant; it only verifies a few forbidden strings.

The blast radius is medium because this is a governance/CI false-confidence issue rather than an immediate runtime crash. A future change could introduce a nonportable dependency through the same public-core include path and still pass unless it happens to match the narrow grep list. A better gate would encode the actual invariant: either allowlist DaisySP explicitly and fail on all other external/platform includes in `core/`, or split the check into “forbidden platform headers” and “approved third-party DSP headers” so the reported result matches what was actually validated.
