I walked this chunk (the plugin / teensy / workbench adapters) carefully. Here are my independent findings.

### Hardcoded SVF mode labels with a silent `default` fallback inside a class documented as generic/descriptor-driven

Finding-ID: AUDIT-BARRAGE-claude-01
Status:     open
Severity:   medium
Surface:    adapters/plugin/plugin-parameters.cpp:24-34 (modeName), 49-58 (discrete build loop); adapters/plugin/plugin-parameters.h:9-16 (class contract)

`plugin-parameters.h` advertises this class as fully data-driven: *"There is no hand-written parameter list: each ParameterDescriptor becomes a JUCE parameter."* But `modeName(int index)` (cpp:24-34) is a hardcoded SVF-specific switch returning `"lowpass"/"highpass"/"bandpass"`, and the build loop populates discrete choices by calling `modeName(i)` for `i in 0..discreteCount-1`. The labels are *code*, not *data* — exactly the "configuration that should be data ending up as code" anti-pattern, and a leaking abstraction: a parameter builder that claims to be effect-agnostic hardcodes one effect's enum.

Worse, `modeName` has `case 0: default: return "lowpass";`. This is a silent fallback (Constitution V / project "no fallbacks" rule). If `SvfEffect` ever gains a 4th mode (notch/peak) and bumps `discreteCount` to 4 — the natural, descriptor-driven way to extend it — `modeName(3)` falls through to `"lowpass"`, producing a duplicate, silently-wrong choice label in the DAW's generated UI with no error. The blast radius is a downstream dev who extends the descriptor table (the documented extension point) and gets a mislabeled UI rather than a compile/run error. A reasonable fix: carry the discrete choice labels in the `ParameterDescriptor` itself (a `span<const char*>` of names) so the builder stays generic, and make an out-of-range index a hard error rather than a fallback.

### `processBlock` constructs a `std::function` every block — "Allocation-free" claim relies on unguaranteed SBO

Finding-ID: AUDIT-BARRAGE-claude-02
Status:     open
Severity:   medium
Surface:    adapters/plugin/plugin-processor.cpp:30-34; adapters/plugin/plugin-parameters.h:21 (ApplyFn = std::function), plugin-parameters.cpp:76 (apply signature)

`processBlock` (cpp:30-34) calls `parameters_.apply([this](ParamId id, float normalized){ node_.setParameter(id, normalized); })`, and `apply` takes `const ApplyFn&` where `ApplyFn = std::function<void(ParamId,float)>` (header:21). Every audio callback therefore constructs a fresh `std::function` from the lambda on the realtime thread. The inline comment explicitly claims *"Allocation-free."* That claim is only true by small-buffer-optimization luck: `std::function` is *not* guaranteed by the standard to avoid heap allocation. For a single `this`-pointer capture, libc++ and libstdc++ both SBO it, so on JUCE's actual targets it happens to be allocation-free today — but the guarantee is the library's, not the code's.

Given this feature's entire govern history is RT-safety hardening (commits 2fef393/bd79479: "no heap allocation in process()"), a comment asserting allocation-freedom via a non-guaranteed mechanism is a fragility worth closing. The latent failure mode: anyone who adds a second capture to that lambda (or builds against a stdlib without SBO) silently introduces a per-block heap allocation on the audio thread, and the allocation sentinel test (`tests/core/no-allocation-test.cpp`, other chunk) almost certainly does not exercise this JUCE plugin path. Fix: pass the apply target as a non-owning function-ref type or a template/concrete callable instead of constructing `std::function` per block, or at minimum static_assert/document the SBO dependency.

### `audio-source.cpp` is listed in scope but its implementation is absent from the diff — the RT-critical path is unaudited

