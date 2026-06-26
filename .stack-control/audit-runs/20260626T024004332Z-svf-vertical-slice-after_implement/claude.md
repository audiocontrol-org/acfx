I'll walk this chunk's diff (the platform-independent DSP core + the Teensy toolchain) and surface what I find.

### Cutoff parameter's advertised 20 kHz maximum is silently unreachable at standard sample rates

Finding-ID: AUDIT-BARRAGE-claude-01
Status:     open
Severity:   medium
Surface:    core/effects/svf/svf-effect.h — `kParams` cutoff descriptor (`20.0f, 20000.0f`) vs `clampedCutoff()` (`const float maxFreq = sampleRate_ * 0.32f;`)

The cutoff descriptor declares `max = 20000.0f` and is described as "the single source of parameter truth (SC-006)" that "drives every adapter (FR-003)." But `clampedCutoff()` caps the actual coefficient at `sampleRate_ * 0.32f`. `0.32 * SR >= 20000` only when `SR >= 62500`, so at the two most common rates — 44.1 kHz (cap ≈ 14112 Hz) and 48 kHz (cap ≈ 15360 Hz) — roughly the top of the advertised log range is dead: turning the cutoff knob anywhere above ~15.3 kHz at 48 kHz produces no audible change, yet every adapter, DAW automation lane, and UI built from the descriptor renders 20 kHz as reachable.

Blast-radius: a downstream adapter/host trusts the descriptor's `max` to draw its range and map automation. The mismatch is sample-rate-dependent (fine at 96 kHz, broken at 48 kHz), so it won't show up in a high-SR test and will surface as "the filter stops responding near the top" in normal use. It does not crash or corrupt — hence medium, not high — but it's a genuine contract/behavior divergence in the very table the spec designates as authoritative.

A reasonable fix is to derive the descriptor's effective max from the prepared sample rate (or clamp into the descriptor band and document that the top of the range is SR-bounded), so the advertised range and the reachable range agree rather than silently diverging.

### `prepare()` silently truncates channel count beyond `kMaxChannels` with no diagnostic

Finding-ID: AUDIT-BARRAGE-claude-02
Status:     open
Severity:   medium
Surface:    core/effects/svf/svf-effect.h — `prepare()` (`numChannels_ = ctx.numChannels < kMaxChannels ? ctx.numChannels : kMaxChannels;`) and `process()` channel clamp

`ProcessContext::numChannels` is documented as "upper bound on AudioBlock::numChannels." `prepare()` silently clamps it to the compile-time `kMaxChannels = 8`, and `process()` then computes `channels = min(io.numChannels(), numChannels_)`. A host that prepares the effect with more than 8 channels gets the first 8 filtered and the remainder passed through unfiltered (in-place, so identity), with no error, log, or assertion. `prepare()` runs while the stream is stopped (it is `noexcept` but off the audio thread), so it is exactly the place where an unsupported-channel-count condition *could* be signalled, yet it instead degrades quietly.

This is the "fallback that hides a failure mode" the project guidelines call a bug-factory: a 16-channel host silently loses half its channels rather than learning the effect can't honor the request. Blast-radius is bounded (>8-channel hosts are uncommon for an SVF), which keeps it at medium, but the silent truncation is the kind of defect an unattended integrator would never notice until audio is wrong in production.

A reasonable fix is to surface the limit — either an explicit error/diagnostic path for `ctx.numChannels > kMaxChannels`, or a documented contract that the effect supports at most `kMaxChannels` and the adapter must not exceed it (mirroring how the thread-ownership note already pushes quiescence onto the adapter).

### `clampedCutoff()` hardcodes `20.0f` floor, duplicating the cutoff descriptor's `min`

Finding-ID: AUDIT-BARRAGE-claude-03
Status:     open
Severity:   low
Surface:    core/effects/svf/svf-effect.h — `clampedCutoff()` (`if (f < 20.0f) f = 20.0f;`)

The lower clamp in `clampedCutoff()` is the literal `20.0f`, which is the same value as `kParams[kCutoff].min`. The two are now coupled by coincidence rather than by reference: if the cutoff descriptor's `min` is ever changed (say to 10 Hz), the descriptor and the runtime clamp silently disagree, and the bottom of the advertised range becomes unreachable in the same way Finding-01 describes for the top — except this one has no sample-rate excuse, it's a stale duplicated constant.

Blast-radius is low because today the values match and a `denormalize` of `norm=0` already yields exactly `min`, so the clamp is currently a no-op at the bottom. The risk is purely future drift / magic-number hygiene. A reasonable fix is to reference `kParams[kCutoff].min` (it's `constexpr`) instead of repeating the literal, so the floor tracks the single source of truth automatically.

### `denormalize`/`normalize` re-coerce an invalid `discreteCount` at runtime, masking a descriptor that the build-time guard already rejected

Finding-ID: AUDIT-BARRAGE-claude-04
Status:     open
Severity:   low
Surface:    core/dsp/parameter.h — `denormalize` and `normalize` discrete branches (`const int count = d.discreteCount < 2 ? 2 : ...`)

`isValidDescriptor()` (and the `SvfEffect` `static_assert`) already make `discreteCount < 2` a *compile* error for any descriptor in a constexpr table. Yet both `denormalize` and `normalize` defensively re-coerce `discreteCount < 2` up to `2` at runtime. This is harmless defense-in-depth for the values that exist today, but it's worth noting as a small inconsistency: the runtime path silently *repairs* a state the contract says cannot occur, rather than relying on the enforced invariant. If a future caller constructs a `ParameterDescriptor` dynamically (outside a static table, bypassing the `static_assert`) with `discreteCount = 0`, it will be silently treated as a 2-bucket parameter instead of being caught.

Blast-radius is low — no current call site can reach it, and the behavior is benign quantization rather than a crash or NaN. I flag it only because it's the seam where the "build-time guard" story and the "runtime guard" story overlap: the comment in `parameter.h` frames the debug asserts as "defense in depth," but these discrete clamps are *release-path* silent coercions, not asserts, so they don't fail loud even in debug. A consistent choice (assert in debug, document the trust boundary) would make the invariant ownership clearer than a silent `< 2 ? 2` fixup.

---

I checked the cross-thread parameter handoff (`setParameter` relaxed-bits + release-dirty / `applyPending` acquire-exchange) and it is correct for the published single-consumer model — the acquire on the dirty atomic synchronizes-with the producer's release, so the relaxed bits load is well-defined; concurrent producers only ever cause a benign lost-update of a fully-published value, never a torn read. The `clamp01` NaN guard maps NaN/-inf→0 and +inf→1 as documented, and `setParameter(NaN)` resolves to a safe 20 Hz floor. The discrete round-trip (`normalize` mid-bucket → `denormalize` floor) is index-stable for the 3-mode table. The `span` C++17 polyfill and the C++17 concept-degradation in `effect.h` are shape-correct. My four findings above are the divergences worth triaging; the rest of the chunk held up.