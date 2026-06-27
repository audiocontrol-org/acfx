I'll perform this audit independently, walking chunk `d0c555613386cd51` carefully against the diff.

### File-chooser filter advertises FLAC/MP3 the decoder cannot open

Finding-ID: AUDIT-BARRAGE-claude-01
Status:     open
Severity:   high
Surface:    adapters/workbench/source-bar.cpp:21 (filter `"*.wav;*.aiff;*.aif;*.flac;*.mp3"`) ↔ adapters/workbench/audio-source.cpp:9 (`registerBasicFormats()`)

The `SourceBar` file chooser is constructed with the filter `"*.wav;*.aiff;*.aif;*.flac;*.mp3"` (source-bar.cpp:21), so the picker actively presents `.flac` and `.mp3` files as selectable. But `WorkbenchAudioSource`'s constructor calls `formatManager_.registerBasicFormats()` (audio-source.cpp:9), which in JUCE registers **only** `WavAudioFormat` and `AiffAudioFormat` — FLAC and MP3 require explicit `registerFormat(new FlacAudioFormat()…)` / `MP3AudioFormat` registration (gated behind `JUCE_USE_FLAC` / `JUCE_USE_MP3AUDIOFORMAT`). A user who selects an offered `.flac`/`.mp3` will reach `useFilePlayer()`, where `formatManager_.createReaderFor()` returns null and the code throws `AudioSourceError("…file cannot be opened/decoded")`.

The blast radius: this is a correctness defect a normal consumer will hit on the first natural action — pick a standard FLAC the workbench itself offered. The README (chunk d0c555613386cd51) sells "the **source bar** switches … to a **file** you pick with **Load file…**" with no format caveat, so the contradiction is also documented as working. A reasonable fix is to either (a) narrow the chooser filter to `"*.wav;*.aiff;*.aif"` to match `registerBasicFormats()`, or (b) register the FLAC/MP3 formats in the constructor (and confirm the JUCE module flags are enabled) so the advertised filter is honest. The filter and the registered format set must be kept in lockstep — right now they are not.

### ROADMAP marks the feature `status: planned` while nine commits implement it

Finding-ID: AUDIT-BARRAGE-claude-02
Status:     open
Severity:   medium
Surface:    ROADMAP.md:28-30

The ROADMAP diff adds `analyze-clean: yes` to the `feature/workbench-audio-config` block but leaves `status: planned` untouched, while the audited range contains nine implementation commits (T001–T019, polish, acceptance). An agent or operator consulting ROADMAP.md as the source of truth for "what is started / what is open to pick up" reads `planned` and concludes the feature is unbuilt — the more natural reading is the wrong one, and nothing adjacent corrects it (the `analyze-clean: yes` line, if anything, reinforces "still pre-implementation gating").

Blast radius is moderate: an unattended agent doing roadmap-driven work could attempt to (re)scope/re-implement an in-flight feature, or a human could mis-report status. A reasonable fix is to advance the status field to whatever your vocabulary uses for in-implementation (e.g. `in-progress` / `building`) in the same change that records implementation, so the roadmap doesn't lag the branch by an entire phase. If the status field is deliberately only advanced at ship time, state that invariant in the roadmap header so the staleness is intentional and legible rather than reading as drift.

### Re-launching the chooser destroys an in-flight `FileChooser` mid-modal

Finding-ID: AUDIT-BARRAGE-claude-03
Status:     open
Severity:   low
Surface:    adapters/workbench/source-bar.cpp:18-34 (`openChooser`)

`openChooser()` reassigns the member `chooser_ = std::make_unique<juce::FileChooser>(…)` (source-bar.cpp:20) on every "Load file…" click, then `launchAsync` with a lambda capturing `this`. If the user clicks "Load file…" while a chooser launched by a prior click is still open, the `std::make_unique` assignment destroys the previous `FileChooser` object while its native/async modal is still pending. The async completion lambda captures `this` (not the chooser), so the bar object survives, but tearing down an in-flight `FileChooser` is platform-dependent behavior and at best produces a confusing double-dialog / orphaned callback.

Blast radius is small — it requires a deliberate double-open and JUCE largely tolerates it — so this is hygiene rather than a guaranteed crash. A reasonable fix is to guard re-entry: if `chooser_` is non-null and active, ignore the new click (or disable `fileButton_` until the async callback fires and clears `chooser_`). The comment at source-bar.cpp:19 already notes "The FileChooser must outlive the launch, so it is owned by the bar" — that ownership reasoning should be extended to the re-entrant case.

### Reconfigure invariant depends on an unshown `release()` resetting `configured_`

Finding-ID: AUDIT-BARRAGE-claude-04
Status:     open
Severity:   low
Surface:    adapters/workbench/audio-source.h:15-46, adapters/workbench/audio-source.cpp:11-13,46-48

The header's contract was rewritten from one-shot setup to a repeatable "release-before-reconfigure" lifecycle (audio-source.h:15-22): `useFilePlayer`/`useLiveInput` now throw when `configured_.load(acquire)` is true, and the narrative asserts the workbench "calls `release()` … before reselecting, then `prepare()`." The entire correctness of the new reconfigure path therefore hinges on `release()` setting `configured_` back to `false` — but `release()` is not in this chunk, so this audit cannot confirm it does. If `release()` does not clear `configured_` (or clears it without a matching `release` memory ordering against the `acquire` load here), then the *second* source selection always throws and US2/US3 source-switching is silently broken, or the audio thread could observe a half-reassigned `fileBuffer_`.

Blast radius is contained to "verify the cross-chunk piece exists," and the feature commits suggest the path was exercised, so I rate this low. The actionable step for the operator: confirm in `audio-source.cpp` (the unchanged portion / other chunk `5420a3615ad2e99c` orchestration) that `release()` stores `configured_ = false` with `memory_order_release`, and that the workbench actually calls `release()` inside the audio-stopped window before each reselect — the new header comment promises both but neither is visible in the changed lines.

### README's "never silent silence or placeholder audio" is an unverifiable absolute

Finding-ID: AUDIT-BARRAGE-claude-05
Status:     open
Severity:   low
Surface:    README.md:65-67

The README addition states failures "are surfaced and leave the workbench in a safe, usable state — **never** silent silence or placeholder audio." This is an unconditional guarantee over every failure mode (device won't open, unreadable file, corrupt saved settings, mid-stream device loss), but the code visible in this chunk only demonstrates *throwing* on bad input (audio-source.cpp) and a cancel callback (source-bar.cpp) — the "surfaced and safe state" recovery lives in workbench-app.cpp (other chunk) and is not provable from here. Absolute words like "never" in user-facing docs become drift the moment one failure path (e.g. a device that opens but delivers no callbacks) doesn't route through the surfacing logic.

Blast radius is low — it's a documentation-confidence issue, not a runtime defect — but per project guidelines, claims should track what the code provably does. A reasonable fix is to soften to the demonstrated behavior ("open/decode/settings failures raise a visible error and revert to the last working source") rather than an all-quantifier guarantee, or to ensure the workbench chunk actually has a fixture covering each enumerated failure the sentence promises.