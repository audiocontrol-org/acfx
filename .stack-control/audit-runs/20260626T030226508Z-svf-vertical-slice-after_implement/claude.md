I'll audit this chunk carefully — the DaisySP wrapper, the host boundary header, and the portability gate script.

That ScheduleWakeup was an inadvertent call — disregard it; this is a one-shot audit, not a loop.

Here are my findings for chunk `a05731de7f2cca0b`.

---

### `SvfPrimitive::reset()` silently reverts cutoff and resonance to DaisySP defaults (200 Hz / 0.5), not just clearing state

Finding-ID: AUDIT-BARRAGE-claude-01
Status:     open
Severity:   medium
Surface:    core/primitives/svf-primitive.h:43-44 (`reset()`), with cross-file dependency on core/effects/svf/svf-effect.h (chunk e7b284327d06692a)

`reset()` is documented as "Re-initialize to a cleared-but-prepared state (DaisySP's Init clears state)" — implying it only clears the filter's internal history/integrator memory. But it is implemented as `svf_.Init(sampleRate_)`, and DaisySP's `Svf::Init` does far more than clear state: it resets `fc_` to its hardcoded default (200 Hz) and `res_` to 0.5, then calls `SetFreq(fc_)`/`SetRes(res_)`. So a `reset()` discards any caller-configured cutoff and resonance and reverts the *coefficients*, not merely the state. The header comment understates this materially.

Crucially, `SvfPrimitive` keeps no memory of the last-applied freq/res — `setFreq`/`setRes` forward directly to `svf_` (lines 30, 33) and store nothing. So the primitive *cannot* restore coefficients itself after a `reset()`; the only path back to the configured cutoff is the caller re-pushing parameters. Blast radius: when a host calls `reset()` (DAW transport stop/restart, sample-rate change, voice steal), the filter silently snaps to a 200 Hz / 0.5-res lowpass-family configuration and stays there until the next `setParameter` lands — an audible, hard-to-trace defect for an unattended adopter. Whether this actually ships depends on `SvfEffect::reset()` re-applying its pending freq/res after calling the primitive's `reset()`; that lives in another chunk and must be verified. A reasonable fix: have `SvfPrimitive` cache the last freq/res and re-apply them inside `reset()` after `Init`, **or** rewrite the doc to state explicitly that `reset()` reverts coefficients and the caller MUST re-push parameters afterward.

---

### `check-portability.sh` Check 3 fails *open* when an MCU-adapter directory is absent

Finding-ID: AUDIT-BARRAGE-claude-02
Status:     open
Severity:   medium
Surface:    scripts/check-portability.sh:33-39 (Check 3, SC-007 gate)

Check 3 is `if grep -rEn 'juce|processor-node' adapters/daisy adapters/teensy ; then … fail … else … OK`. `grep` returns exit 1 when there is no match (→ "OK") but also returns exit 2 when a target path does not exist. The script runs under `set -u` only (line 11) — not `set -e`/`pipefail` — so a non-zero grep from a *missing* directory is not distinguished from a clean "no match," and the `else` branch prints `OK: daisy + teensy reference neither JUCE nor ProcessorNode`. If `adapters/daisy` or `adapters/teensy` is renamed, moved, or deleted by a future refactor, this safety gate goes green while actually checking nothing.

This matters because the gate's entire purpose is to enforce SC-007 unattended in CI; a gate that passes when its target surface vanishes gives false assurance precisely when something structural has changed. Contrast Check 4 (lines 50-55), which correctly fails *closed*: `if ! grep -rq 'acfx_core' "adapters/$adapter/CMakeLists.txt" 2>/dev/null` treats a missing file as a failure. The fix is to make Checks 2 and 3 fail closed too — e.g. assert each target path exists before grepping (`[ -d adapters/daisy ] || { note FAIL; fail=1; }`), or distinguish grep exit 1 (clean) from exit ≥2 (error) explicitly.

---

### Check 4's `#ifdef`-fork grep is scoped only to `core/effects/`, leaving the new `core/primitives/` DaisySP wrapper unguarded

Finding-ID: AUDIT-BARRAGE-claude-03
Status:     open
Severity:   low
Surface:    scripts/check-portability.sh:42-48 (Check 4, SC-001/SC-005), relative to core/primitives/svf-primitive.h

Check 4 forbids per-target `#ifdef` forks with `grep -rEn '#if(def)?.*(JUCE|DAISY|TEENSY|__arm__|DESKTOP)' core/effects/`. The grep root is hardcoded to `core/effects/`. But this very chunk introduces `core/primitives/svf-primitive.h` — a new platform-DSP wrapper sitting outside `core/effects/`. A per-target DSP fork (`#ifdef __arm__` selecting a different filter implementation, a stubbed `process()` on desktop, etc.) inside `core/primitives/` or `core/dsp/` would pass the gate undetected, even though that is exactly the kind of "one-source-many-targets" violation the check exists to catch (Scenario E).

The blast radius is low today because no such fork exists, but the gate's coverage no longer matches the source tree it is meant to protect: the primitives layer is now a legitimate place for platform divergence to creep in. A one-line fix widens the root to all of `core/` (`grep -rEn '…' core/`), which is consistent with Check 2 already scanning `core/` wholesale.

---

### `sampleRate_ = 48000.0f` default acts as a silent fallback if `process()` runs before `init()`

Finding-ID: AUDIT-BARRAGE-claude-04
Status:     open
Severity:   low
Surface:    core/primitives/svf-primitive.h:54 (`float sampleRate_ = 48000.0f`)

The member initializer `sampleRate_ = 48000.0f` (line 54) plus the default-constructed `daisysp::Svf svf_{}` (line 53) mean that if `process()` or `reset()` is ever reached without a prior `init()`, the filter runs against a hardcoded 48 kHz assumption rather than surfacing the missing-initialization. Project guidelines (CLAUDE.md, repo standards) call out fallbacks/default data as bug-factories that hide failure modes: a wrong-but-plausible 48 kHz silently masks a `prepare()`-ordering bug instead of making it visible.

The RT-safety constraint (`noexcept`, no throws in the audio path) legitimately rules out throwing inside `process()`, so the right surface for enforcement is the prepare path or a debug assertion (`assert(sampleRate_ initialized)`), not the hot path. The contract that `init()` precedes `process()` is real and elsewhere relied upon (the `setFreq` comment names `SvfEffect` as the disciplined caller); making the unpepared state a hard failure in debug — rather than a quietly-functional 48 kHz default — would keep the no-fallback discipline without violating RT-safety. Low severity because well-behaved callers (`prepare()` → `init()`) never hit it; flagged because the default itself is the bug-factory pattern the project explicitly forbids.

---

These four are what I found worth surfacing. I specifically checked: the `EffectNode`/`ProcessorNode` single-virtual-dispatch design (clean — by-value storage, no extra indirection, `final` with virtual dtor), the thread-handoff contract comment (correctly delegates atomicity to the wrapped effect; can't be enforced by a concept, but that's documented), the CMake `INTERFACE` wiring (correct include root and `acfx_core` link), and the DaisySP-in-`core/` portability argument (intentional and consistent with the gate's grep patterns, already-dispositioned design decision — not re-litigated). The `reset()` coefficient-loss finding is the one I'd most want a human to chase down against `SvfEffect`.