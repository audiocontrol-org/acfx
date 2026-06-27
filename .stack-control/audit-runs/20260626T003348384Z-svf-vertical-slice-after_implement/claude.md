I'm acting as the audit CLI being fired here — I'll walk this chunk and emit findings directly per the required format.

### svf-effect.h is listed in scope but absent from every diff — the feature's keystone effect type is unauditable (and possibly missing)

Finding-ID: AUDIT-BARRAGE-claude-01
Status:     open
Severity:   high
Surface:    core/effects/svf/svf-effect.h (declared in this chunk's "Files in scope"; no diff present here or in any other chunk's file list)

This chunk's header names `core/effects/svf/svf-effect.h` as in-scope, but no diff for it appears in this chunk, and none of the other-chunk file lists (561c0…, 5d46b…, 6a56b…, b74f5…, d58ba…) contain it either. Yet `SvfEffect` is the load-bearing type of the whole vertical slice: `svf-primitive.h:24` explicitly delegates the cutoff-clamping contract to it ("the caller (SvfEffect) clamps cutoff into that range before calling"), `processor-node.h` exists solely to wrap it as `EffectNode<SvfEffect>`, and `effect.h`'s `Effect` concept is the contract `SvfEffect` must satisfy. The audited surface defines the primitive, the parameter model, the concept, and the host node — but not the effect that joins them.

Blast radius: for a feature literally named *svf-vertical-slice*, the slice is incomplete on the audited surface. If the file genuinely does not exist, `EffectNode<SvfEffect>`, the SVF tests, and the workbench/plugin adapters have no concrete effect to instantiate and the feature does not build — a blocking gap masked by the chunk boundary. If it exists but was excluded from the barrage, then the single most important correctness surface (parameter→primitive wiring, cutoff clamping, reset semantics, allocation-freedom of `process`) received zero review. Either way an operator triaging "0 HIGH on svf-vertical-slice" would be drawing a convergence conclusion over a fleet that never saw the keystone. A reasonable fix: confirm `svf-effect.h` exists, and if so re-run the barrage with it in a chunk; if it does not exist, the slice is unfinished.

### check-portability.sh gate 2/3 use case-sensitive `juce`, so the canonical `JuceHeader.h` include slips past the platform-purity gate

Finding-ID: AUDIT-BARRAGE-claude-02
Status:     open
Severity:   medium
Surface:    scripts/check-portability.sh:24-30 (gate 2), :32-37 (gate 3)

Gate 2 enforces Constitution IV (no platform headers in `core/`) with `grep -rEn 'juce|libDaisy|daisy_seed|<Audio\.h>|<Arduino\.h>' core/`, and gate 3 reuses the lowercase `juce` token for MCU adapters. `grep -E` is case-sensitive by default. JUCE's Projucer-generated umbrella header is `#include <JuceHeader.h>` (capital J-u-c-e) — that string does not contain the lowercase substring `juce`, so a `core/` or `adapters/teensy/` file that pulls in `<JuceHeader.h>` passes both gates clean. (Lowercase module includes like `<juce_audio_basics/...>` do match, which is exactly why the false-negative is easy to miss in casual testing.)

Blast radius: this script is the *explicit, visible* replacement for the forbidden git hooks (Constitution II) and is wired into CI. Its whole value is that a green run certifies the core-is-platform-independent invariant. A case blind spot means the gate can report "OK: core/ is platform-independent" while a JUCE umbrella include sits in core — the precise violation it exists to stop, shipped with a passing gate. The fix is one flag: add `-i` to the `grep` calls in gates 2 and 3 (or broaden the alternation to `[Jj]uce`).

### check-portability.sh gate 4 string-matches `acfx_core` in CMakeLists text instead of checking the link graph — brittle to alias/transitive linkage

Finding-ID: AUDIT-BARRAGE-claude-03
Status:     open
Severity:   medium
Surface:    scripts/check-portability.sh:46-51 (gate 4 per-adapter loop); cross-file with host/processor-node/CMakeLists.txt:11

Gate 4 asserts every adapter links the shared core via `grep -rq 'acfx_core' "adapters/$adapter/CMakeLists.txt"`. This is a literal-substring test over CMake source, not a query of the resolved link graph. Two failure modes: (a) an adapter that links the core through its alias — `target_link_libraries(... acfx::core)` — contains no `acfx_core` substring (the `::` breaks it), so the gate false-FAILS a correctly-wired adapter; (b) an adapter that links the core only *transitively* (e.g. via `acfx::host`, which itself does `target_link_libraries(acfx_host INTERFACE acfx_core)` per `host/processor-node/CMakeLists.txt:11`) also lacks the literal token and false-fails, even though SC-001/SC-005 ("every adapter links the same acfx_core") is in fact satisfied. Conversely a stale comment mentioning `acfx_core` would false-PASS.

