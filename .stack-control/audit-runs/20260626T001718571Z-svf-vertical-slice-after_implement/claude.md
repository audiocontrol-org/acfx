Looking at chunk ba29de07a54f0920 — the plugin processor, Teensy adapter, and workbench adapters. I walked each diff for correctness, real-time safety, and cross-adapter consistency against the "same descriptor table" claim.

### Discrete combo ignores the descriptor default — UI desyncs from the effect at startup

Finding-ID: AUDIT-BARRAGE-claude-01
Status:     open
Severity:   medium
Surface:    adapters/workbench/parameter-view.cpp:19 (combo init) vs :38-39 (slider init)

The continuous path initializes its slider from the descriptor default — `row.slider->setValue(normalize(d, d.defaultValue), …)` (parameter-view.cpp:38-39). The discrete path does **not**: it hardcodes `row.combo->setSelectedItemIndex(0, juce::dontSendNotification)` (parameter-view.cpp:19), ignoring `d.defaultValue` entirely. Because the init uses `dontSendNotification`, `onChange` never fires, so the effect keeps its own descriptor default internally — but the combo displays index 0 regardless.

For any discrete parameter whose default index is not 0 (e.g. an SVF `mode` defaulting to bandpass), the UI shows the wrong selection on launch while the effect is actually running a different mode. The operator reads "lowpass," hears bandpass, and the first knob move snaps the effect to whatever the combo shows. Blast radius: a sketch-and-hear workbench whose stated purpose (FR-003 / SC-006) is faithful descriptor-driven UI silently misrepresents the effect's starting state. Fix: initialize the combo from the default, mirroring the slider — compute the default index via the same denormalize the descriptor uses (`floor(normalize(d, d.defaultValue) * count)`) and `setSelectedItemIndex(defaultIndex, dontSendNotification)`.

### Teensy: `setParameter` (loop thread) races `process` (audio ISR) on the shared `svf` with no synchronization

Finding-ID: AUDIT-BARRAGE-claude-02
Status:     open
Severity:   medium
Surface:    adapters/teensy/teensy-main.cpp:18-49 (AcfxSvfNode::update) vs :84-92 (loop)

`AcfxSvfNode::update()` runs in the Teensy Audio Library's software-interrupt context and calls `svf.process(audioBlock)` (teensy-main.cpp:31). `loop()` runs in thread context and calls `svf.setParameter(...)` three times per iteration (teensy-main.cpp:85-91). Both touch the *same* file-scope `acfx::SvfEffect svf` (teensy-main.cpp:15) with zero synchronization. `setParameter` typically recomputes filter coefficients; an SVF's coefficient set is multiple floats that are not updated atomically. The ISR can preempt `loop()` mid-update and run `process` against a torn coefficient set (new cutoff, stale resonance, or a half-written value).

For a recursive state-variable filter, a transiently inconsistent coefficient set isn't just a zipper artifact — depending on `SvfEffect`'s coefficient math (not visible in this chunk) it can momentarily push the filter outside its stable region and produce a loud transient or NaN that then propagates through the recursive state. Blast radius: an adopter flashing this to hardware gets intermittent glitches/instability under knob motion with no obvious cause, and the bug is timing-dependent so it won't reproduce on the desktop targets. A reasonable fix moves the `analogRead`→`setParameter` mapping *into* `update()` (same context as `process`, the pattern the workbench/plugin already follow by applying params at block top), or stages parameter writes through a single-producer atomic the ISR consumes.

### `WorkbenchAudioSource::fillBlock` contains a throwing path on the audio callback thread

Finding-ID: AUDIT-BARRAGE-claude-03
Status:     open
Severity:   medium
Surface:    adapters/workbench/audio-source.cpp:46-58 (fillBlock)

