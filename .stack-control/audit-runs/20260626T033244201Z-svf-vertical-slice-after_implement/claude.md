I'll audit this chunk (`a05731de7f2cca0b`) directly — it's a security/quality review task with a defined output format. Walking the five files in scope: the SvfPrimitive wrapper, the host ProcessorNode/EffectNode boundary, the host CMake, the `.gitkeep`, and the portability gate script.

### `SvfPrimitive::reset()` reverts cutoff/resonance to DaisySP defaults instead of preserving configuration

Finding-ID: AUDIT-BARRAGE-claude-01
Status:     open
Severity:   high
Surface:    core/primitives/svf-primitive.h:35 (`reset()`), cross-file with SvfEffect (chunk e7b284…)

`reset()` is documented as returning the filter to a "cleared-but-prepared state" and is implemented as `svf_.Init(sampleRate_)`. But DaisySP's `Svf::Init` does not merely clear the integrator state (`low_/band_/high_`) — it also re-seeds the filter coefficients to DaisySP's own defaults (`fc_ ≈ 200 Hz`, `res_ ≈ 0.5`) and recomputes `SetFreq`/`SetRes` from them. The conventional contract of a DSP filter `reset()` is "zero the state history, keep the configured cutoff/resonance." This implementation silently discards the cutoff and resonance previously pushed via `setFreq`/`setRes`, reverting them to ~200 Hz / 0.5. `mode_` survives (it's a member), but freq and res do not.

The blast radius is a cross-file state desync. The `ProcessorNode` thread contract (processor-node.h:18–24) says SvfEffect "publishes an atomic pending value that the audio thread consumes inside `process()`." If that handoff only re-publishes on *change* (the usual lock-free pattern: compare-and-publish, or a dirty flag), then after a `reset()` — panic, transport stop, re-prepare — the DaisySP filter is back at 200 Hz/0.5 while the effect's cached parameter values are unchanged, so nothing re-applies them. The filter then runs as a 200 Hz lowpass at res 0.5 until the user happens to move a control. An adopter calling `SvfPrimitive::reset()` directly would hit the same surprise. A reasonable fix: have `reset()` clear only the state (re-`Init`, then immediately re-apply the last `setFreq`/`setRes`/`setMode`), or cache `freq_`/`res_` in the wrapper and restore them after `Init`. The contract comment must then match whichever semantics are chosen.

### Portability gate checks 2 and 3 fail *open* — a moved/renamed directory makes them pass vacuously

Finding-ID: AUDIT-BARRAGE-claude-02
Status:     open
Severity:   medium
Surface:    scripts/check-portability.sh:26-40 (checks 2 and 3)

Both `if grep -rEn '…' core/ ; then …FAIL… else …OK… fi` (check 2) and the equivalent for `adapters/daisy adapters/teensy` (check 3) collapse three distinct grep exit codes into two branches: grep returns 0 on match (→ FAIL), 1 on no-match (→ OK), and **2 on error** (e.g. the directory doesn't exist) — which also falls into the `else` → "OK" branch. The gate therefore cannot distinguish "checked and clean" from "couldn't check at all." If `core/` is reorganized or a path is renamed (and the prior commit log shows multiple rounds touched "source path"), this gate prints `OK: core/ is platform-independent` and exits success while having scanned nothing.

This is exactly the failure mode a quality gate is supposed to prevent, and it's a quietly-plausible wrong reading: an operator (or unattended CI) sees a green portability check and trusts that core is platform-clean, when in fact the scan silently no-op'd. Note check 4's adapter loop (`! grep -rq … 2>/dev/null`) fails *safe* (missing file → FAIL), so the script is internally inconsistent about its failure direction. Fix: guard each path with an explicit existence check that hard-fails if the expected directory is absent (`[ -d core ] || { note FAIL; fail=1; }`), or capture grep's exit status and treat `2` as an error distinct from `1`.

### Adapter-link gate 4 greps the literal string `acfx_core` rather than verifying the link graph

Finding-ID: AUDIT-BARRAGE-claude-03
Status:     open
Severity:   medium
Surface:    scripts/check-portability.sh:48-53 (check 4 adapter loop)

The "every adapter links the same `acfx_core`" check is `grep -rq 'acfx_core' adapters/$adapter/CMakeLists.txt`. But the host boundary added in this same diff exposes an alias: host/processor-node/CMakeLists.txt declares `acfx_host` as an INTERFACE library that itself `target_link_libraries(acfx_host INTERFACE acfx_core)`. The natural way for the workbench and plugin adapters to consume core is to link `acfx::host` (pulling `acfx_core` transitively), in which case their `CMakeLists.txt` need never mention `acfx_core` by name — and the gate would false-FAIL a correctly-wired adapter.

The channel this opens: to satisfy a string-presence proxy, an author will add a redundant direct `acfx_core` link (or a comment containing the token) purely to make the grep happy — code shaped to pass the gate rather than the gate verifying intent. Conversely a CMakeLists that merely *mentions* `acfx_core` in a comment but doesn't link it would false-PASS. The check verifies a substring, not the dependency edge it claims to enforce. A more honest gate would query the resolved link graph (e.g. a configured-CMake `--graphviz` or a `target_link_libraries` parse that follows the `acfx::host`→`acfx_core` alias), or at minimum accept `acfx::host` as a transitive proof for the desktop adapters and require the literal only for the MCU adapters.

### `SvfPrimitive` is a reusable `core/` surface but silently trusts caller-supplied ranges with no enforcement

Finding-ID: AUDIT-BARRAGE-claude-04
Status:     open
Severity:   medium
Surface:    core/primitives/svf-primitive.h:21-25 (`setFreq`/`setRes`), :39-49 (`process` before `init`)

`setFreq` carries the comment "DaisySP requires `0 < f < sampleRate/3`; the caller (SvfEffect) clamps cutoff into that range before calling," and `setRes` documents `[0,1]` — but neither method enforces or clamps. The precondition is delegated entirely to one specific caller. `SvfPrimitive` lives in `core/primitives/` as a general, reusable wrapper (the header markets it as "a thin, allocation-free, mode-selectable wrapper"); any second adopter that calls `setFreq` with an out-of-range value gets DaisySP filter instability / NaN propagation with no diagnostic. Similarly, `process()` relies on `init()` having run: `svf_{}` value-initializes to zeroed coefficients, so calling `process()` before `init()` yields silent garbage rather than a detectable error — which directly contradicts the project rule "raise descriptive errors for missing functionality instead of fallbacks."

This is a footgun in a layer explicitly designed for reuse. The blast radius is bounded today (SvfEffect is the only caller and it clamps), but the contract is encoded only as a comment naming a sibling class — exactly the kind of leaked precondition that breaks the next consumer who reads the type signature rather than the prose. Reasonable fixes: clamp inside the primitive (it's still allocation-free and bounded), or — if "thin, caller-validates" is a deliberate invariant — state it as a precondition with a debug `assert`/contract on the range so a violating caller fails loudly in test/debug builds rather than producing a quiet wrong filter.

---

Two checked-and-clean notes for the operator's cross-model join: the `EffectNode<T>` / `ProcessorNode` boundary (processor-node.h) is sound — single virtual dispatch per block, `final` on the concrete node, `T` stored by value with no hidden indirection, thread handoff correctly delegated to the wrapped effect; the only latent constraint is that `T` must be default-constructible (no ctor forwarding), which is fine for the current effect. The mode switch in `process()` maps `highpass→High()`, `bandpass→Band()`, `lowpass/default→Low()` correctly. `external/.gitkeep` and the host CMake INTERFACE wiring are unremarkable.