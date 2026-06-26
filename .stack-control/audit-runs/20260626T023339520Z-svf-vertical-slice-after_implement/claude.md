I'll audit this chunk (the SVF primitive, the host `ProcessorNode` boundary, and the portability gate script) and emit findings independently.

### `SvfPrimitive::reset()` silently discards configured cutoff/resonance, not just filter state

Finding-ID: AUDIT-BARRAGE-claude-01
Status:     open
Severity:   medium
Surface:    core/primitives/svf-primitive.h:35-37 (`reset()` + its doc comment)

`reset()` is implemented as `svf_.Init(sampleRate_)`, and its comment claims it brings the filter to a "cleared-but-prepared state (DaisySP's Init clears state)." DaisySP's `Svf::Init` does more than clear the integrator state — it also re-seats the configuration to DaisySP defaults (cutoff ≈ 200 Hz, res ≈ 0.5) and recomputes coefficients from them. So `reset()` does not merely clear history; it throws away whatever `setFreq`/`setRes` the owning `SvfEffect` previously pushed and reverts the filter to a 200 Hz / 0.5-res default.

The blast radius depends on `SvfEffect` (not in this chunk), but the defect lives in this primitive's contract. The documented thread/handoff model elsewhere in the chunk (processor-node.h:18-24) says parameter changes "land at the next processBlock" via an atomic *pending* value that is consumed only when a change is published. If `SvfEffect::reset()` forwards to `primitive.reset()` and does **not** unconditionally re-publish the current cutoff/resonance, then a reset followed by a block with no parameter change will run the filter at DaisySP defaults rather than the user's last-set values — an audible, silent wrong-state bug that an unattended caller relying on the "cleared-but-prepared" wording would never suspect.

A reasonable fix: have `reset()` clear only the integrator state and then re-apply `sampleRate_`, the last `setFreq` Hz, and the last `setRes` value (cache them in the wrapper), OR correct the doc comment to state explicitly that `reset()` reverts cutoff/resonance to defaults and that callers MUST re-apply parameters afterward. The current comment actively conceals the configuration loss.

### Portability gate reports "OK" when a grepped directory is absent (grep exit-2 false negative)

Finding-ID: AUDIT-BARRAGE-claude-02
Status:     open
Severity:   medium
Surface:    scripts/check-portability.sh:30-44 (checks 2 and 3)

Checks 2 and 3 are structured as `if grep -rEn '<pat>' <dir> ; then FAIL ; else OK ; fi`. `grep` returns 0 on a match, 1 on no-match, and **2 on error** (e.g., the directory does not exist or is unreadable). The `if` treats exit 2 as false and falls into the `else` branch, printing the success line — e.g. "OK: daisy + teensy reference neither JUCE nor ProcessorNode" — even though grep never actually inspected the target. Check 3 greps `adapters/daisy adapters/teensy` as two arguments; if either is renamed/removed, grep still exits non-zero-but-not-1 and the gate reports a clean pass while having scanned at most one of the two MCU adapters.

This is the precise false-negative shape the project's "no fallbacks that hide failure modes" rule warns against: a portability gate that goes green when the thing it is supposed to police has structurally drifted away. It does not fire in today's tree (all dirs exist), so the blast radius is conditional on a future directory rename/move — hence medium, not high — but when it does fire it fails *silently green*, which is worse than a loud failure because CI would report the gate as satisfied.

Fix: distinguish "no matches" (exit 1 = pass) from "grep error" (exit ≥2 = hard fail). Capture the status explicitly, e.g. `grep ...; rc=$?; if [ "$rc" -eq 0 ]; then FAIL; elif [ "$rc" -ge 2 ]; then note "ERROR: could not scan <dir>"; fail=1; fi`, or pre-assert each expected directory exists before scanning.

### `acfx_core` literal match in check 4 breaks if an adapter uses the project's own `acfx::core` alias

Finding-ID: AUDIT-BARRAGE-claude-03
Status:     open
Severity:   medium
Surface:    scripts/check-portability.sh:53-58 (per-adapter `grep -rq 'acfx_core'`)

