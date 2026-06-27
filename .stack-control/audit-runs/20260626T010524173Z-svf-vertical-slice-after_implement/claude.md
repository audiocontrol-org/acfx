I'll audit this chunk — the core DSP spine (concept, parameter model, SVF effect, host boundary). Let me anchor each finding to the diff.

### Thread-ownership comment over-claims: prepare()/reset() mutate coefficients off the audio thread with no synchronization

Finding-ID: AUDIT-BARRAGE-claude-01
Status:     open
Severity:   high
Surface:    core/effects/svf/svf-effect.h:16-22, 98-119, 152-156

The class header comment claims (lines 16-22): *"the actual filter coefficients are mutated on exactly one thread, with no torn-coefficient data race against process() — an invariant the core encodes itself rather than asking every adapter to honor."* The member comment (lines 152-153) reinforces it: applied state is *"owned by the audio thread (read/written only in prepare/reset/applyPending)."*

This is only true for the `setParameter` → `applyPending` path. The actual coefficient mutation happens in `applyCutoff/applyResonance/applyMode` (lines 110-119), and those are **also** called from `prepare()` (line 89, via `applyAll()`) and `reset()` (line 95). `prepare()` is documented in `process-context.h` as running *"before audio starts and on any device change"* — i.e. on the audio-device/message thread, **not** the audio thread. So on a device change while audio is live, `prepare()`/`reset()` mutate `svf_` internal state, `sampleRate_`, and `numChannels_` concurrently with `process()` — a genuine data race the comment claims the core has eliminated.

Blast radius: the audit's unattended-agent framing makes this high. An adapter author who reads "the core encodes this invariant itself" reasonably concludes `reset()` is safe to wire to a UI "reset filter" button or a settings change while the stream runs. The setParameter path was hardened (round-2 govern findings), but the comment now overstates the guarantee to cover prepare/reset, which remain adapter-discipline-dependent. Fix: scope the claim explicitly to `setParameter`, and state the real invariant for prepare/reset ("must be called only when the audio stream is stopped — the adapter owns this") rather than asserting single-thread ownership the code doesn't enforce.

### Channel count silently clamped to 8; channels beyond kMaxChannels pass through unfiltered

Finding-ID: AUDIT-BARRAGE-claude-02
Status:     open
Severity:   medium
Surface:    core/effects/svf/svf-effect.h:86-87, 70-78, 122

`prepare()` clamps `numChannels_ = ctx.numChannels < kMaxChannels ? ctx.numChannels : kMaxChannels` (line 86), where `kMaxChannels = 8` (line 122). `process()` then iterates `channels = min(io.numChannels(), numChannels_)` (line 71). The combined effect: if a host opens a stream with more than 8 channels, channels 8..N are **silently passed through unfiltered** — the inner loop never touches them, and nothing reports the truncation.

This is the "fallback that hides a failure mode" pattern the project commandments forbid: a downstream operator on a >8-channel host gets a filter that quietly stops filtering past channel 8, with no error and no log. Compare `parameter.h`, which asserts on invalid invariants. There is no analogous guard here. Blast radius is medium — most adopters run mono/stereo, but a multichannel adopter would hit silent partial processing that's hard to diagnose. A reasonable fix: assert `ctx.numChannels <= kMaxChannels` in debug, and document the hard cap on the `ProcessContext`/effect contract so the limit is a declared constraint rather than a silent clamp.

### setParameter swallows an out-of-range ParamId with no debug assert, unlike the rest of core

Finding-ID: AUDIT-BARRAGE-claude-03
Status:     open
Severity:   medium
Surface:    core/effects/svf/svf-effect.h:82-85

`setParameter` guards `if (i >= kNumParams) return;` with the comment *"out-of-range id: a programming error; no silent state change"* (lines 82-84). The comment correctly identifies this as a programming error — but the handling is a silent early return with no debug assertion. This is inconsistent with `parameter.h`, which uses `assert(...)` to surface invariant violations in debug (e.g. lines 60-61, 84-85). An adapter that mis-maps its parameter id space (off-by-one, wrong base index, stale enum) will have its writes silently discarded, and the effect will appear "stuck" with no signal pointing at the real cause.

