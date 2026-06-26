I'll audit this chunk — the three files with diffs (`scripts/check-portability.sh`, `tests/CMakeLists.txt`, `tests/core/no-allocation-test.cpp`), noting that `tasks.md` is listed in scope but no diff was provided.

### Portability gate's `#ifdef`-fork check only scans `core/effects/`, leaving `core/primitives/` unguarded

Finding-ID: AUDIT-BARRAGE-claude-01
Status:     open
Severity:   medium
Surface:    scripts/check-portability.sh:50-55 (check 4)

Check 4 enforces the "one-source-many-targets" invariant (SC-001/SC-005) by grepping for per-target `#ifdef` forks, but it scopes the scan to `core/effects/` only:
```bash
if grep -rEn '#if(def)?.*(JUCE|DAISY|TEENSY|__arm__|DESKTOP)' core/effects/ ; then
```
The DSP primitive the effect is built on lives in `core/primitives/svf-primitive.h` (per the cross-chunk file lists). A per-target fork like `#ifdef DAISY` placed in the primitive is invisible to this gate. Worse, check 2 (no platform headers) wouldn't catch a bare `#ifdef DAISY` either, since `DAISY` is not in its header pattern — so a primitive-level target fork escapes *both* gates while the script reports green.

The stated invariant is that the entire platform-independent DSP core is one source compiled for many targets, not just the effect wrapper. Blast radius: an adopter or unattended agent trusts a green portability gate as proof the core has no per-target branches; a fork introduced in `core/primitives/` would merge undetected, silently breaking the "same source on every target" guarantee the slice is built to demonstrate. Fix: scan `core/` (or at minimum `core/effects core/primitives`) rather than `core/effects/`.

### Case-sensitive `juce` grep in checks 2/3 is inconsistent with the uppercase `JUCE` in check 4 and misses `<JuceHeader.h>` / `JUCE_*` macros

Finding-ID: AUDIT-BARRAGE-claude-02
Status:     open
Severity:   medium
Surface:    scripts/check-portability.sh:28,36 (checks 2 and 3) vs. line 50 (check 4)

Checks 2 and 3 match the token lowercase and case-sensitively:
```bash
grep -rEn 'juce|libDaisy|daisy_seed|<Audio\.h>|<Arduino\.h>' core/        # check 2
grep -rEn 'juce|processor-node' adapters/daisy adapters/teensy           # check 3
```
while check 4 matches it uppercase: `(JUCE|DAISY|TEENSY|...)`. The same JUCE token is therefore treated differently by the same script. The lowercase, case-sensitive pattern catches modern modular includes (`<juce_dsp/juce_dsp.h>`) but misses the Projucer-style umbrella include `#include <JuceHeader.h>` ("Juce", not "juce") and any `JUCE_*` preprocessor macro usage in a core file — both of which would constitute a platform leak into `core/`.

This is a quality gate whose green status is taken as "core is platform-independent." Blast radius: a core file that pulls in `<JuceHeader.h>` or uses `JUCE_` macros passes check 2 and the script reports the core clean, giving false confidence that the platform-independence constitution rule (IV) holds when it doesn't. Fix: add `-i` to checks 2 and 3 (or normalize all three checks to the same case-insensitive pattern set) so casing variants can't slip through.

### Adapter-link check is a bare substring match, not a verification that the adapter actually links `acfx_core`

Finding-ID: AUDIT-BARRAGE-claude-03
Status:     open
Severity:   low
Surface:    scripts/check-portability.sh:56-62

The "every adapter links the same acfx_core" gate is:
```bash
if ! grep -rq 'acfx_core' "adapters/$adapter/CMakeLists.txt" 2>/dev/null; then
```
This passes whenever the literal `acfx_core` appears *anywhere* in the file — a comment mentioning it, an `add_dependencies`, a `target_include_directories`, or even the substring inside an unrelated token like `acfx_core_tests`. It does not confirm a `target_link_libraries(<adapter> ... acfx_core ...)` directive. An adapter whose CMakeLists references `acfx_core` only in a comment (or links a stale copy) would report OK while not actually linking the shared core.

