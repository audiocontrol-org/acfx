I'm acting as the `claude` lane of the audit barrage. I walked chunk `561c01cdba330da9` carefully — the workbench adapter, the CMake bootstrap/dependency/toolchain files, and `audio-block.h`. Findings below.

### Discrete (combo) controls ignore the descriptor's defaultValue, desyncing the GUI from the effect's real state

Finding-ID: AUDIT-BARRAGE-claude-01
Status:     open
Severity:   medium
Surface:    adapters/workbench/parameter-view.cpp:18-21 (combo branch) vs :35-37 (slider branch)

The continuous branch seeds the control from the descriptor default — `row.slider->setValue(normalize(d, d.defaultValue), …)` (line 36). The discrete branch unconditionally selects bucket 0: `row.combo->setSelectedItemIndex(0, juce::dontSendNotification)` (line 21), discarding `d.defaultValue` entirely. Because it's set with `dontSendNotification`, no `onChange` fires either, so the effect keeps whatever discrete default *it* initialized to while the combo displays bucket 0.

If any discrete parameter's default maps to a bucket other than 0 (e.g. an SVF filter-type enum defaulting to "lowpass" at index 1), the workbench opens showing the wrong selection and never reconciles it — the displayed mode contradicts the audible mode until the operator touches the control. That is a quiet correctness defect an operator would trust the UI over. The fix is to mirror the slider path: compute the default bucket from `normalize(d, d.defaultValue)` (same inverse used in `setNormalized`, lines 47-52) and select that index.

### ProcessContext is prepared for a hardcoded 2 channels but process() drives up to 8 channels from the live buffer

Finding-ID: AUDIT-BARRAGE-claude-02
Status:     open
Severity:   high
Surface:    adapters/workbench/workbench-app.cpp:60 (prepare) vs :97-103 (process)

`prepareToPlay` builds `const ProcessContext ctx{sampleRate, blockSize, 2}` with a literal channel count of 2 (line 60). `getNextAudioBlock` then derives the actual channel count from the live device buffer: `numChannels = juce::jmin(buffer.getNumChannels(), kMaxChannels)` (line 90, `kMaxChannels == 8`) and constructs `AudioBlock block(chans.data(), numChannels, numSamples)` (line 102) passed to `node_->processBlock`. `setAudioChannels(2, 2)` is a *request*, not a guarantee — JUCE may open a device with a different active channel count, in which case `buffer.getNumChannels()` can exceed 2.

If `SvfEffect::prepare` sizes its per-channel state (filter z-state, atomics) to `ctx.numChannels()`, and `process()` iterates `block.numChannels()`, a device that opens with >2 channels yields out-of-bounds reads/writes on the per-channel state — in the RT callback, the worst place for it. Even short of OOB, preparing for 2 while processing N is a contract violation the effect can't defend against. The prepared channel count must be the same quantity the process path uses: derive it from the device (`numInputChannels()`/output count) or clamp `processBlock` to `ctx.numChannels()`. I can't see `SvfEffect` in this chunk to confirm the OOB, so the blast radius is conditional on its state sizing — but the inconsistency itself is real and the failure mode is severe, hence high.

### kMaxChannels truncation silently drops channels with no surfaced warning

Finding-ID: AUDIT-BARRAGE-claude-03
Status:     open
Severity:   low
Surface:    adapters/workbench/workbench-app.cpp:90, :97-103

`numChannels = juce::jmin(buffer.getNumChannels(), kMaxChannels)` caps processing at 8 channels. Channels 8..N of the buffer are filled by `source_.fillBlock(region)` (dry) but never handed to the effect, so they pass through unprocessed while 0..7 are processed — a silent partial-processing split with no log or notice. Per the project's "no silent caps / surface what was dropped" discipline this should at least be observable. In a stereo workbench this never triggers, so blast radius is low, but the cap is invisible if it ever does.

### Dead member `params_` — assigned in the constructor body, never read

Finding-ID: AUDIT-BARRAGE-claude-04
Status:     open
Severity:   low
Surface:    adapters/workbench/workbench-app.cpp:142 (declaration), :29 (assignment)

`span<const ParameterDescriptor> params_;` is declared (line 142) and assigned `params_ = node_->parameters();` in the constructor body (line 29), but it is never read anywhere in `WorkbenchComponent`. The `ParameterView` is independently constructed from `node_->parameters()` in the member initializer list (line 26-27), so `params_` is dead state. It also aliases into whatever storage `node_->parameters()` returns, which is a latent footgun if that span ever points at per-call temporary storage. Remove the member, or use it as the single source the view also consumes.

### Discrete count clamp differs between the forward (onChange) and inverse (setNormalized) mappings

Finding-ID: AUDIT-BARRAGE-claude-05
Status:     open
Severity:   low
Surface:    adapters/workbench/parameter-view.cpp:24-25 (forward) vs :48-49 (inverse)

The forward map (combo `onChange`) uses the raw count: `const std::uint8_t count = d.discreteCount;` and `norm = (index + 0.5f) / count` (lines 19, 24). The inverse map (`setNormalized`) clamps: `const int count = row.descriptor.discreteCount < 2 ? 2 : row.descriptor.discreteCount;` (line 48). For `discreteCount >= 2` the two agree and the round-trip is exact. But for `discreteCount == 1` the inverse uses 2 while the forward uses 1, and for `discreteCount == 0` the forward divides by zero (`norm` becomes inf/NaN) while the combo also has zero items. These are degenerate descriptors, so the practical blast radius is low, but the two mappings should share one clamp helper so the inverse is provably the inverse of the forward, and a `discreteCount < 1` descriptor should be rejected at construction rather than producing NaN normalized values that flow into `setParameter`.

### Combo item labels are bare integer indices, not descriptor-derived names

Finding-ID: AUDIT-BARRAGE-claude-06
Status:     open
Severity:   low
Surface:    adapters/workbench/parameter-view.cpp:17-18

`row.combo->addItem(juce::String(i), i + 1)` labels each discrete bucket with its raw index ("0", "1", "2", …). The header comment for this view claims controls are "labelled from the descriptor" (parameter-view.h:9-12), but only the *row* label comes from the descriptor; the discrete *values* do not. If a discrete parameter represents an enum (filter type, oscillator shape), the operator sees meaningless integers instead of mode names. This is informational/low because `ParameterDescriptor` as shown in this chunk exposes only `name`/`discreteCount` with no per-value label table — so there may be nothing to render. Flagging the doc/behavior gap: either the descriptor needs a per-value label surface (and the view should consume it), or the header comment overstates what's labelled.

---

**Cross-model note for triage:** my highest-confidence signal is AUDIT-BARRAGE-claude-02 (prepare/process channel-count mismatch) — it's conditional on `SvfEffect`'s state sizing, which is in another chunk, so a sibling with that file in scope should confirm whether the OOB actually reifies. AUDIT-BARRAGE-claude-01 (discrete default desync) is self-contained in this chunk and reproducible from the diff alone.

I checked the CMake files (`CPM.cmake`, `dependencies.cmake`, both toolchains) for unpinned refs, missing hash verification, and download-failure handling: pins are explicit, `EXPECTED_HASH SHA256=` is present and `file(DOWNLOAD)` fatals on mismatch/failure, and `-fno-exceptions` on the embedded toolchains is consistent with exceptions appearing only in the desktop adapter — clean. `audio-block.h` is a correct non-owning, non-allocating view. The reference-to-member capture pattern (`OnChange& cb = onChange_; [&cb]{…}`) is subtle but correct — capturing a reference variable by reference binds the closure to `onChange_`, which outlives the child controls.