Blast radius: this gate underwrites the "one-source-many-targets" success criterion. A grep against CMake text couples a constitutional invariant to a particular spelling of the link directive; a routine refactor to the `acfx::core` alias would turn the gate red with no real regression, eroding trust in the gate (operators learn to ignore it), and the comment-match path lets a genuinely-unlinked adapter pass. A robust fix evaluates the actual dependency closure (e.g. CMake's `--graphviz` output, or a configure-time `get_target_property(... LINK_LIBRARIES)` check) rather than grepping source.

### svf-primitive.h `reset()` re-runs DaisySP `Init`, discarding the configured cutoff/resonance — not just "internal state"

Finding-ID: AUDIT-BARRAGE-claude-04
Status:     open
Severity:   medium
Surface:    core/primitives/svf-primitive.h:34 (`reset()` → `svf_.Init(sampleRate_)`); cross-file dependency on the unseen svf-effect.h

`reset()` is documented as "Re-initialize to a cleared-but-prepared state (DaisySP's Init clears state)" and implements it as `svf_.Init(sampleRate_)`. But DaisySP's `Svf::Init` does more than clear the integrator state — it resets the filter's frequency and resonance to its built-in defaults. So after a `reset()`, any prior `setFreq`/`setRes` configuration is silently lost and the filter reverts to DaisySP's default cutoff until the parameters are pushed again. The `Effect` concept (`effect.h:21`) specifies `reset()` as "clear internal state" — clearing *state*, not *configuration*. This wrapper conflates the two.

Blast radius: whether this produces an audible bug depends entirely on `SvfEffect::reset()` (not in the audited chunk) — if it forwards to `primitive.reset()` without re-applying the current parameter values afterward, then every transport reset / device change snaps the filter cutoff back to the DaisySP default, a clearly wrong behavior an end user would hear. This is the kind of quietly-plausible defect that survives because the wrapper's doc-comment reads correct in isolation. Fix: have `reset()` re-apply the retained `mode_` plus the last `setFreq`/`setRes` after `Init` (store them as members), or document and enforce that the owning effect must re-push all parameters after every `reset()`.

### parameter.h silently coerces a misconfigured discrete descriptor (`discreteCount < 2`) to 2 buckets — a fallback that masks a config error

Finding-ID: AUDIT-BARRAGE-claude-05
Status:     open
Severity:   low
Surface:    core/dsp/parameter.h:42 (`denormalize`), :73 (`normalize`)

`ParameterDescriptor` documents `discreteCount` as ">= 2 when kind == discrete, else 0" (parameter.h:24). Both mapping functions defend against a violation with `const int count = d.discreteCount < 2 ? 2 : static_cast<int>(d.discreteCount);`. A descriptor declared `kind == discrete` with `discreteCount` of 0 or 1 (i.e. a malformed table) is therefore silently treated as a 2-value enumeration rather than surfacing the error. Project guidelines (`CLAUDE.md`: "Never implement fallbacks … Throw errors with a description of the missing functionality instead") make silent fallbacks a flagged bug-factory; here a typo in a constexpr descriptor table becomes an invisible 2-bucket parameter instead of a diagnosable fault.

Blast radius: low — descriptors are compile-time constants authored by the effect, so the blast radius is confined to a developer mis-declaring a table, and the corruption (wrong bucket count) is recoverable once noticed. But because the audio path can't throw, the right shape is the same one this file already uses for the logarithmic invariant: a debug `assert(d.discreteCount >= 2 && "discrete parameter requires discreteCount >= 2")` that compiles out in release, replacing the silent `< 2 ? 2` clamp — so the misconfiguration is caught in tests rather than papered over.

### parameter.h linear path omits the `min < max` guard the logarithmic path enforces — degenerate `min == max` yields NaN from `normalize`

Finding-ID: AUDIT-BARRAGE-claude-06
Status:     open
Severity:   low
Surface:    core/dsp/parameter.h:84-88 (`normalize`, linear case); compare :81-83 (logarithmic asserts), :58-62 (`denormalize`)

The logarithmic branches of both `denormalize` and `normalize` assert `d.min > 0.0f && d.max > d.min`, but the linear branches assert nothing. For a continuous-linear descriptor with `min == max` (a degenerate but not obviously-illegal table), `normalize` computes `(plain - d.min) / (d.max - d.min)` = `0.0f / 0.0f` = NaN, which then flows out to the host as a parameter value. The logarithmic path would have caught the analogous degeneracy in debug; the linear path lets it through in both debug and release.

Blast radius: low — it requires a malformed `min == max` continuous descriptor, which a correct effect won't author, and a NaN parameter is more likely to surface loudly than to corrupt silently. Still, the asymmetry is a small inconsistency: a `assert(d.max > d.min && "linear parameter requires min < max")` on the linear branches would make the two skew paths enforce the same precondition and keep the validation discipline uniform across the file.

---

I also checked, and found clean: `span.h`'s SFINAE converting ctor correctly excludes `const`-array-to-`T*` narrowing via the `static_cast<T*>` guard (binds `span<const T>` from const arrays as intended, rejects rvalues to avoid dangling); `effect.h`'s C++17 `is_effect` trait divergence from the C++20 concept (it omits the return-type convertibility check) is documented as diagnostic-only and never gates a build, so it's informational rather than a defect; `processor-node.h`'s single-virtual-per-block design matches FR-004; and the discrete normalize/denormalize round-trip is correct (`floor((idx+0.5)/count·count) == idx`).