I'll audit chunk `a05731de7f2cca0b` carefully — the SVF primitive wrapper, the host ProcessorNode boundary, the host CMake target, and the portability gate script.

### `reset()` silently reverts configured cutoff/resonance to DaisySP defaults

Finding-ID: AUDIT-BARRAGE-claude-01
Status:     open
Severity:   high
Surface:    core/primitives/svf-primitive.h:40-41 (`reset()`), with cross-file dependency on `core/effects/svf/svf-effect.h` (chunk e7b284327d06692a)

`reset()` is implemented as `svf_.Init(sampleRate_)`. DaisySP's `Svf::Init` does **not** merely clear filter state — it re-seeds the cutoff (`fc_`) and resonance (`res_`) to its built-in defaults (≈200 Hz, ≈0.5) and recomputes coefficients from them. The wrapper stores only `sampleRate_` and `mode_`; it does **not** store the last `hz`/`res` it was given, so after `reset()` there is no way for it to restore the configured tuning. The result is an asymmetric reset: `mode_` survives (it's a wrapper member), but the cutoff and resonance the user dialed in are dropped back to DaisySP defaults. The doc comment claims only "cleared-but-prepared state (DaisySP's Init clears state)" — it does not disclose that frequency and resonance revert, so the comment drifts from behavior.