Finding-ID: AUDIT-BARRAGE-claude-03
Status:     open
Severity:   medium
Surface:    adapters/workbench/audio-source.cpp (in this chunk's "Files in scope" header; no diff body present, and not present in any "Other chunks" file list)

This chunk's header lists `adapters/workbench/audio-source.cpp` in scope, but the diff body contains only `audio-source.h` — there is no `audio-source.cpp` diff here, and it does not appear in any of the five "Other chunks" file lists either. The header (audio-source.h:14-23) makes strong, load-bearing RT-safety promises: file decoded to memory off the audio thread, `fillBlock()` reads at an atomic `playPos_` *"with no locks and no allocation,"* and the setup-time precondition that source selection *"is ENFORCED — a selection call while already configured throws."* Every one of those guarantees lives in the `.cpp` that isn't in the reviewable diff.

The consequence for this audit: the most RT-safety-sensitive workbench code (Constitution VI) — the actual `fillBlock` read loop, the play-position wrap arithmetic, the decode-into-`fileBuffer_` path, and the `configured_`/`throw` enforcement — cannot be verified. The header's `noexcept fillBlock` could still allocate or take a lock and I'd have no way to see it. Either the `.cpp` was dropped from the bundle (a barrage-coverage gap the operator should re-run to close) or it genuinely isn't committed (a missing surface — the class would not link). The operator should confirm `audio-source.cpp` exists and is re-fed before treating the workbench audio-source RT claims as reviewed.

### `ParameterDescriptor::defaultValue` is interpreted two different ways depending on `kind`, with no stated contract

Finding-ID: AUDIT-BARRAGE-claude-04
Status:     open
Severity:   medium
Surface:    adapters/plugin/plugin-parameters.cpp:50 (discrete default), 62-63 (continuous default), 82-86 (discrete apply)

In `build()`, the continuous branch treats `d.defaultValue` as a *plain physical value*: `normalize(d, d.defaultValue)` (cpp:62-63). The discrete branch treats the *same field* as a *raw choice index*: `const int defaultIndex = static_cast<int>(d.defaultValue)` (cpp:50). Meanwhile `apply()` emits, for a discrete param, a *normalized* mid-bin value `(index + 0.5) / count` (cpp:84-85). So `defaultValue` carries three different encodings across the file's lifetime (plain value for continuous, raw index for discrete-default, normalized for discrete-runtime), and which one applies is implicit in `kind` with nothing in the descriptor documenting it.

This is the kind of quietly-plausible trap that bites an unattended agent extending the descriptor table (e.g. adding the SVF mode param, or a second discrete param). The natural assumption — "set `defaultValue` to the same normalized space everything else uses" — yields `static_cast<int>(0.5) == 0`, silently pinning the default to choice 0 with no error. The correctness here hinges entirely on the descriptor author having populated discrete `defaultValue` as a raw integer index, an invariant enforced nowhere in this diff. Fix: state the dual semantics explicitly in `ParameterDescriptor`'s doc (and ideally give discrete params a dedicated `defaultIndex` field) so the contract is in the data, not in the reader's head.

### `midi-binding.h` has no RT-safety / threading contract while every sibling adapter does, and `handle()`/`bind()` share an unsynchronized `unordered_map`

Finding-ID: AUDIT-BARRAGE-claude-05
Status:     open
Severity:   low
Surface:    adapters/workbench/midi-binding.h:18-37

Every other adapter surface in this chunk documents its thread/RT contract meticulously — `audio-source.h:14-23` spells out lock-free/alloc-free `fillBlock`, `plugin-processor.cpp:30` annotates "Allocation-free," `teensy-main.cpp:88-92` explains the `AudioNoInterrupts` grouping. `midi-binding.h` says nothing about which thread `handle()` runs on. MIDI is typically dispatched inside the audio callback, so `handle()` (which does `bindings_.find()` on an `unordered_map`, line 31) most likely runs on the realtime thread, while `bind()` (line 22) mutates that same map. `std::unordered_map` is not safe for concurrent read + write; if `bind()` can ever be called from the UI thread after the stream starts, that's a data race on the audio thread.

Blast radius is low because, in the expected usage, bindings are configured at setup before audio runs — but nothing in the header states or enforces that precondition (unlike `audio-source.h`, which makes "configure before prepare" an enforced, throwing invariant). A reader has no signal that re-binding a CC live is unsafe. Fix: document `handle()` as realtime/`noexcept`-read-only and `bind()` as setup-only, matching the rigor of the sibling headers, or make `bind()`-after-prepare a hard error the way the audio source does.