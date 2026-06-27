I'll audit this chunk (workbench-app.cpp + the persistence/settings TUs) as an independent reviewer in the barrage. I walked the diff focusing on the persistence/restore lifecycle, the source-reconfigure path, and the FR-009 surfacing logic.

### Env var silently overrides a deliberately-saved "live input" selection (contradicts FR-006 and the in-code comment)

Finding-ID: AUDIT-BARRAGE-claude-01
Status:     open
Severity:   high
Surface:    adapters/workbench/workbench-app.cpp (constructor restore block, ~lines 89–101) + adapters/workbench/workbench-settings.cpp:serialize/parse

The restore block reads:

```cpp
if (loaded.source.mode == SourceMode::file && !loaded.source.filePath.empty()) {
    mode_ = SourceMode::file; sourceFile_ = ...;
} else if (const char* path = std::getenv("ACFX_WORKBENCH_FILE")) {
    mode_ = SourceMode::file; sourceFile_ = juce::File(juce::String::fromUTF8(path));
}
```

The comment directly above asserts: *"Saved settings take precedence over the ACFX_WORKBENCH_FILE first-run convenience … the env var only seeds the source when there is nothing saved yet."* The code does **not** implement that. `serialize()` writes `"live"` for a live selection, and `parse("live")` returns `SourceConfig{live, ""}` — which is **indistinguishable from "no saved source"**. Both fall through to the `else if`, so whenever `ACFX_WORKBENCH_FILE` is exported, a user who explicitly selected *live input* via the source bar gets silently reverted to the env-var file on **every** relaunch. `saveSettings()` keeps writing `"live"`, but it never sticks, and there is no UI affordance to clear the env var. Blast radius: any operator who set the FR-004 convenience env var once (e.g. in their shell profile) can never persist a live-input selection — persistence appears broken for the live case, with no error and no explanation. The fix requires `LoadedSettings` to carry a *"source key was present"* signal (e.g. `props->containsKey(kSourceConfigKey)`), and the restore to honor a present-but-live saved source over the env var.

### Saved-file-gone at startup has no fallback-to-live, unlike the cancel path and unlike the device fallback

Finding-ID: AUDIT-BARRAGE-claude-02
Status:     open
Severity:   medium
Surface:    adapters/workbench/workbench-app.cpp (restore block + `onChooseCancelled` ~lines 73–80; `surfaceStartupIssues` ~lines 258–283)

`onChooseCancelled` carefully falls back to live "only if the current selection is a file with no usable file (FR-009)": `if (mode_ == file && !sourceFile_.existsAsFile()) { mode_ = live; ... }`. But the **startup** restore performs no equivalent check: a saved file path that has since been deleted/unmounted is restored verbatim as `mode_ = file, sourceFile_ = <missing>`, and `prepareToPlay` then throws `AudioSourceError` → `sourceReady_ = false`. The workbench boots into a dead no-source state. Worse, `surfaceStartupIssues()` only reports `corrupt` settings and a vanished **audio device** — it never surfaces a vanished **source file**, so the person sees a silent non-working workbench at launch (the FR-009 "saved device unavailable" notification has a sibling case here that is unhandled). This is an asymmetry: a vanished device gets graceful fallback + a notification; a vanished file gets neither at startup. A reasonable fix mirrors the cancel logic in the restore block — if the restored file does not `existsAsFile()`, fall back to live and add a message to `surfaceStartupIssues`.

### FR-009 "saved device unavailable" notification depends on an untested JUCE-internal XML attribute name + raw name-string equality

Finding-ID: AUDIT-BARRAGE-claude-03
Status:     open
Severity:   medium
Surface:    adapters/workbench/workbench-app.cpp:~line 86 (`getStringAttribute("audioOutputDeviceName")`) and `surfaceStartupIssues` ~lines 268–277

