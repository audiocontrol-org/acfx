---
slug: workbench-audio-config
targetVersion: ""
---

# Audit log — workbench-audio-config

## 2026-06-27 — audit-barrage lift (end-govern-after_implement)

### AUDIT-20260627-01 — Saved device preference is silently clobbered by the fallback after a single unavailable-device launch

Finding-ID: AUDIT-20260627-01
Status:     open
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
Status:     open
Severity:   high
Per-lane:   codex=high
Decision:   single-model (gate-counted high)
Surface:    adapters/workbench/workbench-app.cpp:104-108, adapters/workbench/workbench-app.cpp:281-287

The startup warning path only remembers and compares `audioOutputDeviceName`. If the previously selected input device disappears but the output device remains the same, `setAudioChannels(2, 2, loaded.deviceState.get())` can fall back to another available input while `surfaceStartupIssues()` emits no message. That violates the US3 acceptance scenario for “a previously selected device” becoming unavailable, and it is especially visible for live input: the workbench may process the wrong physical input with no explanation.

Blast radius is high because this is a shipped correctness defect in the persistence/restore path a user will hit when USB or loopback inputs change. A reasonable fix is to capture the saved input device name as well, compare it against the active input device after restore, and include it in the same startup warning path instead of checking output only.

### AUDIT-20260627-03 — Completed checklist masks unexecuted manual acceptance

Finding-ID: AUDIT-20260627-03
Status:     open
Severity:   high
Per-lane:   codex=high
Decision:   single-model (gate-counted high)
Surface:    specs/workbench-audio-config/tasks.md:69, specs/workbench-audio-config/tasks.md:84, specs/workbench-audio-config/tasks.md:99, specs/workbench-audio-config/tasks.md:113, specs/workbench-audio-config/tasks.md:123

The task list marks US1/US2/US3/US4 and Scenario F acceptance items as `[X]` while the line text says those interactive checks are operator-owned and not actually completed in this implementation pass. This is especially load-bearing because `contracts/source-config.md` says most of the feature is validated through manual JUCE acceptance rather than unit tests, so marking these tasks complete changes the apparent verification state of the feature.

Blast radius is high: a downstream operator or unattended agent reading this artifact as written will reasonably conclude all acceptance criteria passed, including device routing, source switching, persistence restore, MIDI filtering, and rapid switch RT-safety. The reasonable fix is to keep the mechanical implementation tasks checked, but represent unexecuted manual acceptance as incomplete or as a separate clearly non-complete operator acceptance record, without marking the acceptance tasks done.

### AUDIT-20260627-04 — Ignored decode failure can accept an unreadable file as a valid source

Finding-ID: AUDIT-20260627-04
Status:     open
Severity:   high
Per-lane:   codex=high
Decision:   single-model (gate-counted high)
Surface:    adapters/workbench/audio-source.cpp:34-42

`useFilePlayer()` validates that a reader exists and that the reported length is positive, but it ignores the return value from `reader->read(&decoded, ...)` at line 37. If decoding fails or only partially succeeds for a corrupt/truncated file, the function still installs `fileBuffer_`, sets `hasFile_ = true`, and switches out of live mode. That directly contradicts the README promise that unreadable files are surfaced and do not produce “silent silence or placeholder audio.”

Blast radius is high because this is a shipped correctness path a user can hit by choosing a damaged but recognized audio file: the UI will believe the source switch succeeded, persistence may remember that file, and playback can become silent or invalid without an error. A reasonable fix is to check `read()`’s boolean result, clear/avoid installing the decoded buffer on failure, and throw `AudioSourceError` with the file path before changing source state.
