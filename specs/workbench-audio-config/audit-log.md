---
slug: workbench-audio-config
targetVersion: ""
---

# Audit log — workbench-audio-config

## 2026-06-27 — audit-barrage lift (end-govern-after_implement)

### AUDIT-20260627-01 — Saved device preference is silently clobbered by the fallback after a single unavailable-device launch

Finding-ID: AUDIT-20260627-01
Status:     fixed (commit 1f03d96 — preserve saved device-state during a fallback session; refresh the snapshot on deliberate change)
Severity:   high
Per-lane:   claude=high
Decision:   single-model (gate-counted high)
Surface:    adapters/workbench/workbench-app.cpp:135-139 (destructor `saveSettings()`), :291-293 (`changeListenerCallback`), :273-274 (`saveSettings`/`currentSourceConfig`)

The destructor unconditionally re-persists the live device-manager state on quit:
```cpp
~WorkbenchComponent() override {
    deviceManager.removeChangeListener(this);
    saveSettings(); // persist the final selection on quit (FR-006)
    ...
```
`saveSettings()` calls `persistence_.save(deviceManager, ...)`, which writes `deviceManager.createStateXml()` — i.e. *whatever device is currently open*. Combine this with the FR-009 fallback path: on launch, `setAudioChannels(2, 2, loaded.deviceState.get())` restores the saved device, but if that device is unavailable JUCE's `selectDefaultDeviceOnFailure` opens a different one (the code even warns the user about this in `surfaceStartupIssues`). When the user then quits — or merely edits buffer size in the Audio Settings dialog, which fires `changeListenerCallback → saveSettings` — the **fallback** device name overwrites the saved preferred device name in the settings file.

Blast radius: the common laptop-plus-external-interface case. User saves "USB DAC" as their device, launches once with the interface unplugged, quits. The settings file now says "Built-in Output". Next launch with the interface plugged back in opens Built-in Output, not the USB DAC — the deliberately-saved preference is permanently lost, silently, defeating the stated purpose of FR-006. The warning the user saw at startup gave no hint that their preference was about to be discarded. A reasonable fix: when the loaded `deviceState`'s preferred output/input device name differs from the device actually opened (the fallback case detected in `surfaceStartupIssues`), do **not** overwrite the saved device-state block on quit/auto-save — preserve the original preferred-device XML so a temporarily-absent device is reselected when it returns. At minimum, gate the destructor/auto save so a known-fallback session does not clobber the stored preference.

### AUDIT-20260627-02 — Missing-input-device fallback is not surfaced

Finding-ID: AUDIT-20260627-02
Status:     fixed (commit 1f03d96 — capture + compare + surface the saved INPUT device too)
Severity:   high
Per-lane:   codex=high
Decision:   single-model (gate-counted high)
Surface:    adapters/workbench/workbench-app.cpp:104-108, adapters/workbench/workbench-app.cpp:281-287

The startup warning path only remembers and compares `audioOutputDeviceName`. If the previously selected input device disappears but the output device remains the same, `setAudioChannels(2, 2, loaded.deviceState.get())` can fall back to another available input while `surfaceStartupIssues()` emits no message. That violates the US3 acceptance scenario for “a previously selected device” becoming unavailable, and it is especially visible for live input: the workbench may process the wrong physical input with no explanation.

Blast radius is high because this is a shipped correctness defect in the persistence/restore path a user will hit when USB or loopback inputs change. A reasonable fix is to capture the saved input device name as well, compare it against the active input device after restore, and include it in the same startup warning path instead of checking output only.

### AUDIT-20260627-03 — Completed checklist masks unexecuted manual acceptance

Finding-ID: AUDIT-20260627-03
Status:     dispositioned (commit 1f03d96 — operator-acknowledged; prominent MANUAL ACCEPTANCE STATUS banner added to tasks.md so the gate-only [X] marks are not read as passed acceptance; Scenarios B-F remain operator-owned before graduation)
Severity:   high
Per-lane:   codex=high
Decision:   single-model (gate-counted high)
Surface:    specs/workbench-audio-config/tasks.md:69, specs/workbench-audio-config/tasks.md:84, specs/workbench-audio-config/tasks.md:99, specs/workbench-audio-config/tasks.md:113, specs/workbench-audio-config/tasks.md:123

The task list marks US1/US2/US3/US4 and Scenario F acceptance items as `[X]` while the line text says those interactive checks are operator-owned and not actually completed in this implementation pass. This is especially load-bearing because `contracts/source-config.md` says most of the feature is validated through manual JUCE acceptance rather than unit tests, so marking these tasks complete changes the apparent verification state of the feature.

Blast radius is high: a downstream operator or unattended agent reading this artifact as written will reasonably conclude all acceptance criteria passed, including device routing, source switching, persistence restore, MIDI filtering, and rapid switch RT-safety. The reasonable fix is to keep the mechanical implementation tasks checked, but represent unexecuted manual acceptance as incomplete or as a separate clearly non-complete operator acceptance record, without marking the acceptance tasks done.

### AUDIT-20260627-04 — Ignored decode failure can accept an unreadable file as a valid source

Finding-ID: AUDIT-20260627-04
Status:     fixed (commit 1f03d96 — useFilePlayer checks reader->read() and throws before mutating source state)
Severity:   high
Per-lane:   codex=high
Decision:   single-model (gate-counted high)
Surface:    adapters/workbench/audio-source.cpp:34-42