Throwing is correctly avoided here (this path is callable from MIDI/UI and is `noexcept`), but an `assert(i < kNumParams && "ParamId out of range")` would catch the adapter bug in debug builds at zero release cost, matching the codebase's own established pattern. Blast radius is medium: a silently-dropped parameter write is exactly the kind of quietly-plausible wrong behavior an unattended integrator would build around rather than diagnose.

### Invalid-descriptor handling is inconsistent: log skew asserts, discreteCount<2 silently self-corrects

Finding-ID: AUDIT-BARRAGE-claude-04
Status:     open
Severity:   low
Surface:    core/dsp/parameter.h:39-42, 70-73, 56-58, 87-89

The two invalid-descriptor paths in `parameter.h` are handled differently. A logarithmic parameter with `min <= 0` triggers `assert(d.min > 0.0f && d.max > d.min ...)` (lines 56-58, 87-89) — fail-loud in debug. But a discrete parameter with `discreteCount < 2` (which the descriptor doc on `parameter.h:30` says is invalid: *">= 2 when kind == discrete"*) is **silently coerced** to 2 via `d.discreteCount < 2 ? 2 : ...` (lines 39, 70) with no assert.

This is a hygiene inconsistency in a core primitive that future effects will reuse: one invariant violation is caught, the structurally-identical one is silently papered over. An effect author who ships a descriptor with `discreteCount = 0` (e.g. forgot to set it) gets a 2-bucket quantizer instead of a diagnostic. Low blast radius for the current SVF (its descriptors are valid), but it compounds as the descriptor table is the declared single source of truth (SC-006) that every adapter consumes. Fix: assert `d.discreteCount >= 2` on the discrete path to match the logarithmic path.

### process() before prepare() silently passes audio through unprocessed (numChannels_ defaults to 0)

Finding-ID: AUDIT-BARRAGE-claude-05
Status:     open
Severity:   low
Surface:    core/effects/svf/svf-effect.h:154, 70-78

`numChannels_` defaults to `0` (line 154). If `process()` runs before `prepare()` — a contract violation, but possible during adapter bring-up or a missed lifecycle call — then `channels = min(io.numChannels(), 0) = 0`, the processing loop is skipped entirely, and the block passes through silently unfiltered. Likewise `ProcessContext` (`process-context.h:10-13`) has no default member initializers, so an adapter that constructs `ProcessContext ctx;` and forgets a field hands garbage `sampleRate` into the coefficient math.

Both are low-severity because they're guarded by the documented "adapter calls prepare first" contract, but both are the silent-no-op / silent-garbage shape the project's fail-loud ethos discourages. A debug assert in `process()` (`assert(numChannels_ > 0 && "process() before prepare()")`) and zero/sentinel default initializers on `ProcessContext` would surface the misuse instead of producing plausible-looking-but-wrong audio. Blast radius is low — it manifests only on a lifecycle bug — but when it does, "filter does nothing" with no error is a slow diagnosis.

---

I checked the cross-thread atomic handoff carefully (`setParameter` relaxed-store-then-release-flag, `applyPending` acquire-exchange-then-relaxed-load) and it is **correct** — the release/acquire on `pendingDirty_` establishes the happens-before that publishes the relaxed `pendingNorm_` value, with no lost-update or torn-value window. I also verified the discrete/continuous normalize↔denormalize round-trips (mid-bucket landing for discrete is sound), the `clampedCutoff` bound (`0.32 * sr` is safely under DaisySP's `sr/3`), the span polyfill's const-conversion path, and the single-virtual-dispatch design of `EffectNode`. Those are clean. The five findings above are the silent-failure and doc-accuracy seams worth the operator's attention.