The entire "Saved audio device X was unavailable; using Y instead" notification hinges on two brittle assumptions: (1) the device name was persisted under the exact key `"audioOutputDeviceName"` in `AudioDeviceManager::createStateXml()`, and (2) `getCurrentAudioDevice()->getName()` returns a string byte-equal to that saved attribute. If JUCE stored the device under a different/combined attribute (e.g. setups where input==output serialize differently), `savedOutputDevice` comes back empty and the warning *never fires* even when the device genuinely changed — a silent failure of a user-facing FR-009 surface. Conversely, any benign name normalization difference produces a **false** "unavailable" warning. There is no fixture in this chunk exercising the device-vanished path, so neither failure mode is caught. The reasonable mitigation is to (a) round-trip the attribute name against an actual `createStateXml()` output in a test, or (b) source the comparison name from the same device-setup struct JUCE used to restore, rather than reaching into the persisted XML by a hardcoded string.

### First-run MIDI auto-enable is keyed on "no saved device state at all", so a newly-connected controller defaults to inert

Finding-ID: AUDIT-BARRAGE-claude-04
Status:     open
Severity:   medium
Surface:    adapters/workbench/workbench-app.cpp:~lines 119–124

The auto-enable-all-MIDI-inputs default fires only `if (loaded.deviceState == nullptr)`. After the very first launch, `saveSettings()` writes a device-state XML, so `deviceState` is non-null thereafter. Consequence: if the user later **plugs in a MIDI controller that was not connected at first launch**, it is absent from the restored enabled set and therefore defaults to *disabled* — the CC bindings stay inert until the user manually enables it in the Audio Settings window. For a "sketch-and-hear" workbench whose point is live MIDI control, a freshly-plugged controller silently doing nothing is a real trap, and it is keyed on the conflation of "first run" with "any prior saved state." This may be partly intended under US4 (explicit per-device selection), but the default-to-disabled for a *never-before-seen* device is distinct from a user *explicitly* disabling one (SC-005) and deserves either documentation or enabling newly-appeared devices by default. Worth disambiguating in the spec and code comment, since the current behavior reads as "MIDI just stopped working when I changed cables."

### Redundant double-save on every source-bar change (explicit `saveSettings()` plus change-listener-triggered save)

Finding-ID: AUDIT-BARRAGE-claude-05
Status:     open
Severity:   low
Surface:    adapters/workbench/workbench-app.cpp:~lines 60–80 (source-bar callbacks) + `changeListenerCallback` ~line 254

Each source-bar callback does `restartAudio(); saveSettings();`. `restartAudio()` → `restartLastAudioDevice()` triggers an `AudioDeviceManager` change broadcast, which lands in `changeListenerCallback` → `saveSettings()`. So every live/file switch writes the settings file twice (once synchronously, once on the deferred change message). This is harmless to correctness — both writes capture the already-updated `mode_`/`sourceFile_`, and `saveIfNeeded()` is cheap — but it is dead redundancy that muddies the "single source of persistence" story and doubles disk churn on every device/source edit. The explicit `saveSettings()` calls in the three source-bar callbacks are unnecessary given the change listener already persists on every device-config change; removing them (relying solely on the listener) would simplify the model. Calling out per the project's bug-factory hygiene rather than as a defect.

---

These five are what I'd surface. I specifically checked the serialize/parse round-trip (clean — embedded newlines in paths round-trip correctly because `parse` takes everything after the first newline as the path), the corruption-detection logic in `WorkbenchPersistence::load` (sound — distinguishes missing/empty from present-but-unparseable), the message-thread-only invariant on `mode_`/`sourceFile_` vs `prepareToPlay` (sound — `restartLastAudioDevice()` is synchronous, so `prepareToPlay` reads the updated state with a happens-before), and the construction-time ordering of `addChangeListener` after restore (correct — avoids construction-time saves). My highest-confidence finding is **claude-01**: the env-var-vs-saved-live precedence directly contradicts both FR-006 and its own adjacent comment, and silently defeats persistence for any operator using the documented env-var convenience.