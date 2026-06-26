I'm being asked to perform an independent audit of chunk `d58ba5050d21850a` (plugin / teensy / workbench adapters) for the `svf-vertical-slice` feature. This is a direct review task against the provided diff — I'll walk the code and emit anchored findings.

### Non-atomic `fileBuffer_` swap in `useFilePlayer()` races with the audio thread's `fillBlock()`

Finding-ID: AUDIT-BARRAGE-claude-01
Status:     open
Severity:   high
Surface:    adapters/workbench/audio-source.cpp:28-31 (the swap) vs. :63-75 (the reader)

`useFilePlayer()` performs `fileBuffer_ = std::move(decoded);` (line 28) — a non-atomic move-assignment that destroys the previous `AudioBuffer<float>`'s heap storage — while the audio thread may be inside `fillBlock()` calling `fileBuffer_.getReadPointer(...)` and dereferencing `src[pos]` (lines 63-67). The `std::atomic` flags (`hasFile_`, `playPos_`) are published *after* the move (lines 29-31), so they order the *flag* but provide no protection for the buffer storage itself: an in-flight reader holding a `getReadPointer` into the old buffer gets a use-after-free the moment the move frees it. Notably the code does not even set `hasFile_=false` *before* the swap, which would at least let a fresh `fillBlock` bail out.

The header (audio-source.h:11-18) makes an *absolute* RT-safety claim and specifically says it eliminated "no transport object whose source pointer the audio thread could see freed mid-swap." That fix removed the transport but moved the same race onto the buffer reassignment — the channel was relocated, not closed. For a "sketch-and-hear workbench," loading a new file *during live playback* is the obvious use case, and `useFilePlayer()` exposes no precondition guard or documented stop-the-audio contract. Blast radius: an adopter wiring a "load file" button to this method while audio runs gets intermittent UAF crashes that won't reproduce deterministically. A correct fix double-buffers (hold both old and new, swap an `atomic<int>` active-index) or requires the caller to quiesce the audio graph first and states that as an enforced precondition, not a comment.

### Discrete normalized value lands exactly on the bucket-rounding seam — round-trip depends on undocumented `denormalize` semantics in another file

Finding-ID: AUDIT-BARRAGE-claude-02
Status:     open
Severity:   medium
Surface:    adapters/plugin/plugin-parameters.cpp:88-91 (`apply()` discrete branch)

For a discrete parameter `apply()` sends `norm = (index + 0.5f) / count` (line 90). The round-trip back to `index` only works if the effect's discrete `denormalize` *truncates* (`floor(norm * count)`): `(i+0.5)/count * count = i+0.5 → floor → i`. But `i + 0.5` is exactly the half-integer boundary. If the descriptor's discrete denormalize instead *rounds* (`std::round`/`lround`, or `int(x+0.5)`), every value sits precisely on the rounding seam and round-half-up yields `i+1` — a systematic off-by-one that silently selects the wrong filter mode (e.g. "lowpass" plays as "highpass"). The denormalize implementation lives in `core/dsp/parameter.h` (not in this chunk), so this surface encodes a fragile cross-file contract that nothing here documents or tests.

Blast radius: a downstream contributor changing or reimplementing discrete denormalize to use rounding (a natural choice) breaks mode selection across *both* plugin and workbench with no failing unit test, because the value is engineered to be maximally sensitive to the rounding rule. A robust fix offsets toward the bucket interior less ambiguously, or — better — has the discrete path send the integer index through a typed discrete channel rather than re-encoding it as a float that must survive a normalize/denormalize round-trip.

### SVF-specific mode names hardcoded in the generic descriptor→parameter builder

Finding-ID: AUDIT-BARRAGE-claude-03
Status:     open
Severity:   medium
Surface:    adapters/plugin/plugin-parameters.cpp:25-34 (`modeName`) and :47-49 (its use)

`plugin-parameters.cpp` is documented (plugin-parameters.h:11-18) as a *generic* mapping where "each `ParameterDescriptor` becomes a JUCE parameter" with "no hand-written parameter list." But the discrete-choice labels are produced by `modeName(int index)` (lines 25-34), which hardcodes `"lowpass"/"highpass"/"bandpass"` keyed by integer index — SVF-specific data baked into the supposedly effect-agnostic builder. This is configuration-as-code: the choice labels belong in the `ParameterDescriptor` table alongside the count, not in a `switch` in the plugin adapter.

Concretely this breaks the moment a second discrete parameter exists, or if the SVF mode enum is reordered: every discrete parameter would be labeled with filter-mode names, and a mode reorder in the core silently mislabels the host UI (the *labels* desync from the *behavior*, which still flows through the numeric index). Blast radius is contained to display correctness today but it's a coupling violation against the stated platform-independent/data-driven design (Constitution IV; the header's own SC-006 claim of an identical cross-adapter mapping). Fix: carry the choice labels in the descriptor (a `span<const char*>` or equivalent) and have `build()` read them.

### `ParameterDescriptor::defaultValue` carries two incompatible meanings (plain value vs. raw index) with no type-level distinction

Finding-ID: AUDIT-BARRAGE-claude-04
Status:     open
Severity:   medium
Surface:    adapters/plugin/plugin-parameters.cpp:44 (discrete) vs. :60-61 (continuous)

In the continuous branch, `defaultValue` is treated as a *plain engineering value*: `const float defaultNorm = normalize(d, d.defaultValue);` (line 61). In the discrete branch, the same field is treated as a *raw choice index*: `const int defaultIndex = static_cast<int>(d.defaultValue);` (line 44). The single `float defaultValue` field thus means different things depending on `kind`, with nothing in the type system or this code flagging the difference.