Check 4 asserts every adapter links the same core via `grep -rq 'acfx_core' "adapters/$adapter/CMakeLists.txt"`. This matches one literal spelling of the link target. The codebase deliberately promotes namespaced ALIAS targets — host/processor-node/CMakeLists.txt:6 defines `add_library(acfx::host ALIAS acfx_host)` — so it is idiomatic (and likely a core CMakeLists already defines `acfx::core ALIAS acfx_core`) for a consumer to write `target_link_libraries(... acfx::core)`. The string `acfx::core` does **not** contain the substring `acfx_core` (`::` vs `_`), so an adapter that links via the namespaced alias the project itself encourages would make `grep -rq 'acfx_core'` fail and the gate emit a **false FAIL** ("adapters/X does not link acfx_core") even though X links the same core target.

The gate is enforcing a spelling, not the semantic ("links the canonical core"). The blast radius is a future maintainer switching an adapter to the conventional alias and getting a red gate they cannot diagnose from the message, or — inversely — masking a genuine miswire because the check is too literal to be trusted. Medium because it's latent until someone uses the alias, but it directly couples a quality gate to an arbitrary spelling choice.

Fix: match either form, e.g. `grep -rqE 'acfx(_|::)core'`, or, more robustly, drive the check off the actual CMake configure (the target graph) rather than a textual grep of `CMakeLists.txt`.

### `#ifdef`-fork check is scoped only to `core/effects/`, leaving the wrapped primitive and shared DSP unguarded

Finding-ID: AUDIT-BARRAGE-claude-04
Status:     open
Severity:   low
Surface:    scripts/check-portability.sh:47-51 (check 4 `#ifdef` scan over `core/effects/`)

The "no per-target `#ifdef` fork of the effect" scan greps only `core/effects/`. But the effect's actual per-sample math now lives one directory over, in `core/primitives/svf-primitive.h` (introduced in this same chunk), and the shared contracts live in `core/dsp/`. A `#if defined(DAISY)` / `#ifdef __arm__` fork hidden in `core/primitives/` or `core/dsp/` — exactly where someone tempted to special-case the MCU build would put it — would sail past this gate while still being a one-source-many-targets violation per SC-001/SC-005.

This is low severity because it doesn't fire on today's clean tree and the constitution's intent is enforced elsewhere by convention; but the gate's *stated* claim ("no per-target #ifdef forks of the effect") over-promises relative to its `core/effects/`-only scope, which can lull an operator into trusting a check narrower than its label.

Fix: broaden the scan to the whole `core/` subtree (`grep -rEn '#if(def)?.*(JUCE|DAISY|TEENSY|__arm__|DESKTOP)' core/`) so the primitive and DSP spine are covered, or narrow the printed claim to match the actual scope.

### Per-check "OK" lines are gated on the cumulative `fail` flag, so a passing gate is silently suppressed after an earlier unrelated failure

Finding-ID: AUDIT-BARRAGE-claude-05
Status:     open
Severity:   low
Surface:    scripts/check-portability.sh:28, 60 (`[ "$fail" -eq 0 ] && note "  OK: ..."`)

Both the file-size summary (line 28) and the adapter-linking summary (line 60) print their "OK" line guarded by `[ "$fail" -eq 0 ]`, where `fail` is the **global cumulative** failure flag, not a per-check status. For check 1 this is harmless (it runs first). But by check 4, if check 2 or 3 already set `fail=1`, the "OK: every adapter links the same acfx_core" line is suppressed *even when every adapter does correctly link core*. An operator reading the output of a multi-failure run sees the OK confirmations for passing checks vanish, conflating which gate actually failed and making the report harder to diagnose.

This is purely an output-clarity defect — the exit code remains correct — so it is low severity. But the whole point of this script (per its header) is to be a "visible step" operators read on purpose; an output that hides the pass/fail status of independent checks undercuts that purpose.

Fix: track a per-check local status (e.g. `c1=0` reset at the top of each check, set on that check's own failures) and gate each "OK" line on the local status rather than the shared `fail` accumulator.