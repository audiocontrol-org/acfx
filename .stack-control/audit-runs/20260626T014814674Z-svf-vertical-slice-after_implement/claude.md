I walked this chunk (plugin + teensy + workbench adapters) carefully. Findings below, anchored to the diff.

### "Descriptor-driven" parameter builder hardcodes effect-specific choice labels by integer index

Finding-ID: AUDIT-BARRAGE-claude-01
Status:     open
Severity:   medium
Surface:    adapters/plugin/plugin-parameters.cpp:24-34, 53-58

`plugin-parameters.h:11-14` advertises this surface as fully descriptor-driven — *"There is no hand-written parameter list: each ParameterDescriptor becomes a JUCE parameter."* But the discrete branch fills the choice strings from a hardcoded `modeName(int index)` switch (`lowpass`/`highpass`/`bandpass`, default `lowpass`) that encodes SVF-specific knowledge keyed by raw integer index. The generic builder therefore is not generic: a second discrete effect, or an added SVF mode, would silently mislabel.

Concretely, `modeName` returns `"lowpass"` for every index ≥ 3 (and index < 0). If an agent extends the effect to `discreteCount = 4` (e.g. adds notch), `build()` loops `for (i = 0; i < d.discreteCount; ++i) choices.add(modeName(i))` and produces choices `{lowpass, highpass, bandpass, lowpass}` — a duplicate label and a wrong name for the new mode, with nothing in this file flagging it. The blast radius is a quietly-wrong UI/automation label an unattended agent would build on, believing the parameter system is data-driven. A reasonable fix: carry the choice labels in the `ParameterDescriptor` itself (a label table the descriptor owns), so the adapter contains no per-effect string knowledge — matching the file's own stated contract.

### `defaultValue` is interpreted as a plain unit for continuous params but as a raw index for discrete — undocumented overload

Finding-ID: AUDIT-BARRAGE-claude-02
Status:     open
Severity:   medium
Surface:    adapters/plugin/plugin-parameters.cpp:50-52 vs 64-66

In the discrete branch the default comes from `const int defaultIndex = static_cast<int>(d.defaultValue);` (line 51) — `defaultValue` is read as a choice index. In the continuous branch the same field is read as a plain physical value: `const float defaultNorm = normalize(d, d.defaultValue);` (line 65). So `ParameterDescriptor::defaultValue` means two different things depending on `kind`, and nothing in this chunk states that contract.

For the current SVF this happens to work because the mode indices (0/1/2) coincide with their plain values, so `static_cast<int>` is harmless. That coincidence hides the assumption. If the descriptor convention is actually "defaultValue is always a plain value that must be normalized then denormalized" (as the continuous branch and the apply()/denormalize round-trip imply), then the discrete branch is inconsistent and a future discrete parameter whose plain default ≠ its index would boot to the wrong choice. Because `core/dsp/parameter.h` is in another chunk I can't confirm which interpretation the descriptor intends — that itself is the risk: the two branches encode different assumptions about a shared field. Fix: make both branches go through the same normalize/denormalize path, or document on the descriptor that `defaultValue` for `discrete` is a 0-based index.

### `MidiBinding` declares no thread-ownership contract and can rehash/race on the audio thread

Finding-ID: AUDIT-BARRAGE-claude-03
Status:     open
Severity:   medium
Surface:    adapters/workbench/midi-binding.h:22-37

`bind()` does `bindings_[ccNumber] = id;` (line 22) — an `unordered_map` insert that may rehash and allocate. `handle()` (lines 27-37) is `const` and calls `bindings_.find(...)`. In JUCE, MIDI arrives on the audio thread, so `handle()` will typically run there. The file gives no thread-ownership statement and no enforcement, unlike its sibling `audio-source.h:12-21`, which spends a full paragraph pinning setup-vs-audio-thread ownership and *enforces* the precondition (a misuse throws).

