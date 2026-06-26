I walked the diff for `svf-vertical-slice`. This is round-3-hardened code; the cross-thread atomic handoff (`pendingBits_`/`pendingDirty_` with release/acquire on the dirty flag, relaxed on the value) is a textbook-correct single-consumer pattern, and the concept/duck-typing split is sound. The findings below are the surfaces that survived that scrutiny — the strongest is a defensive clamp that doesn't actually defend.

### `clamp01` passes NaN/non-finite values straight through, poisoning RT filter state irrecoverably

Finding-ID: AUDIT-BARRAGE-claude-01
Status:     open
Severity:   high
Surface:    core/dsp/parameter.h:27 (`detail::clamp01`), consumed by `denormalize` at :34-58 and `SvfEffect::applyPending`/`applyCutoff`

`clamp01` is `x < 0.0f ? 0.0f : (x > 1.0f ? 1.0f : x)`. For `x = NaN`, both `NaN < 0.0f` and `NaN > 1.0f` evaluate false, so the function **returns NaN unchanged** — the one input class a 0..1 clamp exists to neutralize is the one it lets through. The path is reachable end-to-end: `SvfEffect::setParameter` stores the caller's float bits verbatim (`svf-effect.h:floatBits`), `applyPending` reads them back and calls `denormalize(kParams[kCutoff], NaN)`, whose logarithmic branch computes `min * std::pow(max/min, NaN) = NaN` (parameter.h:52-53). That NaN reaches `SvfPrimitive::setFreq` → `daisysp::Svf::SetFreq`, and a NaN coefficient propagates into the filter's recursive state on the very next sample, corrupting **all future output on that channel until `reset()`** — there is no self-healing.

Blast radius: a single malformed automation value (buggy host automation curves, a denormalized/uninitialized control read, an MCU control loop with a divide-by-zero) silently and permanently destroys the audio path with no error surfaced — exactly the "fallback that hides a failure mode" the project guidelines forbid, except here it's a guard that fails open. An unattended adopter wiring up parameter automation will trust the clamp and never see the corruption coming. Fix: make `clamp01` non-finite-safe, e.g. `return x >= 0.0f ? (x <= 1.0f ? x : 1.0f) : 0.0f;` — because `NaN >= 0.0f` is false, NaN maps to 0, and `±inf` clamp correctly. This is the round-0 self-red-team driver applied: the round-3 atomic hardening is correct, but it faithfully *transports* a poisoned value into the hot path.

### Resonance is forwarded to DaisySP at its exact upper bound with no clamp — asymmetric with the defensive cutoff clamp

Finding-ID: AUDIT-BARRAGE-claude-02
Status:     open
Severity:   medium
Surface:    core/effects/svf/svf-effect.h:159-162 (`applyResonance`) + core/primitives/svf-primitive.h:33 (`setRes`); descriptor at svf-effect.h:48-49

Cutoff is clamped defensively into a safe band before reaching DaisySP (`clampedCutoff()` floors at 20 Hz and caps at `sampleRate_ * 0.32` to honor DaisySP's `f < sr/3` requirement, svf-effect.h:140-148). Resonance gets no such treatment: the descriptor exposes `resonance` over `[0.0, 1.0]` inclusive (svf-effect.h:48), and `applyResonance` forwards the denormalized value straight to `SvfPrimitive::setRes` → `daisysp::Svf::SetRes`. For a state-variable filter the damping term scales as `2·(1−res)`, which reaches **zero at res = 1.0** — undamped self-oscillation / unbounded Q at the user-reachable maximum. The wrapper comment even asserts "`r in [0, 1]` (DaisySP stability bound)" (svf-primitive.h:32), but `1.0` is precisely the value most SVF formulations treat as the open (excluded) end of the stable interval.

Blast radius: a user (or automation) sweeping resonance to max — the natural thing to do with a resonance knob — drives the filter to its instability edge with no guard, while the *adjacent* coefficient (cutoff) is carefully fenced. The asymmetry is the tell: either both bounds are DaisySP's responsibility or neither is, and the code has already decided cutoff is the effect's to enforce. Fix: clamp resonance to a safe sub-unity max (mirroring `clampedCutoff`, e.g. cap at ~0.98) in `setRes` or lower the descriptor `max`, and correct the "[0, 1]" comment to the actually-safe interval. Severity is medium rather than high because some DaisySP builds clamp `res_` internally and the audible result is self-oscillation rather than NaN corruption — but the unguarded asymmetry is a real design defect.

### `clampedCutoff` hardcodes the 20 Hz floor, silently duplicating the descriptor minimum

Finding-ID: AUDIT-BARRAGE-claude-03
Status:     open
Severity:   low
Surface:    core/effects/svf/svf-effect.h:140-148 (`clampedCutoff`) vs descriptor at :46-47

`clampedCutoff()` floors the cutoff at a literal `20.0f` (svf-effect.h:145-146), which is the same value declared as the cutoff descriptor's `min` (`20.0f`, :46). The two are independent magic numbers with no link. If the descriptor minimum is ever retuned (a different effect instance, a wider range, a different unit), the clamp floor silently won't track it, and the descriptor-advertised range and the actually-enforced range diverge — a host UI could let the user dial below the real floor, or vice versa.

Blast radius: low and slow-compounding — today the two values agree so behavior is correct; the risk is future drift between the advertised parameter contract and the enforced one. Fix: derive the floor from `kParams[kCutoff].min` rather than re-typing the literal, keeping the descriptor as the single source of truth (which is the stated FR-003 intent — "one constexpr parameter table drives every adapter").

### C++17 `span` polyfill converting constructor binds any lvalue container and stores a raw pointer — latent dangling-view footgun

Finding-ID: AUDIT-BARRAGE-claude-04
Status:     open
Severity:   low
Surface:    core/dsp/span.h:36-39 (converting constructor, C++17 branch)

The polyfill's converting ctor `template <typename Container, typename = decltype(...data())> constexpr span(Container& c)` accepts *any* lvalue exposing `data()`/`size()` and captures `c.data()` as a non-owning pointer. Current usage is safe because the only thing bound is the `static constexpr kParams` array (svf-effect.h:43, lifetime is the program). But the ctor as written will just as happily bind to a function-local `std::array`, producing a `span` that dangles the instant the local goes out of scope — with no compile error, since the lvalue overload is satisfied. On the C++20 path this is `std::span`, which has the same hazard, so the polyfill at least matches std semantics; the note is that the embedded (Teensy) path ships this footgun without `std::span`'s broader review/lint coverage behind it.

Blast radius: low — no current caller misuses it, and this is a vocabulary-type shim, not a runtime fallback. Flagging it as a channel the polyfill *opens* (channel-enumeration driver): the value/state channel is "any contiguous lvalue," not just the static table it was written for, and only the static-table case has a fixture. A `static_assert` or a deleted rvalue overload would close the gap cheaply if non-static spans are ever constructed in core.