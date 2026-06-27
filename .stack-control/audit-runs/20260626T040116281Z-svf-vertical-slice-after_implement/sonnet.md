I'll audit chunk `a05731de7f2cca0b` carefully, covering `core/primitives/svf-primitive.h`, `host/processor-node/processor-node.h`, `host/processor-node/CMakeLists.txt`, `scripts/check-portability.sh`, and `external/.gitkeep`.

---

### Gate 2 doesn't detect DaisySP includes in `core/` — platform-independence claim is unverified by CI

Finding-ID: AUDIT-BARRAGE-claude-01
Status:     open
Severity:   medium
Surface:    scripts/check-portability.sh:29-35, core/primitives/svf-primitive.h:5

The platform-independence gate (gate 2) checks `core/` for `juce|libDaisy|daisy_seed|<Audio\.h>|<Arduino\.h>`. It will not match the DaisySP include at `svf-primitive.h:5`: `#include "Filters/svf.h"`. The comment in that file asserts DaisySP is "a platform-independent pure-DSP math library," which is the architectural premise that lets the library live in `core/`. But the gate never verifies that claim — it doesn't check for DaisySP at all.

Blast-radius: if DaisySP's headers transitively pull in any platform header on a future dependency update, `core/` is silently polluted and CI reports no violation. The claim that DaisySP is safe in `core/` is load-bearing for the entire architecture (Constitution IV) but is enforced only by developer discipline, not by any automated check. A reasonable fix: extend the gate-2 regex to also flag DaisySP patterns (`daisysp|Filters/svf\.h`) unless an explicit architectural exemption is documented and the transitive header chain has been audited.

---

### `EffectNode::parameters()` silently imposes a static-method constraint invisible from the interface

Finding-ID: AUDIT-BARRAGE-claude-02
Status:     open
Severity:   medium
Surface:    host/processor-node/processor-node.h:43

```cpp
span<const ParameterDescriptor> parameters() const override { return T::parameters(); }
```

`T::parameters()` is unqualified static-call syntax: it calls `parameters()` as a free static method on `T` with no object. This compiles only if `T::parameters()` is declared `static`. But the base interface (`ProcessorNode`, line 21) declares `parameters()` as a non-static `const` virtual, which is the natural pattern a developer writing a new Effect type would follow. If they write a non-static `parameters()`, the `EffectNode` instantiation produces a cryptic compile error inside the template, not at the call site.

Blast-radius: the hidden static constraint is not encoded in `ACFX_EFFECT_CONCEPT` (defined in `core/dsp/effect.h`, not visible in this chunk). Any developer authoring a second Effect type by modelling it after the `ProcessorNode` interface — a natural, obvious move — will hit a confusing template instantiation error. They would likely either add a spurious `static` keyword or reach for `fx_.parameters()`, creating an inconsistency. Fix: either add `static` to the concept requirement for `parameters()`, or change the call to `fx_.parameters()` if instance-method is acceptable.

---

### `SvfPrimitive::reset()` uses default 48 kHz silently if called before `init()`

Finding-ID: AUDIT-BARRAGE-claude-03
Status:     open
Severity:   low
Surface:    core/primitives/svf-primitive.h:36

`reset()` calls `svf_.Init(sampleRate_)` where `sampleRate_` has a default member initializer of `48000.0f`. If `reset()` is called before `init()` (which sets `sampleRate_`), DaisySP is initialized at 48 kHz regardless of the actual system sample rate. There is no assertion, comment, or ordering contract at the call site guarding against this.

Blast-radius: in an audio context, a sample-rate mismatch causes all cutoff-frequency calculations to be proportionally wrong — a subtle, hard-to-diagnose frequency drift. A `[[nodiscard]]` guard or a debug assertion (`assert(sampleRate_ != 48000.0f || initialized_)`) would make the bad-ordering case detectable. Alternatively, document in the header that `reset()` before `init()` is valid and intentionally uses 48 kHz as the fallback.

---

### `default:` in the mode switch silently maps future `SvfMode` values to lowpass

Finding-ID: AUDIT-BARRAGE-claude-04
Status:     open
Severity:   low
Surface:    core/primitives/svf-primitive.h:44-51

```cpp
switch (mode_) {
case SvfMode::highpass: return svf_.High();
case SvfMode::bandpass: return svf_.Band();
case SvfMode::lowpass:
default:               return svf_.Low();
}
```

Grouping `SvfMode::lowpass` with `default:` suppresses the `-Wswitch`/`-Wswitch-enum` compiler warning that would fire when a new enum value is added without a corresponding case. Adding bandshelf, allpass, or notch outputs later would silently fall through to lowpass. Separating `case SvfMode::lowpass: return svf_.Low();` from the `default:` (or removing `default:` entirely and relying on compiler exhaustiveness) would make unhandled modes a compile-time or at least a lint-time error.

---

### Gate 1 silently passes when source directories are absent (false green on empty find)

Finding-ID: AUDIT-BARRAGE-claude-05
Status:     open
Severity:   low
Surface:    scripts/check-portability.sh:18-23

```bash
done < <(find core host adapters tests -type f \( -name '*.h' -o -name '*.cpp' \) 2>/dev/null)
[ "$fail" -eq 0 ] && note "  OK: all source files within budget"
```

`2>/dev/null` suppresses directory-not-found errors. On a sparse checkout, misconfigured CI matrix, or path mismatch, `find` exits 0 with zero output; the while loop never runs; `fail` stays 0; the gate prints "OK: all source files within budget" having checked nothing. Add a file-count guard:

```bash
count=0
while IFS= read -r f; do
  count=$((count + 1))
  ...
done < <(find ...)
[ "$count" -eq 0 ] && { note "  FAIL: no source files found — check paths"; fail=1; }
```