`useFilePlayer()` validates that a reader exists and that the reported length is positive, but it ignores the return value from `reader->read(&decoded, ...)` at line 37. If decoding fails or only partially succeeds for a corrupt/truncated file, the function still installs `fileBuffer_`, sets `hasFile_ = true`, and switches out of live mode. That directly contradicts the README promise that unreadable files are surfaced and do not produce “silent silence or placeholder audio.”

Blast radius is high because this is a shipped correctness path a user can hit by choosing a damaged but recognized audio file: the UI will believe the source switch succeeded, persistence may remember that file, and playback can become silent or invalid without an error. A reasonable fix is to check `read()`’s boolean result, clear/avoid installing the decoded buffer on failure, and throw `AudioSourceError` with the file path before changing source state.

## 2026-06-27 — audit-barrage lift (end-govern-after_implement)

### AUDIT-20260627-05 — `outcome: "override-eligible"` recorded with 4 unresolved HIGH findings and empty `closedInLoopFindings`

Finding-ID: AUDIT-20260627-05
Status:     dispositioned (governance-artifact recursion, not a feature defect: the barrage audited govern's own convergence record because commit 1f03d96 committed it into the feature diff. Resolved by gitignoring + untracking the regenerable govern artifacts — .stack-control/audit-runs/ and .stack-control/govern/convergence/ — so they no longer enter the audited diff. The convergence record is regenerated by each govern run and read from disk by the graduate gate. Recommended tooling feedback: `stackctl govern` should exclude its own artifact paths (.stack-control/) from the audited diff so governance output cannot recursively become a finding.)
Severity:   high
Per-lane:   claude=high
Decision:   adjudicated (gate-counted high) — blast-radius=unstated, reachability=unstated, fix-debt=no; no down-calibration signal — high retained.
Surface:    .stack-control/govern/convergence/impl__design-feature-workbench-audio-config.json:13-71 (liftedFindings / closedInLoopFindings / outcome)

The record converges by *lifting*, not *closing*: `liftedFindings` holds four `high`-severity items — silent clobber of the saved device preference, an unsurfaced missing-input-device fallback, a completed checklist masking unexecuted manual acceptance, and an ignored decode failure accepting an unreadable file — while `closedInLoopFindings` is `[]` and `outcome` is `"override-eligible"`. Read literally, this artifact records that the feature became eligible to proceed with **zero** highs resolved in-loop; every one was deferred to tracking. Two of these (the device-preference clobber and the accepted-unreadable-file) are concrete correctness defects, not spec ambiguities.

There is also a drift signal against the commit history in this very range: `1f03d96 "address govern findings AUDIT-01/02/04 + 03"` claims these were addressed, yet this committed record still shows them lifted-and-open with nothing closed and the outcome unchanged — so either the record is a stale pre-fix snapshot left committed as if authoritative, or the fixes did not flow back into the convergence state. Blast radius: a release gate or operator that keys "safe to advance" off `outcome: "override-eligible"` would ship the feature carrying four unresolved high-severity correctness/process findings, with nothing in the artifact contradicting that reading. Fix: regenerate the convergence record after the fix commits so closed findings move to `closedInLoopFindings`, and confirm the `override-eligible` outcome still holds against the current head rather than against a base that predates the fixes.

### AUDIT-20260627-06 — File chooser advertises FLAC/MP3, but the decoder only registers WAV/AIFF

Finding-ID: AUDIT-20260627-06
Status:     fixed (this commit — SourceBar file filter derived from WorkbenchAudioSource::supportedFileWildcard() (formatManager_.getWildcardForAllFormats()), so the picker only offers formats the decoder registers; filter + decoder kept in lock-step)
Severity:   high
Per-lane:   claude=high
Decision:   single-model (gate-counted high)
Surface:    adapters/workbench/source-bar.cpp:20-21 (filter) vs. adapters/workbench/audio-source.cpp:9 (`registerBasicFormats()`)

The async chooser is launched with the wildcard set `"*.wav;*.aiff;*.aif;*.flac;*.mp3"` (source-bar.cpp:20-21), so the file dialog presents `.flac` and `.mp3` files as valid, selectable choices. But the only formats the source ever registers are the basic set: `WorkbenchAudioSource() { formatManager_.registerBasicFormats(); }` (audio-source.cpp:9). `juce::AudioFormatManager::registerBasicFormats()` registers **WAV and AIFF only** — it does not register `FlacAudioFormat` or `MP3AudioFormat`. Consequently `formatManager_.createReaderFor()` for a chosen `.flac`/`.mp3` returns `nullptr`, and `useFilePlayer()` throws on every such pick.

Blast radius: a user with a FLAC or MP3 library — the two most common lossless/lossy library formats — picks a file the workbench explicitly offered them, and gets a hard error *every single time*. This directly defeats the README's promise of "a **file** you pick with **Load file…** (looped through the filter)" (README.md). It is surfaced (not silent), so it's not a safety bug, but it is a broken affordance the picker actively advertises. A reasonable fix is to either (a) register the extra formats in the constructor (`formatManager_.registerFormat(new juce::FlacAudioFormat(), false)`, plus the MP3 format where the build enables it) so the filter is honest, or (b) trim the filter to `"*.wav;*.aiff;*.aif"` so the picker never offers what the decoder can't read. The filter and the registered format set must be kept in lock-step.
