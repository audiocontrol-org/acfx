I'll audit the diff carefully for correctness bugs, design issues, missing edge cases, and policy violations.

---

### `setParameter` thread ownership undocumented despite commit bd79479 claiming to address it

Finding-ID: AUDIT-BARRAGE-claude-01
Status:     open
Severity:   high
Surface:    host/processor-node/processor-node.h:21,37

`ProcessorNode::setParameter` and `EffectNode::setParameter` carry no documentation about which thread may call them. The commit `bd79479` is titled "Address govern findings: RT-safety, thread ownership, doc drift," but the only apparent path for parameter updates — the `setParameter` virtual on `ProcessorNode` — is still silent on thread ownership. In a DAW or workbench context, `processBlock` fires on the real-time audio thread while parameter changes originate from the UI or MIDI thread. A caller who reads this header gets no signal that calling `setParameter` concurrently with `processBlock` is either safe or unsafe, and `SvfEffect::setParameter` (in the unauditable `svf-effect.h`) contains no visible synchronization. The blast-radius: every adapter that uses `EffectNode` is implicitly racy on `setParameter` without realizing it. A one-line doc comment on the `ProcessorNode` method stating the required thread contract (e.g., "must be called from the same thread as `processBlock`, or under external synchronization") would close the gap.

---

### `normalize()` calls `std::clamp` before asserting logarithmic preconditions, inverting the guard order relative to `denormalize()`

Finding-ID: AUDIT-BARRAGE-claude-02
Status:     open
Severity:   medium
Surface:    core/dsp/parameter.h:67–79

In `normalize()`, line 67: `plain = std::clamp(plain, d.min, d.max);` executes unconditionally before the switch on `d.skew`. For a logarithmic parameter with a malformed descriptor where `d.min >= d.max` or `d.min <= 0`, `std::clamp(plain, d.min, d.max)` has **undefined behavior** (the C++ standard requires `d.min <= d.max`). The assert at line 74 fires _after_ the UB has already occurred. By contrast, `denormalize()` correctly places the assert for logarithmic preconditions _before_ any computation (lines ~52–54). The inconsistency means `normalize()` silently takes UB in release builds when the descriptor is malformed, while `denormalize()` would have caught the same error earlier. Fix: for `ParamSkew::logarithmic` in `normalize()`, assert the preconditions before calling `std::clamp`, or restructure so the clamp only happens after the skew-specific path is entered.

---

### Silent substitution of `discreteCount = 2` violates the project no-fallback rule

Finding-ID: AUDIT-BARRAGE-claude-03
Status:     open
Severity:   medium
Surface:    core/dsp/parameter.h:38,63

Both `denormalize()` line 38 and `normalize()` line 63 contain:
```cpp
const int count = d.discreteCount < 2 ? 2 : static_cast<int>(d.discreteCount);
```
The struct comment documents `discreteCount >= 2 when kind == discrete` as an invariant. When the invariant is violated (`discreteCount` is 0 or 1), both functions silently substitute `count = 2` and continue. The project CLAUDE.md states explicitly: "Never implement fallbacks or use mock data outside of test code. Throw errors with a description of the missing functionality and/or data instead. Errors let us know that something isn't implemented. Fallbacks and mock data are bug factories." A caller with a malformed descriptor (e.g., a typo in a `constexpr` table setting `discreteCount = 0`) will get silently wrong quantization with no compile-time or runtime signal. This should be an `assert(d.discreteCount >= 2)` (debug gate) or, if this path can be reached with runtime data, a hard error. The silent-2 substitution is precisely the kind of fallback the guidelines forbid.

---

### `core/effects/svf/svf-effect.h` is listed in chunk scope but has no diff

Finding-ID: AUDIT-BARRAGE-claude-04
Status:     open
Severity:   medium
Surface:    core/effects/svf/svf-effect.h (missing from diff)

The chunk metadata lists `core/effects/svf/svf-effect.h` as a file in scope, but no diff for it appears in the "Diffs" section. This file is the SVF effect implementation — the central deliverable of the svf-vertical-slice feature. It is the type `T` used everywhere `ACFX_EFFECT_CONCEPT T` appears, and it is the concrete type instantiated by `EffectNode<SvfEffect>` in all adapters. Its absence from the auditable diff means the Effect concept contract enforcement, the parameter table, the `process()` RT-safety, and the `setParameter()` implementation are all outside this audit's evidentiary base. If the file exists unchanged from before the diff range, it should be explicitly noted so auditors know which pre-existing surfaces are load-bearing. If it is missing from the diff due to a tooling gap, that gap should be closed before the barrage results are accepted as complete.

---

### `check-portability.sh` platform-ifdef check is scoped to `core/effects/` but the prohibition extends to all of `core/`

Finding-ID: AUDIT-BARRAGE-claude-05
Status:     open
Severity:   low
Surface:    scripts/check-portability.sh:47–52

Check 4 in `check-portability.sh` runs:
```bash
if grep -rEn '#if(def)?.*(JUCE|DAISY|TEENSY|__arm__|DESKTOP)' core/effects/ ; then
```
The grep is limited to `core/effects/`. But Constitution IV ("Platform-independent core, thin adapters") and the SC-001/SC-005 invariant apply to all of `core/`, including `core/dsp/`, `core/primitives/`, and any future subdirectory. A platform `#ifdef` in `core/dsp/effect.h` or `core/primitives/svf-primitive.h` would pass the gate undetected. The gate title says "One-source-many-targets" and the comment says "no per-target #ifdef forks of the effect" — the word "effect" may be intentional scoping, but the invariant as stated in the constitution is broader. Either the script should be widened to `core/` (replacing `core/effects/`) or the narrower scope should be explicitly documented alongside the broader invariant so future contributors know where the gate ends.