The script's closing banner asserts "every adapter links the same acfx_core" — a stronger claim than the check substantiates. Blast radius is bounded (a real adapter that didn't link core wouldn't compile, so this mostly fails closed in practice), hence low, but the gate's wording overstates what it proves. A more honest check would grep within the `target_link_libraries` context, or the banner should be softened to "references acfx_core."

### `find` errors are suppressed, so a partial/empty tree yields a false "all within budget" pass

Finding-ID: AUDIT-BARRAGE-claude-04
Status:     open
Severity:   low
Surface:    scripts/check-portability.sh:19-24

The file-size loop reads from:
```bash
done < <(find core host adapters tests -type f \( -name '*.h' -o -name '*.cpp' \) 2>/dev/null)
[ "$fail" -eq 0 ] && note "  OK: all source files within budget"
```
The `2>/dev/null` swallows the error when any of `core host adapters tests` is missing (partial checkout, run from an unexpected layout, or a renamed dir). With no input lines the loop body never runs, `fail` stays 0, and the script prints "OK: all source files within budget" — a vacuous pass that looks identical to a real pass. The same silent-empty failure mode affects checks 2/3/4, whose `grep` returning non-zero (no match) is indistinguishable from "directory absent."

Blast radius: low in normal CI (the tree is present), but the gate gives no signal that it scanned *nothing*, so a structural regression that moves/empties these dirs would read as green. A cheap guard is to assert the four roots exist (or that the find produced ≥1 file) and hard-fail otherwise, so "scanned nothing" can never masquerade as "scanned and clean."

### No-allocation test covers only the `kCutoff` parameter path and never exercises `setParameter` through the `EffectNode` boundary

Finding-ID: AUDIT-BARRAGE-claude-05
Status:     open
Severity:   low
Surface:    tests/core/no-allocation-test.cpp:31-32,46-56

The bare-effect case asserts allocation-freedom for `setParameter` but only ever sets `kCutoff`:
```cpp
fx.setParameter(ParamId{SvfEffect::kCutoff}, (i % 2 == 0) ? 0.25f : 0.75f);
```
Other parameters (resonance, and especially a filter-mode/type parameter, which is the classic place a coefficient table or branch could trigger a one-time allocation) are never driven through `setParameter` in the measured region. The `EffectNode<SvfEffect>` case (lines 46-56) calls `processBlock` but never calls `setParameter` at all, so the host-boundary parameter path has zero allocation coverage.

The test's comment claims it proves "parameter changes on the audio thread must also be allocation-free," but it proves that only for one parameter on the bare effect. Blast radius is low — the dominant `process()` path is well covered across four block sizes — but a mode-switch that allocated on first use would pass this test green. Fix: iterate the parameter id over the full `kCutoff`/resonance/mode set in the measured loop, and add a `setParameter` call inside the `EffectNode` loop so the boundary's param path is exercised too.

### `tasks.md` is listed in chunk scope but no diff was provided, so task-closure claims (T018/T037/T038) can't be cross-checked

Finding-ID: AUDIT-BARRAGE-claude-06
Status:     open
Severity:   informational
Surface:    specs/svf-vertical-slice/tasks.md (declared in scope, absent from the diff)

The chunk header lists `specs/svf-vertical-slice/tasks.md` among the files in scope, but the Diffs section contains no entry for it. The script header cites T037/T038 and the test header cites T018 as the tasks these artifacts close. Without the `tasks.md` diff in this chunk I cannot verify that those task rows were actually marked complete, that their acceptance text matches what these files implement, or that the verified/manual split (referenced in commit `f88525b`) is honestly reflected.

This is not a code defect — it's a coverage gap in what was handed to this audit lane. Flagging so the operator knows a scoped surface arrived without content; if `tasks.md` did change, its diff should be routed to a lane for the T018/T037/T038 closure claims to be checked against these implementations.

---

I checked the three diffed files for: gate completeness (regex scope, casing, substring-vs-structure matching), silent-failure modes in the bash control flow, the no-allocation test's measured-region discipline (correctly captures the count before any assertion macro runs — that part is sound), doctest registration fallback (graceful when `doctest_SOURCE_DIR` is unset), and parameter-path coverage. The CMake test target itself is clean. The strongest signals are findings 01 and 02 — both are holes in portability gates whose green status is load-bearing for the "platform-independent core" and "one source, many targets" guarantees this slice exists to prove.