Whether this is observable depends on `SvfEffect::reset()` (not in this chunk). Per the thread contract in `processor-node.h:18-24`, `SvfEffect` "publishes an atomic pending value that the audio thread consumes inside `process()`" — i.e. parameters are applied on *change*. If `SvfEffect::reset()` calls `primitive.reset()` but does not unconditionally re-push the current freq/res (because the atomic pending value hasn't changed since the last block), the filter will run at 200 Hz / 0.5 Q after every reset until the user next moves a knob. That is a latent, audible mis-tuning on every transport stop/start or host reset call.

Blast-radius: a downstream consumer wiring `EffectNode<SvfEffect>` into a DAW or the workbench will get correct audio on first play, then a wrongly-tuned filter after any `reset()`, with nothing in the code or comment warning them. Fix: have `SvfPrimitive` store the last `hz_`/`res_` and re-apply them inside `reset()` after `Init`, or split "clear state" from "reconfigure" so `reset()` preserves tuning — and update the comment to state exactly what survives a reset. (Verify the precise DaisySP `Svf::Init` body against the pinned CPM version before fixing.)

### Portability gate's JUCE check is case-sensitive — misses `JuceHeader.h` / `JUCE`

Finding-ID: AUDIT-BARRAGE-claude-02
Status:     open
Severity:   medium
Surface:    scripts/check-portability.sh:27 (gate 2) and :35 (gate 3)

Gate 2 (`grep -rEn 'juce|libDaisy|daisy_seed|<Audio\.h>|<Arduino\.h>' core/`) and gate 3 (`grep -rEn 'juce|processor-node' adapters/daisy adapters/teensy`) both match `juce` case-sensitively. The lowercase pattern catches the common leak forms (`juce::` namespace use, `<juce_dsp/...>` module includes) but does **not** match the Projucer-style umbrella header `#include <JuceHeader.h>` (capital `Juce`) or any `JUCE`-cased macro/identifier. A `core/` file that pulls in `<JuceHeader.h>` would slip through and the gate would print "OK: core/ is platform-independent" — a false-clean on a Constitution-IV safety gate.

This matters because the gate's entire purpose is to be the explicit, trusted enforcement of "no platform headers in core" (the comment cites Constitution IV / SC-007). A consumer or CI reads a green gate as proof the invariant holds; the blind spot means the proof is incomplete. Because the most-common JUCE leak shapes are lowercase and *are* caught, this is a partial gap rather than a wide-open hole, hence medium rather than high.

Fix: add `-i` to the `grep` calls (or broaden the alternation to include `JUCE|Juce`). Note the same case-sensitivity applies to `<Audio\.h>`/`<Arduino\.h>` — on case-insensitive filesystems (macOS) a `#include <arduino.h>` would also evade the check.

### Gate 4's `acfx_core` literal-substring check false-fails when adapters link via the `acfx::host` / `acfx::core` alias

Finding-ID: AUDIT-BARRAGE-claude-03
Status:     open
Severity:   medium
Surface:    scripts/check-portability.sh:52-57; host/processor-node/CMakeLists.txt:6,11

Gate 4 asserts "every adapter links the same acfx_core" via `grep -rq 'acfx_core' "adapters/$adapter/CMakeLists.txt"` for `workbench plugin daisy teensy`. But `host/processor-node/CMakeLists.txt` defines an alias `acfx::host` (line 6) that itself links `acfx_core` (line 11), and the comment states the host target is "Shared by the workbench + plugin adapters." If the workbench/plugin CMakeLists link **`acfx::host`** (the documented, intended consumption path) rather than the raw `acfx_core` token, the literal substring `acfx_core` will not appear in their CMakeLists and the gate prints "FAIL: adapters/workbench does not link acfx_core" — a false failure on a correctly-wired adapter. The same happens for any adapter that links the namespaced alias `acfx::core` (the string `acfx::core` does not contain the substring `acfx_core`).

The gate conflates "links the core library" with "the literal text `acfx_core` appears in this file." Transitive linkage through an alias is the normal, idiomatic CMake pattern and the project explicitly created `acfx::host` to enable it, so the gate's heuristic is at odds with the design it's meant to verify. This fails loud (CI red) rather than silently passing, so it's less dangerous than finding 02, but a day-one false-fail that blocks legitimate builds (or, worse, pressures someone to add a redundant `acfx_core` line just to satisfy the grep) is a real cost.

Fix: match the alias forms too (`grep -Eq 'acfx_core|acfx::core|acfx::host'`), or — better — query the actual link graph via CMake/`file(GENERATE)` rather than text-grepping CMakeLists. Verify against the real adapter CMakeLists (chunks a70ef633…, 4cfb00d5…, 6a56babf…) which aren't visible in this chunk.

### `SvfPrimitive::process()` has an unenforced init-before-process precondition

Finding-ID: AUDIT-BARRAGE-claude-04
Status:     open
Severity:   low
Surface:    core/primitives/svf-primitive.h:18-21, 34-46, 50

`svf_` is default-constructed (`daisysp::Svf svf_{}`) and only made usable by `init()`, which calls `svf_.Init(sampleRate)`. There is no guard preventing `process()` from running before `init()`; if it does, the underlying DaisySP filter runs with zero/default-constructed coefficients (no valid cutoff), producing silence or garbage rather than a clear error. `sampleRate_` defaults to 48000 but `svf_` is not prepared by that default. This is a standard DSP "prepare-before-process" contract, and the wrapper documents `init()` as the preparation step, so the risk is bounded — but the contract is implicit, not enforced or asserted.

Blast-radius is low: the host boundary (`EffectNode::prepare` → `fx_.prepare` → presumably `primitive.init`) makes correct ordering the normal path, and misuse would be caught immediately in audible testing. I flag it only because the project's stated principle is "raise descriptive errors for missing functionality instead of silently degrading"; a `process()` that silently produces garbage when un-prepared is a quiet-failure shape. A cheap mitigation is a debug-only assert that `init()` ran (or documenting the precondition explicitly on `process()` as is done for `setFreq`'s clamp precondition).

I checked the remaining surfaces and found them clean: `host/processor-node/processor-node.h`'s single-virtual-dispatch design and documented "Effect owns the cross-thread handoff" contract are internally consistent (the node deliberately adds no synchronization — correct, given the documented division of responsibility); `host/processor-node/CMakeLists.txt`'s INTERFACE target and include-root exposure correctly propagate `acfx_core` headers transitively; and `external/.gitkeep` is an empty placeholder with no behavior.