### Silent `AudioSourceError` swallow in `prepareToPlay` for interactive file selection

Finding-ID: AUDIT-BARRAGE-claude-01
Status:     open
Severity:   medium
Surface:    adapters/workbench/workbench-app.cpp (prepareToPlay, ~line 163–180 in the post-diff file)

The `prepareToPlay` try-block now contains a new branch `source_.useFilePlayer(sourceFile_)` exercised by interactive file selection (US2–US4). The surrounding catch is a pre-existing context line:

```cpp
} catch (const AudioSourceError& e) {
    sourceReady_ = false;
```

The exception message is captured in `e` and silently discarded; `sourceReady_ = false` is set but no notification reaches the user. When a user picks a non-audio file, a deleted file, or a file the decoder cannot open, the workbench goes silently silent: audio stops, no dialog, no status text, no log output visible in the UI. The env-var path (`ACFX_WORKBENCH_FILE`) had this same swallowed-error behavior, but that path is set by a developer and failure is typically obvious immediately. The interactive picker (new in this feature) is far more likely to produce this case for ordinary users. This also violates the project's explicit "no silent fallbacks" policy: the intent is to raise descriptive errors, not set a boolean and continue. A minimal fix surfaces the error either via `juce::NativeMessageBox::showMessageBoxAsync` (consistent with `surfaceStartupIssues`) or by re-throwing to let the caller handle it. Blast-radius: any user selecting a bad file gets silent failure with no recovery path visible.

---

### `surfaceStartupIssues` suppresses device-loss warning when `getCurrentAudioDevice()` returns null

Finding-ID: AUDIT-BARRAGE-claude-02
Status:     open
Severity:   medium
Surface:    adapters/workbench/workbench-app.cpp, `surfaceStartupIssues` (~line 265–285 in the post-diff file)

The function guards the device-fallback message behind `activeDevice.isNotEmpty()`:

```cpp
juce::String activeDevice;
if (auto* device = deviceManager.getCurrentAudioDevice())
    activeDevice = device->getName();
if (savedOutputDevice.isNotEmpty() && activeDevice.isNotEmpty()
    && savedOutputDevice != activeDevice)
    messages.add("Saved audio device \"" + savedOutputDevice + "\" ...");
```

If `getCurrentAudioDevice()` returns null (the device manager holds no active device after the restore attempt — possible when the saved device is gone and JUCE finds nothing to fall back to on this machine), `activeDevice` is empty string and the entire warning is suppressed. The function's own header comment says "Surface (never swallow) startup problems", and the `savedOutputDevice` string is non-empty (the user had a device), yet no message is shown. A correct guard would be `savedOutputDevice.isNotEmpty() && savedOutputDevice != activeDevice` — omitting the `activeDevice.isNotEmpty()` conjunct — so that the absence of an active device is also surfaced ("Saved audio device "X" was unavailable; no replacement found"). Blast-radius: on a machine where the preferred device is missing and no fallback is available, startup silently degrades with no actionable feedback.

---

### `saveSettings()` fires twice on every source-mode change

Finding-ID: AUDIT-BARRAGE-claude-03
Status:     open
Severity:   low
Surface:    adapters/workbench/workbench-app.cpp, `onSelectLive` / `onChooseFile` / `onChooseCancelled` lambdas (~lines 63–97) and `changeListenerCallback` (~line 263)

Each source-change lambda calls `restartAudio()` followed by an explicit `saveSettings()`. `restartAudio()` calls `deviceManager.restartLastAudioDevice()`, which causes the `AudioDeviceManager` to send a change notification. That notification — delivered asynchronously on the next message-pump cycle — fires `changeListenerCallback`, which calls `saveSettings()` again. The result is two identical saves per user-initiated source change. The explicit call and the async listener are both individually correct, but together they are redundant. The explicit `saveSettings()` in the lambdas can be removed; the change listener already handles persistence for every device-configuration change, including restarts. Keeping the explicit call only for the `onChooseCancelled` live-fallback path (where the restart is conditional) or removing it everywhere and relying solely on the listener would be cleaner. Blast-radius: no correctness impact; the saved state is correct in both saves; the only cost is two synchronous file writes per interaction.

---

### `juce::parseXML(file)` re-reads settings file from disk redundantly

Finding-ID: AUDIT-BARRAGE-claude-04
Status:     open
Severity:   low
Surface:    adapters/workbench/workbench-persistence.cpp:33–36

```cpp
const juce::File file = props->getFile();
if (file.existsAsFile() && file.getSize() > 0 && juce::parseXML(file) == nullptr)
    out.corrupt = true;
```

`getUserSettings()` (called two lines earlier) has already loaded and parsed the settings file into the `PropertiesFile` cache. The `juce::parseXML(file)` call then opens and parses the same file from disk a second time purely to detect outer-XML corruption, discarding the result immediately. The `PropertiesFile` already silently degrades on a corrupt file (returning empty properties), so a corrupt-outer-XML file would produce an empty `containsKey(kDeviceStateKey)` result and an empty `getValue(kSourceConfigKey)` — both resolvable to safe defaults — without this extra check. The second corruption check (unparseable device-state VALUE) at lines 41–43 is the more targeted one and does not involve re-reading the file. If outer-XML corruption detection is required, `PropertiesFile::getFile()` existence plus empty-properties result is a sufficient and cheaper proxy. The current code results in two disk reads per launch. Blast-radius: startup-time I/O only; no behavioral difference in the steady state.