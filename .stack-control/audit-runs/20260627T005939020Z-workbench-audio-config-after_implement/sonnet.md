### Saved unplayable/missing file source persists silently across restarts

Finding-ID: AUDIT-BARRAGE-claude-01
Status:     open
Severity:   medium
Surface:    adapters/workbench/workbench-app.cpp:88–102, 238–282

When a saved file path is restored from settings but the file no longer exists or is unplayable, the app enters a silent broken state and then **re-persists that broken state** before the user can intervene. The constructor restores `mode_` and `sourceFile_` from the loaded settings (lines ~88–95), then calls `setAudioChannels(2, 2, loaded.deviceState.get())`. That drives `prepareToPlay`, where `source_.useFilePlayer(sourceFile_)` throws `AudioSourceError`, `sourceReady_` is set to `false`, and the audio callback produces silence. Critically, `setAudioChannels` also fires a `ChangeBroadcaster` event on the device manager, which triggers `changeListenerCallback` → `saveSettings()` — persisting the bad `{file, "/broken/path"}` state again before the user has any chance to correct it. The cycle then repeats on every subsequent launch.

`surfaceStartupIssues` (lines ~263–282) only warns about two problems: corrupt XML and an unavailable audio *device*. It checks neither `!sourceFile_.existsAsFile()` nor `!sourceReady_`. So a user whose audio file was moved or deleted between sessions sees no warning, gets silence, and cannot tell from the UI why — the source bar still shows "file" mode with the stale path. The `onChooseCancelled` guard (lines ~72–79) that falls back to live when `sourceFile_` is missing fires only on an *active file-picker cancel*, not at startup.

A minimal fix: after `setAudioChannels` in the constructor, check `mode_ == SourceMode::file && !sourceReady_` and either (a) revert to live mode (consistent with `onChooseCancelled`) and add a message to `surfaceStartupIssues`, or (b) surface a distinct "saved audio file is unavailable" warning and clear the persisted file path. Either way, the broken path must not be re-saved on the startup `changeListenerCallback`.

---

### Redundant file re-read in `WorkbenchPersistence::load()` creates TOCTOU and bypasses JUCE error API

Finding-ID: AUDIT-BARRAGE-claude-02
Status:     open
Severity:   low
Surface:    adapters/workbench/workbench-persistence.cpp:32–35

```cpp
const juce::File file = props->getFile();
if (file.existsAsFile() && file.getSize() > 0 && juce::parseXML(file) == nullptr)
    out.corrupt = true;
```

`applicationProperties_.getUserSettings()` already reads and parses the settings file internally before this line executes. `juce::parseXML(file)` then opens and parses the *same file from disk* a second time. This creates a narrow but real TOCTOU window: the file could be replaced between the two reads (unlikely in practice, but structurally unsound). The redundant I/O also means two separate file reads on every launch. More importantly, it signals that JUCE's own PropertiesFile error reporting was bypassed rather than used — `PropertiesFile` exposes `getLastError()` / a `Listener` for load failures, and consulting those would eliminate the race entirely. As written, the check also re-parses the full file even when `getUserSettings()` already succeeded, so the only scenario it catches that JUCE doesn't is one where JUCE silently falls back to defaults on a corrupt file — which should be verified against actual JUCE behavior rather than assumed.

The second corruption check (lines ~37–40, device-state key present but `getXmlValue` returns null) is a distinct and legitimate check; it's only the full-file re-parse at lines 32–35 that is the concern here.

---

### Redundant `!loaded.source.filePath.empty()` guard is dead code

Finding-ID: AUDIT-BARRAGE-claude-03
Status:     open
Severity:   informational
Surface:    adapters/workbench/workbench-app.cpp:88

```cpp
if (loaded.source.mode == SourceMode::file && !loaded.source.filePath.empty()) {
```

`parse()` (`workbench-settings.cpp:33`) only reconstructs a `SourceMode::file` config when `!pathPart.empty()` — it explicitly falls back to `{live, ""}` otherwise. The condition `mode == SourceMode::file` therefore *implies* `filePath` is non-empty by the contract of `parse`. The `&& !loaded.source.filePath.empty()` guard is always true when the outer condition is true, so it is dead code. The `SourceConfig` contract in `workbench-settings.h` lines 26–27 even documents this invariant ("empty unless mode == file"). Removing the guard tightens alignment between the contract and the call site; leaving it in quietly weakens the stated invariant by suggesting a `{file, ""}` value is a possible runtime outcome even though `parse` prevents it.