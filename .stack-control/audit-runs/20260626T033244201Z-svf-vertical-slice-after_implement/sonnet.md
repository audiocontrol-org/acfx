### SvfPrimitive::reset() silently drops freq/res — post-reset filter state is under-specified

Finding-ID: AUDIT-BARRAGE-claude-01
Status:     open
Severity:   medium
Surface:    core/primitives/svf-primitive.h:32-33

`reset()` calls `svf_.Init(sampleRate_)`, which reinitializes DaisySP's internal filter state — including coefficients that back the last `setFreq` and `setRes` calls. The wrapper tracks `sampleRate_` and `mode_` but does **not** track the last-set frequency or resonance values. After `reset()` returns, the filter is armed but with whatever post-`Init` defaults DaisySP uses (typically a low default cutoff and zero resonance), not the values the caller had previously configured. The doc comment on line 32 says only "Re-initialize to a cleared-but-prepared state" — it does not say freq/res revert to defaults, so a caller who reads only this header and invokes `reset()` mid-session (e.g., on a DAW stop/start cycle) will get silence or wrong filtering until they rediscover the need to re-apply frequency and resonance. The SvfEffect caller must re-issue `setParameter` for both params after every `reset()`, but that contract is documented nowhere in this header. Either: (a) store `freqHz_` and `resNorm_` and re-apply them inside `reset()`, or (b) document the obligation explicitly in the `reset()` comment and in SvfEffect's implementation.

---

### switch default silently absorbs future SvfMode enumerators as lowpass

Finding-ID: AUDIT-BARRAGE-claude-02
Status:     open
Severity:   medium
Surface:    core/primitives/svf-primitive.h:40-51

The `process()` switch lists `highpass` and `bandpass` explicitly, then falls to `default: return svf_.Low()`. When an enumerator is present, `-Wswitch` (and `-Wswitch-enum`) fires only if the switch has **no** default arm; the `default:` here disables that diagnostic entirely. If `SvfMode` gains a new enumerator (`notch`, `peak`, `allpass`, etc.), the compiler will emit no warning and the new mode will silently produce lowpass output. This is a class of quiet, hard-to-catch regression. The fix is to remove the `default:` arm and instead write `case SvfMode::lowpass: return svf_.Low();` explicitly, letting the compiler enforce exhaustion. If a genuine catch-all is wanted for safety, use `[[unlikely]] default: std::abort();` or a `static_assert`-protected path so the failure is loud rather than silent. Blast-radius: any future mode-extension silently builds and ships the wrong audio path.

---

### check-portability.sh check 3 false-passes when adapter directories are absent

Finding-ID: AUDIT-BARRAGE-claude-03
Status:     open
Severity:   high
Surface:    scripts/check-portability.sh:36-41

```sh
if grep -rEn 'juce|processor-node' adapters/daisy adapters/teensy ; then
```

When `adapters/daisy` or `adapters/teensy` does not exist on disk (fresh checkout of a partial tree, a branch that removed an adapter, or a checkout error), `grep` exits with status 2 (error / no paths). In a bare `if command` test, any non-zero exit — including the "error" status — evaluates as **false**, so the FAIL branch is skipped and "OK: daisy + teensy reference neither JUCE nor ProcessorNode" is printed. The gate that exists specifically to prevent MCU-isolation violations silently passes on a missing MCU adapter tree. The fix: add an explicit existence guard before the grep, e.g.:

```sh
for d in adapters/daisy adapters/teensy; do
  [ -d "$d" ] || { note "  FAIL: $d missing — cannot verify MCU isolation"; fail=1; }
done
```

then run the grep only if both directories exist. Blast-radius: a CI run on a branch that accidentally removes or never had an MCU adapter directory would report the isolation gate as green, defeating SC-007.

---

### DaisySP include in core/ is invisible to the portability check

Finding-ID: AUDIT-BARRAGE-claude-04
Status:     open
Severity:   medium
Surface:    scripts/check-portability.sh:28-34, core/primitives/svf-primitive.h:4

`svf-primitive.h` line 4 includes `"Filters/svf.h"` — a DaisySP header — from inside `core/`. The portability check (check 2) only scans for `juce|libDaisy|daisy_seed|<Audio\.h>|<Arduino\.h>`. The DaisySP pattern is absent from that regex. The project comments ("platform-independent pure-DSP math library") justify the current inclusion, but the check doesn't enforce that justification: any future `#include "daisysp/..."` line added anywhere in `core/` would pass check 2 cleanly, even if DaisySP's header transitively pulls in ARM CMSIS intrinsics or STM32 HAL types (which is realistic in some DaisySP build configurations). The portability gate's invariant should name DaisySP as an allowlisted exception rather than being blind to it. At minimum, the comment in check 2 should note that DaisySP headers are intentionally permitted in `core/primitives/` while all other non-standard third-party headers are not. Without that, a future contributor has no way to know whether adding a second DaisySP header to `core/effects/` is acceptable or a violation.

---

### ProcessorNode::setParameter concurrency contract is undischargeable from the interface

Finding-ID: AUDIT-BARRAGE-claude-05
Status:     open
Severity:   low
Surface:    host/processor-node/processor-node.h:24-28

The comment on `setParameter` states that "The wrapped Effect is responsible for the cross-thread handoff" and explicitly calls out `SvfEffect`'s atomic pending value as the mechanism. This means the thread-safety guarantee is an implementation-side obligation, not an interface-side one. Nothing in `ProcessorNode` itself prevents a conforming implementation of `setParameter` from touching shared state without synchronization and silently violating the documented contract. A future `EffectNode<MyEffect>` where `MyEffect::setParameter` is not thread-safe would compile and link without warning. This is a design information gap rather than a current bug — but it is the kind of gap that surfaces as an audio-thread data race in a new effect six months from now. Consider: (a) a comment that names thread-safety as a **required postcondition** of any `setParameter` implementation (not just a fact about SvfEffect), and (b) a note in any future effect template or test that exercises concurrent `setParameter`/`processBlock` calls.