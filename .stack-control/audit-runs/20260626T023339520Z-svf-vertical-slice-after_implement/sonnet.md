I'll analyze the diff for chunk `a05731de7f2cca0b` covering `core/primitives/svf-primitive.h`, `external/.gitkeep`, `host/processor-node/CMakeLists.txt`, `host/processor-node/processor-node.h`, and `scripts/check-portability.sh`.

---

### `default` label in `SvfPrimitive::process()` suppresses compiler exhaustiveness warning

Finding-ID: AUDIT-BARRAGE-claude-01
Status:     open
Severity:   medium
Surface:    core/primitives/svf-primitive.h:46-51

The `switch` on `SvfMode` merges `case SvfMode::lowpass:` and `default:` into a single arm:

```cpp
case SvfMode::lowpass:
default:
    return svf_.Low();
```

With an `enum class`, both GCC (`-Wswitch`) and Clang (`-Wswitch`) suppress the "unhandled enumerator" diagnostic whenever a `default` label is present. This means adding a future mode (e.g. `SvfMode::notch`, `SvfMode::peak`, `SvfMode::allpass`) will compile cleanly while `process()` silently returns the lowpass tap instead of the new mode's output. The blast radius is audio-correctness: a caller that sets `mode_ = SvfMode::notch` gets lowpass audio with no diagnostic at compile time or assertion at runtime.

A correct fix is to remove the `default` arm entirely and keep only an explicit `case SvfMode::lowpass:` arm. The compiler then flags any new enumerator left unhandled. If the enum grows, `process()` is the one place that must be updated.

---

### Thread-safety contract on `EffectNode::setParameter` enforced only by documentation, not by the type system

Finding-ID: AUDIT-BARRAGE-claude-02
Status:     open
Severity:   medium
Surface:    host/processor-node/processor-node.h:29-34 and 43

The comment block at lines 29–34 documents that `setParameter` may be called concurrently with `processBlock` and that "the wrapped Effect is responsible for the cross-thread handoff." `EffectNode::setParameter` at line 43 delegates unconditionally to `fx_.setParameter(id, n)` — there is no wrapper, no static assertion, and no concept requirement that `T` exposes atomic semantics. Any future `Effect` implementation that skips the atomic pending-value pattern will satisfy `ACFX_EFFECT_CONCEPT` (which enforces interface shape, not threading correctness) and link cleanly, while introducing a data race between the UI/MIDI thread and the audio thread.

The blast radius is a real-time thread safety defect: an adopter implementing a second effect (e.g. a delay, EQ) who doesn't read this comment is likely to write a plain `float` assignment for parameter changes. That's UB under the C++ memory model on every real target (desktop, Daisy, Teensy). The fix doesn't require changing the current code, but should add a static-assert or concept refinement that at minimum documents the requirement in code rather than a comment. A `ParameterWriter<T>` concept with a `set_atomic` requirement, or a helper base that provides the atomic field, would make the contract structural.

---

### Portability gate check 4 greps for `acfx_core` but CMake aliases use `acfx::core`

Finding-ID: AUDIT-BARRAGE-claude-03
Status:     open
Severity:   medium
Surface:    scripts/check-portability.sh:51-56

Check 4 verifies that every adapter links the shared core library:

```sh
if ! grep -rq 'acfx_core' "adapters/$adapter/CMakeLists.txt" 2>/dev/null; then
    note "  FAIL: adapters/$adapter does not link acfx_core"
    fail=1
fi
```

CMake best practice is to consume targets via their alias (`acfx::core`, `acfx::host`) rather than the raw target name (`acfx_core`, `acfx_host`). `host/processor-node/CMakeLists.txt:11` already shows the dual declaration pattern (`acfx_host` + `acfx::host`). If any adapter's `CMakeLists.txt` uses `target_link_libraries(... acfx::core ...)` — the alias form — this grep yields a false failure: the adapter does link `acfx_core` transitively, but the gate reports it as missing. The CI build then passes (the adapter compiles and links correctly) while the portability gate reports a failure, creating noise that trains operators to dismiss gate failures. The fix is to extend the grep pattern to accept either form: `grep -rq 'acfx_core\|acfx::core'`.

---

### `check-portability.sh` uses `set -u` but not `set -e`; `grep` error-exit (code 2) is silently treated as "no match"

Finding-ID: AUDIT-BARRAGE-claude-04
Status:     open
Severity:   low
Surface:    scripts/check-portability.sh:14

The script sets `set -u` to catch unset-variable bugs but omits `set -e`. The portability gate checks use `if grep ...` to set `fail=1`. POSIX `grep` exits with code 0 (match found), 1 (no match), or 2 (error — bad regex, unreadable path, etc.). The `if` construct treats both code 1 and code 2 as "false" — a `grep` that fails due to a missing directory (e.g. `adapters/daisy` not present in a partial checkout) is indistinguishable from "no prohibited header found." Check 3 (`grep -rEn ... adapters/daisy adapters/teensy`) would silently pass if those directories don't exist, defeating the gate for that adapter pair. Adding `set -e` would also require restructuring the `if grep` idiom; a lighter fix is to assert the adapter directories exist before grepping them: `[ -d adapters/daisy ] || { note "FAIL: adapters/daisy missing"; fail=1; }`.