`fillBlock(juce::AudioBuffer<float>&)` is the per-block source pump — by name, signature, and role it runs on the audio device callback thread. It opens with `if (!configured_) throw AudioSourceError("fillBlock called before prepare().")` (audio-source.cpp:47-48). `AudioSourceError`'s constructor calls `what.toStdString()` (audio-source.h:33-34), a heap allocation, and a `throw` triggers exception unwinding — both forbidden in the audio path (CLAUDE.md "Real-time safety: no heap allocation or locks in any process()/audio-callback path"; Constitution V). Even though the guard is rarely taken, a `throw` in the RT path is exactly the kind of latent allocation the constitution bans, and it differs in kind from the legitimate throws in the *setup* methods (`useFilePlayer`, `prepare`), which are not on the audio thread.

Secondarily, `transport_.start()` is invoked lazily from this same path (audio-source.cpp:53-54); auto-starting transport from the audio thread is a questionable RT call best done in `prepare()`. Blast radius: this is the workbench's playback hot path, so any adopter who copies this adapter as the "thin adapter" reference (the explicit framing in CLAUDE.md) inherits an RT-unsafe pattern. Fix: replace the `fillBlock` precondition throw with a non-throwing early-return (or assert in debug only), and move the transport start out of the callback into `prepare()`.

### Plugin `getStateInformation`/`setStateInformation` no-ops silently drop all parameter state on project save/reload

Finding-ID: AUDIT-BARRAGE-claude-04
Status:     open
Severity:   medium
Surface:    adapters/plugin/plugin-processor.h:43-45

Both state hooks are empty (`getStateInformation(juce::MemoryBlock&) override {}` / `setStateInformation(const void*, int) override {}`), with a comment framing this as "intentionally no-op, not a silent fallback" and pointing at plan.md "Storage N/A." The honesty of the comment is good, but the behavioral consequence is understated: many DAW hosts persist plugin parameters *only* via `getStateInformation`, not independently. In those hosts, saving a session and reopening it resets every SVF parameter to its default — the user's cutoff/resonance/mode are lost on every reload. The behavior is host-dependent (some DAWs re-push automation), which makes it worse, not better: it works in the developer's host and fails in the adopter's.

Per the invariant-first-boundary driver, "Storage N/A" is stated as an exclusion rather than an invariant-plus-exception. The honest boundary is: "the milestone persists no plugin state; in hosts that rely on getStateInformation, all parameter values reset on reload." Blast radius: an adopter evaluating this as a usable DAW plugin will hit lost-settings-on-reload immediately, and nothing in the processor surfaces that to them at runtime. Either serialize the parameter values (a few lines over the existing parameter table) or document the reload-resets-state behavior in the README acceptance notes so it isn't discovered the hard way.

### Discrete combo items are labelled by numeric index, hiding the meaning of each discrete value

Finding-ID: AUDIT-BARRAGE-claude-05
Status:     open
Severity:   low
Surface:    adapters/workbench/parameter-view.cpp:16-18

The discrete control populates its combo with `row.combo->addItem(juce::String(i), i + 1)` for `i` in `[0, d.discreteCount)` (parameter-view.cpp:16-18) — every item's visible label is just its ordinal ("0", "1", "2"). For an SVF `mode` parameter, the UI shows "0 / 1 / 2" instead of "Lowpass / Bandpass / Highpass." This is a descriptor-completeness gap as much as a UI one: `ParameterDescriptor` (consumed here via `dsp/parameter.h`, defined in another chunk) apparently carries `discreteCount` but no per-value labels, so the auto-rendered UI cannot name the choices.

Blast radius is contained — it's a usability defect in a developer-facing workbench, not a correctness bug — hence low. But it undercuts the "descriptor-driven, no per-effect UI code" goal (SC-006): a faithful auto-UI for a mode switch needs the value names to come from the descriptor. Fix is a small descriptor extension (optional `discreteLabels` span) that the combo reads when present and falls back to ordinals when absent.

---

I checked the plugin `processBlock` channel handling (bounded correctly by `isBusesLayoutSupported` to mono/stereo, `kMaxChannels=8` over-provisions but is safe), the MIDI-CC→normalized mapping in `midi-binding.h` (`/127.0f` is consistent with the slider/combo normalized space), and the centre-of-bucket round-trip between the combo's `onChange` and `setNormalized` (they are proper inverses for in-range values) — those came back clean. The five findings above are the ones I'd want triaged.