This is a latent trap for whoever authors the descriptor table: if a contributor populates a discrete parameter's `defaultValue` the way the continuous ones are populated (e.g. a normalized 0..1 default, mirroring the surrounding rows), `static_cast<int>` collapses any value in [0,1) to index 0 — the default mode is silently wrong with no error (Constitution V's "fail loud" intent defeated). Blast radius: quiet wrong default-state on plugin instantiation, the kind of thing an unattended build never catches. A fix splits the semantics (a dedicated `defaultIndex` for discrete, or a tagged union) or at minimum documents and asserts the contract at the descriptor boundary.

### `MidiBinding` has no synchronization between `bind()` and `handle()` despite documenting runtime re-binding

Finding-ID: AUDIT-BARRAGE-claude-05
Status:     open
Severity:   medium
Surface:    adapters/workbench/midi-binding.h:22 (`bind`) and :26-37 (`handle`)

`bind()` mutates `std::unordered_map<int, ParamId> bindings_` (line 22) and `handle()` reads it via `bindings_.find(...)` (line 31). The class doc explicitly states "Re-binding a CC replaces it" (line 21), advertising runtime mutation. In the workbench, MIDI messages are delivered on the audio device's MIDI/audio callback thread while `bind()` is the natural response to a UI action — concurrent read/write on a `std::unordered_map` is a data race / UB (possible rehash invalidating the concurrent `find`). Nothing in the class documents a threading contract or single-thread restriction, and unlike `WorkbenchAudioSource` (which went to lengths with atomics) this surface uses none.

Blast radius is conditional on whether bindings actually change at runtime; if they're frozen at setup it's benign, but the doc invites runtime rebinding and there's no guard or comment forbidding it. The audited siblings (`audio-source`) treat the audio thread as a hard concurrency boundary; this file ignores it. A fix either documents+enforces "configure before audio starts," or uses an RT-safe lookup (fixed-size CC→ParamId array indexed 0..127, which is also allocation-free and avoids the hash entirely).

### `teensy-main.cpp` is declared in chunk scope but absent from the diff — unauditable surface

Finding-ID: AUDIT-BARRAGE-claude-06
Status:     open
Severity:   informational
Surface:    chunk d58ba5050d21850a "Files in scope" list vs. the Diffs section

The chunk header lists `adapters/teensy/teensy-main.cpp` under "Files in scope," and `adapters/teensy/CMakeLists.txt` (lines 18-23) treats it as the *only* platform-specific Teensy source — the `AudioStream` node plus analog reads, the heart of the SC-007 ARM-portability claim. But no diff for `teensy-main.cpp` appears in this chunk (the Diffs section jumps from `teensy/CMakeLists.txt` to `workbench/CMakeLists.txt`). I therefore could not review the one file that exercises the core on the Teensy target.

This is not itself a code defect, but it's a coverage gap the operator should know about: the RT-safety, no-allocation, and "same `SvfEffect` compiles unchanged on C++17" claims for the Teensy adapter rest on a file no model in this barrage can see. If it was meant to be in this chunk, the audit of US3's hardware leg is incomplete; the file should be routed into a chunk so the `process()`-path allocation/RT claims get the same scrutiny the plugin and workbench paths received.

### `discreteCount` clamp diverges between `build()` and `apply()`, and `build()` accepts a zero-choice parameter

Finding-ID: AUDIT-BARRAGE-claude-07
Status:     open
Severity:   low
Surface:    adapters/plugin/plugin-parameters.cpp:40-42 (build) vs. :89 (apply)

`apply()` clamps the divisor to a minimum of 2: `const int count = e.descriptor.discreteCount < 2 ? 2 : e.descriptor.discreteCount;` (line 89), but `build()` uses the raw `d.discreteCount` to populate choices (lines 40-42) and to pick `defaultIndex`. For any well-formed parameter (`discreteCount >= 2`) these agree, so this is hygiene rather than a live bug — but the asymmetry means a `discreteCount` of 0 or 1 produces inconsistent normalization between the two methods (build encodes with N, apply decodes with 2), and a `discreteCount` of 0 hands `juce::AudioParameterChoice` an empty `StringArray` (line 41 loops zero times), which JUCE treats as invalid. There is no validation rejecting `discreteCount < 2` at the boundary.

Blast radius is small because the current single discrete parameter has 3 modes, but the divergent clamp is exactly the kind of silent inconsistency that bites when the descriptor table grows. A fix unifies the clamp (or, better, asserts `discreteCount >= 2` once at the top of `build()` so a malformed descriptor fails loud rather than producing a degenerate parameter).

---

**Summary of what I checked that came back clean:** the `processBlock` channel marshalling (plugin-processor.cpp:42-52) is safe given `isBusesLayoutSupported` caps at stereo and `kMaxChannels`=8; the `withStringFromValueFunction` lambda captures the descriptor by value (no dangling, line 67); `fillBlock`'s per-channel loop position arithmetic round-trips correctly against the final `playPos_` modulo store; and `useFilePlayer`'s error paths (non-existent file, no decoder, empty file) all throw descriptively rather than falling back to silence. My strongest concern by blast radius is **AUDIT-BARRAGE-claude-01** (the buffer-swap UAF), because the header makes an absolute RT-safety claim the code does not actually honor for the live-reload path.