If the workbench UI calls `bind()` (e.g. a "learn CC" affordance, or rebinding at runtime) while `handle()` is reading on the audio thread, that is a data race on the map plus a potential heap rehash observed by the audio thread — both forbidden by Constitution VI (no allocation/locks on the audio path). Even if today's caller only binds before the stream starts, the contract is implicit and one UI affordance away from a real-time violation. Given the convergence history shows multiple rounds spent specifically on RT-safety and thread ownership for the other adapters, this one is the outlier. Fix: document the same setup-time-only ownership rule as `audio-source.h` and either freeze the map after setup or back `handle()` with a lock-free snapshot.

### "Allocation-free" comment on the audio path relies on undocumented `std::function` small-object optimization

Finding-ID: AUDIT-BARRAGE-claude-04
Status:     open
Severity:   medium
Surface:    adapters/plugin/plugin-processor.cpp:20-26; adapters/plugin/plugin-parameters.h:21-22

`processBlock` constructs a fresh `std::function` (the `ApplyFn` type, `plugin-parameters.h:21`) from a lambda every block — `parameters_.apply([this](ParamId id, float normalized){ node_.setParameter(id, normalized); });` — and the comment on lines 22-23 asserts this is *"Allocation-free."* That assertion is only true by grace of small-object optimization: the lambda captures a single pointer (`this`) and fits the implementation's inline buffer. It is not a guaranteed property of `std::function`, and a slightly larger capture (or a stdlib with a smaller SOO buffer) would silently heap-allocate on the audio thread — exactly the failure the comment claims to rule out.

The same file's sibling `audio-source.h` is careful to keep `std::function` *off* the audio path; the plugin reintroduces it per-block and labels the risk away. The blast radius: an RT-safety comment that an adopter or agent will trust, masking a latent allocation that only appears under a capture/stdlib change. Fix: pass the apply target as a non-owning callback type with no allocation contract (e.g. a small `FunctionRef`/raw `void(*)(void*, ParamId, float)` + context, or hoist the `std::function` to a member built once in the constructor), and state the RT guarantee in terms of that type rather than SOO.

### `audio-source.cpp` is listed in scope but its diff is absent — the lock-free `fillBlock` claims are unverifiable from this chunk

Finding-ID: AUDIT-BARRAGE-claude-05
Status:     open
Severity:   low
Surface:    adapters/workbench/audio-source.cpp (declared in "Files in scope", no diff present)

This chunk's "Files in scope" header lists `adapters/workbench/audio-source.cpp`, but only `audio-source.h` appears in the diffs. The header makes strong, load-bearing RT claims (lines 14-21): file decoded into memory off the audio thread, `fillBlock()` reads at an atomic `playPos_` with no locks/allocation, source-switching enforced as a precondition that throws. None of that can be checked, because the implementation that would either honor or violate those claims is not shown.

The audio-thread `fillBlock` body is the single most RT-critical surface in the workbench adapter; reviewing the header alone gives false assurance. If this is a presentation truncation, surface the `.cpp` so the lock-free read, the `playPos_` wraparound at end-of-buffer, and the `configured_`/`hasFile_`/`live_` interplay can be audited. I flag it as low only because it is plausibly an extraction artifact rather than a missing file — but the RT guarantees it would carry are exactly the ones the project has repeatedly re-audited.

### Plugin discards all DAW session state — parameters won't recall on project reload

Finding-ID: AUDIT-BARRAGE-claude-06
Status:     open
Severity:   low
Surface:    adapters/plugin/plugin-processor.h:43-46

`getStateInformation`/`setStateInformation` are empty no-ops, annotated as out-of-scope for this milestone (lines 43-44). The annotation is honest, so this is not a hidden fallback. But the behavioral consequence is real and worth a triage note: for VST3/AU/CLAP the host persists plugin state through these calls, so a user who saves a DAW project with a tweaked cutoff/mode and reopens it gets the plugin's defaults back, not their settings — including any host that doesn't independently cache automation values. 

The blast radius is a surprised adopter rather than a correctness break, and it is documented, hence low. The reason I surface it at all: "out of scope" for a *plugin* and "no state persistence" read very differently to an end user, and the README/PRD for an A/B sketch tool should say plainly that settings don't survive a session reload so the gap isn't discovered in a DAW. Fix when in scope: serialize the parameter tree (JUCE `AudioProcessorValueTreeState` or a manual normalized-value blob) in these two methods.