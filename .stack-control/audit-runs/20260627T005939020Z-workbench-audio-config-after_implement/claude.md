I'll walk this chunk carefully, focusing on the persistence/restore lifecycle, the serde seam, and the new construction/destruction flow in `workbench-app.cpp`.

### Saved device preference is silently clobbered by the fallback after a single unavailable-device launch

Finding-ID: AUDIT-BARRAGE-claude-01
Status:     open
Severity:   high
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

### SourceBar UI is not synchronized with the restored source selection

Finding-ID: AUDIT-BARRAGE-claude-02
Status:     open
Severity:   medium
Surface:    adapters/workbench/workbench-app.cpp:90-102 (restore block) + cross-file `adapters/workbench/source-bar.{h,cpp}`

After loading persisted settings the code sets the audio-thread-facing state (`mode_`, `sourceFile_`) from `loaded.source` / the env var, and `prepareToPlay` correctly reconfigures the audio from it. But nothing pushes that restored selection back into `sourceBar_` — the widget is `addAndMakeVisible`'d and its callbacks are wired, yet it is never told "you are currently in file mode showing X". 

Blast radius: on every relaunch where a file source was persisted, the audio plays the restored file (correct) while the source bar displays its default/blank state (drift). The operator sees a UI that disagrees with what they hear, and a subsequent "select live" toggle may behave oddly because `onSelectLive` early-returns on `mode_ == live` while the bar's own visual state was never authoritative. I cannot see `source-bar.h` in this chunk to confirm SourceBar exposes a "set current selection" entry point, so this is flagged as a cross-file gap: either SourceBar must offer a setter the restore block calls, or this chunk should call it. If SourceBar genuinely has no visible state, downgrade — but then the diff is missing the surface that would make a restored file selection visible.

### Every source switch writes the settings file twice

Finding-ID: AUDIT-BARRAGE-claude-03
Status:     open
Severity:   low
Surface:    adapters/workbench/workbench-app.cpp:60-86 (source-bar callbacks) + :291-293 (`changeListenerCallback`)

Each source-bar callback does `restartAudio(); saveSettings();`. `restartAudio()` calls `deviceManager.restartLastAudioDevice()`, which is a device-manager configuration change and therefore triggers `changeListenerCallback → saveSettings()`. So every live/file toggle persists the settings file once via the change listener and once via the explicit trailing `saveSettings()` — two full XML serializations + disk writes (`saveIfNeeded`) per toggle.

Blast radius: redundant I/O, not a correctness bug — `currentSourceConfig()` reads the same `mode_` both times. But it is duplicated logic that obscures the single-responsibility intent, and if `createStateXml()` timing relative to `restartLastAudioDevice()` ever changes, the two saves could capture different states. A reasonable cleanup: rely on the change-listener save alone for device-driven restarts and drop the explicit `saveSettings()` in the callbacks (or vice-versa), so there is one persistence point per logical change.

### Empty `ACFX_WORKBENCH_FILE` produces an invalid file-mode source instead of being treated as unset

Finding-ID: AUDIT-BARRAGE-claude-04
Status:     open
Severity:   low
Surface:    adapters/workbench/workbench-app.cpp:96-99

```cpp
} else if (const char* path = std::getenv("ACFX_WORKBENCH_FILE")) {
    mode_ = SourceMode::file;
    sourceFile_ = juce::File(juce::String::fromUTF8(path));
}
```
`std::getenv` returns a non-null pointer for an environment variable that is *set but empty* (`ACFX_WORKBENCH_FILE=`). The branch then forces `mode_ = file` with `sourceFile_ = juce::File("")`, an invalid file. `prepareToPlay` will call `source_.useFilePlayer(sourceFile_)`, throw `AudioSourceError`, and surface a "source unavailable" error — when the user almost certainly meant "no file override."

Blast radius: a minor first-run convenience footgun; an empty export (common in shell profiles) flips the workbench into a broken file mode rather than live input. The parser-side seam (`workbench-settings.cpp`) is careful to treat empty paths as `live`, so this UI-boundary branch is inconsistent with that invariant. Reasonable fix: guard on `path != nullptr && path[0] != '\0'` (or trim) before entering file mode, matching the empty-path-is-live invariant the serde layer already enforces.

---

I also checked the `serialize`/`parse` seam in `workbench-settings.cpp` for the channels a new parser branch opens (value, state, multiline/composition per channel-enumeration): the "everything after the first newline is the path, verbatim" rule round-trips empty/garbage/unknown-mode/`file`-with-no-path all to the safe `{live, ""}` default symmetrically with `serialize`, and embedded-newline paths survive because only the *first* newline is the delimiter — that surface is clean and matches its contract. The corruption detection in `workbench-persistence.cpp::load` (whole-file-non-XML and present-but-unparseable device-state value) is sound and correctly null-guards `deviceState`. The first-run-only MIDI auto-enable (`loaded.deviceState == nullptr`) and the `addChangeListener`-added-last ordering correctly avoid construction-time save storms. My findings concentrate where the restore/persist lifecycle re-writes state: the destructor clobber (01) is the one I'd treat as a real defect against FR-006/FR-009.