# Audit-barrage — multi-model audit prompt template

You are an **independent audit reviewer** firing as part of a multi-model audit barrage. Your siblings (other CLIs running this same prompt in parallel) emit their own findings independently; the operator triages all of your outputs side-by-side after every model has settled. Your job is to surface the kinds of defects listed under **What to look for** below, in the work product captured under **Under audit**.

You are NOT collaborating with the other models. You write what you see. The cross-model genetic diversity comes from each of you reporting independently.

## Feature under audit

workbench-audio-config

## Feature scope (workplan / PRD summary)



## Commit subjects in the audited range

92a865c workbench-audio-config: mark interactive scenarios as operator-owned acceptance
ca36c69 workbench-audio-config T017-T019: polish (CI visibility, README, verify)
c5c028b workbench-audio-config T015: explicit MIDI input selection (US4)
08460f6 workbench-audio-config T012/T013: persist + restore selections (US3)
d3b2acc workbench-audio-config T009/T010: in-UI source selection (US2)
aa8877d workbench-audio-config T006/T007: in-UI audio device selection (US1)
fc36b9b workbench-audio-config T004/T005: audio-stopped reconfigure lifecycle
7103f38 workbench-audio-config T002/T003: SourceConfig serde + unit test
37cf2f6 workbench-audio-config T001: register new units + stubs in CMake


## Recent audit-log excerpt (prior findings on this feature)

Use this to avoid re-reporting findings that have already been triaged. If a finding was previously dispositioned (`closed`, `won't-fix`, `accepted-trade-off`), don't re-litigate the disposition; only surface a new instance if the underlying shape regressed.



## Under audit

The actual code under review. Read it carefully. The findings you emit must be anchored to specific files + line ranges in this diff (or call out a missing surface that should be in the diff but isn't).

Governance pass over the just-implemented work for feature 'workbench-audio-config', diffed against b561b3e. The differentiated back half audits a plan it did not author or execute.
## Other chunks (file lists only — context for cross-file dependencies this chunk cannot see):
- b803fcb7f17ed923: .github/workflows/ci.yml
- cc36a7e4cc6d3feb: specs/workbench-audio-config/tasks.md, tests/CMakeLists.txt, tests/core/workbench-settings-test.cpp
- d0c555613386cd51: README.md, ROADMAP.md, adapters/workbench/CMakeLists.txt, adapters/workbench/audio-settings.cpp, adapters/workbench/audio-settings.h, adapters/workbench/audio-source.cpp, adapters/workbench/audio-source.h, adapters/workbench/source-bar.cpp, adapters/workbench/source-bar.h

## Chunk 5420a3615ad2e99c
Files in scope: adapters/workbench/workbench-app.cpp, adapters/workbench/workbench-persistence.cpp, adapters/workbench/workbench-persistence.h, adapters/workbench/workbench-settings.cpp, adapters/workbench/workbench-settings.h

## Diffs

### adapters/workbench/workbench-app.cpp
diff --git a/adapters/workbench/workbench-app.cpp b/adapters/workbench/workbench-app.cpp
index 9b3dfa1..2fbdf14 100644
--- a/adapters/workbench/workbench-app.cpp
+++ b/adapters/workbench/workbench-app.cpp
@@ -6,6 +6,7 @@
 #include <juce_audio_utils/juce_audio_utils.h>
 #include <juce_gui_basics/juce_gui_basics.h>
 
+#include "audio-settings.h"
 #include "audio-source.h"
 #include "dsp/audio-block.h"
 #include "dsp/process-context.h"
@@ -13,6 +14,9 @@
 #include "midi-binding.h"
 #include "parameter-view.h"
 #include "processor-node/processor-node.h"
+#include "source-bar.h"
+#include "workbench-persistence.h"
+#include "workbench-settings.h"
 
 // The desktop sketch-and-hear workbench (T022, T026). Holds the effect behind the
 // same host boundary the plugin uses — std::unique_ptr<ProcessorNode> =
@@ -30,7 +34,8 @@ constexpr int kMaxChannels = 8;
 } // namespace
 
 class WorkbenchComponent final : public juce::AudioAppComponent,
-                                 private juce::MidiInputCallback {
+                                 private juce::MidiInputCallback,
+                                 private juce::ChangeListener {
 public:
     WorkbenchComponent()
         : node_(std::make_unique<EffectNode<SvfEffect>>()),
@@ -48,17 +53,90 @@ public:
         abToggle_.onClick = [this] { processed_.store(abToggle_.getToggleState()); };
         addAndMakeVisible(abToggle_);
 
-        setSize(520, 220);
-        // Stereo in/out: input present for live-input mode.
-        setAudioChannels(2, 2);
-        // Enable the available MIDI inputs — registering a callback alone does
-        // not enable any device, so without this the CC bindings stay inert.
-        for (const auto& input : juce::MidiInput::getAvailableDevices())
-            deviceManager.setMidiInputDeviceEnabled(input.identifier, true);
+        // Audio Settings lives in its own window so the main window stays the
+        // sketch-and-hear surface (FR-010). The button opens it on demand.
+        audioSettingsButton_.setButtonText("Audio Settings...");
+        audioSettingsButton_.onClick = [this] { showAudioSettings(); };
+        addAndMakeVisible(audioSettingsButton_);
+
+        // Source bar (FR-003/004): switch between live input and a looped file, no env
+        // var required. Each change updates the message-thread state and then restarts
+        // the audio so prepareToPlay reconfigures with the callback stopped (FR-008).
+        sourceBar_.onSelectLive = [this] {
+            if (mode_ == SourceMode::live)
+                return;
+            mode_ = SourceMode::live;
+            restartAudio();
+            saveSettings();
+        };
+        sourceBar_.onChooseFile = [this](const juce::File& file) {
+            mode_ = SourceMode::file;
+            sourceFile_ = file;
+            restartAudio();
+            saveSettings();
+        };
+        sourceBar_.onChooseCancelled = [this] {
+            // Cancelling must never leave a broken no-source state: only fall back to
+            // live if the current selection is a file with no usable file (FR-009).
+            if (mode_ == SourceMode::file && !sourceFile_.existsAsFile()) {
+                mode_ = SourceMode::live;
+                restartAudio();
+                saveSettings();
+            }
+        };
+        addAndMakeVisible(sourceBar_);
+
+        // Restore persisted selections (FR-006). Saved settings take precedence over
+        // the ACFX_WORKBENCH_FILE first-run convenience (FR-004): the env var only
+        // seeds the source when there is nothing saved yet. This sets the initial
+        // message-thread state; prepareToPlay reconfigures from it thereafter.
+        const LoadedSettings loaded = persistence_.load();
+        if (loaded.source.mode == SourceMode::file && !loaded.source.filePath.empty()) {
+            mode_ = SourceMode::file;
+            sourceFile_ = juce::File(juce::String::fromUTF8(
+                loaded.source.filePath.c_str(),
+                static_cast<int>(loaded.source.filePath.length())));
+        } else if (const char* path = std::getenv("ACFX_WORKBENCH_FILE")) {
+            mode_ = SourceMode::file;
+            sourceFile_ = juce::File(juce::String::fromUTF8(path));
+        }
+
+        // Remember the saved output-device name (if any) so a device that has since
+        // vanished can be surfaced after the manager falls back (FR-009 edge case).
+        juce::String savedOutputDevice;
+        if (loaded.deviceState != nullptr)
+            savedOutputDevice = loaded.deviceState->getStringAttribute("audioOutputDeviceName");
+
+        setSize(520, 300);
+        // Restore devices/rate/buffer (and enabled MIDI inputs) from the saved state; a
+        // null state initialises defaults and a saved-but-missing device falls back to
+        // an available one (selectDefaultDeviceOnFailure). This drives prepareToPlay,
+        // which reads the source state set above.
+        setAudioChannels(2, 2, loaded.deviceState.get());
+
+        // MIDI inputs (US4): the AudioDeviceSelectorComponent's MIDI-inputs section is
+        // the explicit per-device control, and the enabled set is persisted in the
+        // device-manager state. On FIRST run only (no saved state) enable all inputs
+        // once as a sensible default; thereafter the restored state decides which are
+        // enabled. Registering the callback with an empty device id delivers messages
+        // from only the ENABLED inputs, so a disabled controller has no effect (SC-005).
+        // A callback alone enables no device, hence the first-run enable.
+        if (loaded.deviceState == nullptr) {
+            for (const auto& input : juce::MidiInput::getAvailableDevices())
+                deviceManager.setMidiInputDeviceEnabled(input.identifier, true);
+        }
         deviceManager.addMidiInputDeviceCallback({}, this);
+
+        // Persist on every later device-configuration change (added last so the restore
+        // and auto-enable above do not trigger redundant construction-time saves).
+        deviceManager.addChangeListener(this);
+
+        surfaceStartupIssues(loaded.corrupt, savedOutputDevice);
     }
 
     ~WorkbenchComponent() override {
+        deviceManager.removeChangeListener(this);
+        saveSettings(); // persist the final selection on quit (FR-006)
         deviceManager.removeMidiInputDeviceCallback({}, this);
         shutdownAudio();
     }
@@ -72,16 +150,21 @@ public:
         const ProcessContext ctx{sampleRate, blockSize, preparedChannels_};
         node_->prepare(ctx);
 
-        // Source selection (no silent fallback): the built-in file player when
-        // ACFX_WORKBENCH_FILE points at an audio file (the deterministic default
-        // for reproducible A/B), else the live device input, else a surfaced error.
+        // The SINGLE source reconfigure point (FR-008): release any prior selection,
+        // (re)configure from the current message-thread state, then prepare. JUCE
+        // brackets prepareToPlay between audioDeviceStopped/audioDeviceAboutToStart, so
+        // the audio callback is guaranteed STOPPED here — the source buffers are never
+        // reassigned under a live callback. Every device/source change routes through a
+        // device restart (restartAudio / the device selector), which re-enters here.
+        source_.release();
         const int inputs = numInputChannels();
         try {
-            if (const char* path = std::getenv("ACFX_WORKBENCH_FILE")) {
-                source_.useFilePlayer(juce::File(juce::String::fromUTF8(path)));
-            } else if (inputs > 0) {
+            // No silent fallback (Constitution V): configure exactly the selected
+            // source; a failure (no input, unreadable file) is surfaced below.
+            if (mode_ == SourceMode::file)
+                source_.useFilePlayer(sourceFile_);
+            else
                 source_.useLiveInput(inputs);
-            }
             source_.prepare(sampleRate, blockSize);
             sourceReady_ = true;
         } catch (const AudioSourceError& e) {
@@ -136,6 +219,8 @@ public:
 
     void resized() override {
         auto area = getLocalBounds();
+        audioSettingsButton_.setBounds(area.removeFromTop(32).reduced(8, 4));
+        sourceBar_.setBounds(area.removeFromTop(36).reduced(4, 2));
         abToggle_.setBounds(area.removeFromBottom(32).reduced(8, 4));
         paramView_.setBounds(area);
     }
@@ -153,6 +238,63 @@ private:
         return 0;
     }
 
+    // Apply a source change with the audio callback STOPPED. restartLastAudioDevice()
+    // drives audioDeviceStopped -> releaseResources -> audioDeviceAboutToStart ->
+    // prepareToPlay, and prepareToPlay reconfigures the source from the updated
+    // message-thread state. The swap therefore happens entirely inside that stopped
+    // window (FR-008) — no mid-callback source change. Message-thread only.
+    void restartAudio() { deviceManager.restartLastAudioDevice(); }
+
+    // Open (creating on first use) the Audio Settings window. The selector's own edits
+    // drive the device restart cycle, so a device change reconfigures the source via
+    // prepareToPlay with the callback stopped — no extra wiring needed here.
+    void showAudioSettings() {
+        if (audioSettings_ == nullptr)
+            audioSettings_ = std::make_unique<AudioSettingsWindow>(deviceManager);
+        audioSettings_->setVisible(true);
+        audioSettings_->toFront(true);
+    }
+
+    // The current source selection as the persistable SourceConfig (the JUCE ->
+    // std::string boundary; serde itself is JUCE-free).
+    SourceConfig currentSourceConfig() const {
+        if (mode_ == SourceMode::file)
+            return SourceConfig{SourceMode::file, sourceFile_.getFullPathName().toStdString()};
+        return SourceConfig{SourceMode::live, ""};
+    }
+
+    void saveSettings() { persistence_.save(deviceManager, currentSourceConfig()); }
+
+    // Device-manager changes (device/rate/buffer/MIDI edits via the selector) persist
+    // the new configuration (FR-006).
+    void changeListenerCallback(juce::ChangeBroadcaster*) override { saveSettings(); }
+
+    // Surface (never swallow) startup problems: unreadable saved settings, or a saved
+    // device that is gone and was fallen back from (FR-009). Defaults are already in
+    // effect by the time this runs; this only informs the person.
+    void surfaceStartupIssues(bool corrupt, const juce::String& savedOutputDevice) {
+        juce::StringArray messages;
+        if (corrupt)
+            messages.add("Your saved workbench settings were unreadable; starting with "
+                         "defaults.");
+
+        juce::String activeDevice;
+        if (auto* device = deviceManager.getCurrentAudioDevice())
+            activeDevice = device->getName();
+        if (savedOutputDevice.isNotEmpty() && activeDevice.isNotEmpty()
+            && savedOutputDevice != activeDevice)
+            messages.add("Saved audio device \"" + savedOutputDevice + "\" was "
+                         "unavailable; using \"" + activeDevice + "\" instead.");
+
+        if (messages.isEmpty())
+            return;
+        const juce::String text = messages.joinIntoString("\n");
+        juce::MessageManager::callAsync([text] {
+            juce::NativeMessageBox::showMessageBoxAsync(
+                juce::MessageBoxIconType::WarningIcon, "Workbench settings", text);
+        });
+    }
+
     void handleIncomingMidiMessage(juce::MidiInput*, const juce::MidiMessage& msg) override {
         midi_.handle(msg, [this](ParamId id, float norm) {
             node_->setParameter(id, norm); // core is thread-safe (atomic pending)
@@ -172,6 +314,16 @@ private:
     MidiBinding midi_;
     WorkbenchAudioSource source_;
     juce::ToggleButton abToggle_;
+    juce::TextButton audioSettingsButton_;
+    SourceBar sourceBar_;
+    std::unique_ptr<AudioSettingsWindow> audioSettings_;
+    WorkbenchPersistence persistence_;
+
+    // Current source selection, owned on the message thread and read by prepareToPlay
+    // (the single reconfigure point). The source bar (T010) mutates these and then
+    // calls restartAudio() to apply the change with the callback stopped.
+    SourceMode mode_ = SourceMode::live;
+    juce::File sourceFile_;
 
     int preparedChannels_ = 2;
     bool sourceReady_ = false;


### adapters/workbench/workbench-persistence.cpp
diff --git a/adapters/workbench/workbench-persistence.cpp b/adapters/workbench/workbench-persistence.cpp
new file mode 100644
index 0000000..a3c2efd
--- /dev/null
+++ b/adapters/workbench/workbench-persistence.cpp
@@ -0,0 +1,63 @@
+#include "workbench-persistence.h"
+
+#include <string>
+
+// T012 (US3) — persistence over juce::ApplicationProperties. The SourceConfig serde it
+// calls (serialize/parse) lives in the JUCE-free workbench-settings TU; this TU is the
+// JUCE boundary that converts std::string <-> juce::String and owns the settings file.
+
+namespace acfx::workbench {
+
+namespace {
+constexpr const char* kDeviceStateKey = "audioDeviceState";
+constexpr const char* kSourceConfigKey = "sourceConfig";
+} // namespace
+
+WorkbenchPersistence::WorkbenchPersistence() {
+    juce::PropertiesFile::Options options;
+    options.applicationName = "acfx Workbench";
+    options.filenameSuffix = "settings";
+    options.folderName = "acfx";
+    options.osxLibrarySubFolder = "Application Support";
+    options.storageFormat = juce::PropertiesFile::storeAsXML;
+    applicationProperties_.setStorageParameters(options);
+}
+
+LoadedSettings WorkbenchPersistence::load() {
+    LoadedSettings out;
+    auto* props = applicationProperties_.getUserSettings();
+
+    // A settings file that exists and is non-empty but does not parse as XML is
+    // corrupt — surface it rather than silently starting fresh (FR-009).
+    const juce::File file = props->getFile();
+    if (file.existsAsFile() && file.getSize() > 0 && juce::parseXML(file) == nullptr)
+        out.corrupt = true;
+
+    if (props->containsKey(kDeviceStateKey)) {
+        out.deviceState = props->getXmlValue(kDeviceStateKey);
+        // A present-but-unparseable device-state value is also corruption.
+        if (out.deviceState == nullptr && props->getValue(kDeviceStateKey).isNotEmpty())
+            out.corrupt = true;
+    }
+
+    out.source = parse(props->getValue(kSourceConfigKey).toStdString());
+    return out;
+}
+
+void WorkbenchPersistence::save(const juce::AudioDeviceManager& deviceManager,
+                                const SourceConfig& source) {
+    auto* props = applicationProperties_.getUserSettings();
+
+    if (auto xml = deviceManager.createStateXml())
+        props->setValue(kDeviceStateKey, xml.get());
+    else
+        props->removeValue(kDeviceStateKey);
+
+    const std::string serialized = serialize(source);
+    props->setValue(kSourceConfigKey,
+                    juce::String::fromUTF8(serialized.c_str(),
+                                           static_cast<int>(serialized.length())));
+    props->saveIfNeeded();
+}
+
+} // namespace acfx::workbench


### adapters/workbench/workbench-persistence.h
diff --git a/adapters/workbench/workbench-persistence.h b/adapters/workbench/workbench-persistence.h
new file mode 100644
index 0000000..8e79ab0
--- /dev/null
+++ b/adapters/workbench/workbench-persistence.h
@@ -0,0 +1,50 @@
+#pragma once
+
+#include <memory>
+
+#include <juce_audio_utils/juce_audio_utils.h>
+
+#include "workbench-settings.h"
+
+// Save/restore the workbench's audio configuration (research.md decision 3). This is
+// the JUCE side of persistence — deliberately a SEPARATE translation unit from the
+// JUCE-free workbench-settings serde so the tested seam stays JUCE-free. It writes the
+// AudioDeviceManager state XML (devices, rate, buffer, enabled MIDI inputs) plus the
+// serialized SourceConfig into a juce::ApplicationProperties user settings file (app
+// name "acfx Workbench"), and reads them back on launch. A corrupt/missing file is not
+// fatal: load() reports it via LoadedSettings::corrupt so the caller restores defaults
+// and surfaces the problem (FR-009), never crashing startup.
+
+namespace acfx::workbench {
+
+struct LoadedSettings {
+    // The saved AudioDeviceManager state, or null when absent/unreadable. Pass to
+    // AudioAppComponent::setAudioChannels(in, out, deviceState) to restore devices.
+    std::unique_ptr<juce::XmlElement> deviceState;
+    // The saved source selection, or the safe default (live) when absent/garbage.
+    SourceConfig source;
+    // True when a settings file existed but could not be read as valid settings — the
+    // caller starts with defaults AND surfaces the problem (does not fail silently).
+    bool corrupt = false;
+};
+
+class WorkbenchPersistence {
+public:
+    WorkbenchPersistence();
+
+    // Load the saved settings (never throws; a missing file yields defaults with
+    // corrupt == false, a present-but-unreadable file yields defaults with corrupt ==
+    // true).
+    LoadedSettings load();
+
+    // Persist the current device-manager state + source selection. Safe to call on
+    // every change and on quit.
+    void save(const juce::AudioDeviceManager& deviceManager, const SourceConfig& source);
+
+private:
+    juce::ApplicationProperties applicationProperties_;
+
+    JUCE_DECLARE_NON_COPYABLE(WorkbenchPersistence)
+};
+
+} // namespace acfx::workbench


### adapters/workbench/workbench-settings.cpp
diff --git a/adapters/workbench/workbench-settings.cpp b/adapters/workbench/workbench-settings.cpp
new file mode 100644
index 0000000..4a47fcf
--- /dev/null
+++ b/adapters/workbench/workbench-settings.cpp
@@ -0,0 +1,43 @@
+#include "workbench-settings.h"
+
+#include <cstddef>
+
+// SourceConfig serialize/parse (contracts/source-config.md). Pure std::string value
+// transforms — no JUCE, no device, no audio thread. The format is a single mode token,
+// and for file mode a newline followed by the path VERBATIM. Taking everything after
+// the first newline as the path means the path may itself contain any character
+// (spaces, unicode, even embedded newlines on filesystems that allow them) and still
+// round-trips exactly.
+
+namespace acfx::workbench {
+
+namespace {
+constexpr const char* kLiveToken = "live";
+constexpr const char* kFileToken = "file";
+} // namespace
+
+std::string serialize(const SourceConfig& cfg) {
+    // A file source needs a path to be valid; a file config with an empty path is not
+    // a usable file source, so it serializes as live (symmetric with parse, which
+    // refuses to reconstruct an empty-path file mode).
+    if (cfg.mode == SourceMode::file && !cfg.filePath.empty())
+        return std::string(kFileToken) + '\n' + cfg.filePath;
+    return kLiveToken;
+}
+
+SourceConfig parse(const std::string& text) {
+    const std::size_t newline = text.find('\n');
+    const std::string modeToken =
+        newline == std::string::npos ? text : text.substr(0, newline);
+    const std::string pathPart =
+        newline == std::string::npos ? std::string{} : text.substr(newline + 1);
+
+    // Only reconstruct a file source when the token is exactly "file" AND a non-empty
+    // path is present. Everything else — empty, garbage, unknown mode, or a file token
+    // with no path — is the safe default (live, empty path); never throws.
+    if (modeToken == kFileToken && !pathPart.empty())
+        return SourceConfig{SourceMode::file, pathPart};
+    return SourceConfig{SourceMode::live, ""};
+}
+
+} // namespace acfx::workbench


### adapters/workbench/workbench-settings.h
diff --git a/adapters/workbench/workbench-settings.h b/adapters/workbench/workbench-settings.h
new file mode 100644
index 0000000..1bd1e85
--- /dev/null
+++ b/adapters/workbench/workbench-settings.h
@@ -0,0 +1,33 @@
+#pragma once
+
+#include <string>
+
+// The workbench's persistable source selection — the one pure, JUCE-free,
+// device-free seam of the audio-config feature (contracts/source-config.md).
+// SourceConfig uses std::string (not juce::String) on purpose: this header and its
+// serialize/parse pair compile into the JUCE-free host test target (acfx_core_tests),
+// so the seam is unit-tested with no JUCE/app/device context. The workbench converts
+// std::string <-> juce::String at the UI/file boundary.
+
+namespace acfx::workbench {
+
+enum class SourceMode { live, file };
+
+struct SourceConfig {
+    SourceMode mode = SourceMode::live;
+    std::string filePath; // empty unless mode == file
+};
+
+inline bool operator==(const SourceConfig& a, const SourceConfig& b) {
+    return a.mode == b.mode && a.filePath == b.filePath;
+}
+inline bool operator!=(const SourceConfig& a, const SourceConfig& b) { return !(a == b); }
+
+// Pure value transforms (message thread; never the audio callback). serialize emits a
+// stable settings string; parse never throws and returns the safe default
+// SourceConfig{ live, "" } on empty/garbage/unknown-mode input, and only returns file
+// mode when a non-empty path is present (contracts/source-config.md).
+std::string serialize(const SourceConfig& cfg);
+SourceConfig parse(const std::string& text);
+
+} // namespace acfx::workbench


## What to look for

- **Correctness bugs** — logic errors, off-by-one, null/undefined paths, race conditions, missing error handling, swallowed exceptions.
- **Design issues** — coupling between layers that should be independent, leaking abstractions, primitives that should compose but don't, configuration that should be data ending up as code.
- **Missed edge cases** — what happens with empty input? Maximum input? Concurrent calls? Partial failure? Network unavailability? Operator interrupt mid-operation? What is the behavior on a fresh install vs. an upgrade?
- **Code-quality concerns** — files growing past a reasonable cap, names that don't reveal intent, dead code, duplicated logic, magic numbers without explanation, tests that don't test the contract they claim to test.
- **Cross-cutting impact** — does this diff touch a surface that other surfaces depend on? Are those other surfaces updated? Are migrations needed? Are doctor rules / schemas / validators updated to match the new shape?
- **Documentation drift** — does the README / SKILL.md / PRD describe the behavior the code actually implements? If the spec changed, did the implementation? If the implementation changed, did the spec?
- **Operator-discipline traps** — placeholder comments, swallowed errors, hardcoded paths/values that should be configurable, fallbacks that hide failure modes, mock data outside test code. These are bug-factories per project guidelines.

## Process drivers (029 US8 / FR-029)

These codify the structural drivers of myopic convergence (TASK-60), so the loop converges in fewer rounds with less fix-induced surface growth. The first three (channel-enumeration, invariant-first boundary, round-0 self-red-team) are **fix-review** drivers — apply them when the work under audit is a fix for a prior finding. The last two (fleet-degradation pricing, severity-rubric anchoring) are **general** controls that apply to every round:

- **Channel-enumeration.** When a fix ADDS to an allowlist/surface (a new flag, a new accepted value, a new parser branch, a new fold path), do not accept it on the one example it fixes — enumerate the channels it opens: the **value** channel (other inputs now accepted), the **state** channel (new reachable states), the **multiline / composition** channel (how it composes with adjacent surfaces). Flag any opened channel that lacks a fixture.
- **Invariant-first boundary.** When a finding is dispositioned as a scope boundary, state the boundary as the **mechanism's invariant plus an in-scope exception**, NOT as the exclusion of the one counterexample. "We exclude X" is a smell; "the invariant is I, and X is the in-scope exception because…" is the disposition.
- **Round-0 self-red-team.** When the work under audit is itself a FIX for a prior finding, audit the **fix diff as a fresh surface in its own right** — do not assume it is correct merely because it targets a known bug. Ask what new edge the fix opened and what it moved rather than removed; a fix that resolves one finding while opening an unaudited channel is itself a finding.
- **Fleet-degradation pricing.** A convergence claim is only as strong as the fleet that produced it. When the fleet is **degraded** (a timed-out / killed / zero-byte lane — US2 observability), price the round's "0 HIGH" accordingly: it is computed over fewer models, so cross-model agreement is weaker. Do not treat a degraded-fleet quiet round as full convergence.
- **Severity-rubric anchoring.** Rate every finding by the blast-radius rubric below (US3), not by how alarming it feels — a quietly-plausible wrong reading an unattended agent would build outranks an obvious contradiction a reader would resolve.

## Output format

For each finding you surface, emit ONE markdown block in this exact shape:

```
### <heading: one-line summary of the finding>

Finding-ID: AUDIT-BARRAGE-<your-model-name>-<NN>
Status:     open
Severity:   <blocking | high | medium | low | informational>
Surface:    <repo-relative-path:line-range> OR <description of the surface if not anchored to a single file>

<one-to-three paragraphs of body: what the finding is, why it matters, what evidence you relied on, what a reasonable fix would look like. Be specific. Cite line numbers from the diff. If the finding is structural / cross-file, name every file affected.>
```

Number the findings sequentially (`-01`, `-02`, ...).

**Severity — rate each finding by downstream blast-radius:** the consequence if a downstream consumer acts on the audited surface *as written*. The consumer may be an adopter running the code, or — especially for a spec — an AI agent building **unattended** from it, with no human to catch a wrong reading. Rate by what would actually happen if this shipped as-is, **not by how alarming the finding feels**. State the blast-radius reasoning in the finding body for every finding, at every level.

- `blocking` — acting on it as-written breaks the feature's stated goals in obvious ways; OR (for a spec) the more natural reading an agent reaches first is the wrong one, so it will likely be built wrong by default and nothing in the artifact corrects it.
- `high` — a correctness/safety defect a consumer will hit; OR a spec contradiction/ambiguity where the readings are roughly equally plausible and the artifact doesn't disambiguate — an agent might build either, including the wrong one.
- `medium` — a design issue that compounds over time; OR a spec inconsistency a reasonable consumer would resolve correctly anyway (readings barely diverge, or context makes the intended one obvious).
- `low` — hygiene; cosmetic wording with no behavioral or implementation consequence.
- `informational` — context worth seeing, not itself a defect.

**Calibrate by consequence, not by alarm.** A genuine contradiction a reader would obviously resolve the right way is at most `medium`. A quietly-plausible wrong reading an agent would actually build is `high`/`blocking` even if it looks minor. A spec's internal consistency is load-bearing — it is the input to an unattended build.

## If you find nothing — say so explicitly

If you walk the diff carefully and find no findings worth surfacing, emit ONE block in this shape instead:

```
### No findings

Finding-ID: AUDIT-BARRAGE-<your-model-name>-CLEAN
Status:     open
Severity:   informational
Surface:    (the entire diff)

I walked the diff for the feature named above and found no findings worth surfacing. My specific reasoning: <three-to-five sentences explaining what you checked, why those checks came back clean, and what you would have flagged if it had been present.>
```

**Do not pad with weak findings.** A confident "I checked X, Y, Z and they are clean for these reasons" is more useful to the operator than three vague low-severity notes. The cross-model diversity gives the operator independent signal; an empty clean report from your CLI is itself a signal when paired with findings from your siblings.

## Hard constraints

- **No deferral phrases.** Don't write phrases like "fix later", "address in a follow-up", or other commitments to deferred work. The dispatch-wrapper rejects these as bug-factories. If you spot a deferral phrase IN the diff, surface it as a finding.
- **Anchor findings to evidence.** A finding that says "this might be a problem" without naming the specific file + line is not actionable. Name the surface, quote the relevant code, explain what's wrong.
- **One issue per finding block.** Don't bundle multiple concerns into one entry; the operator triages each block as a discrete signal.
- **Provenance is your model name.** Replace `<your-model-name>` in the Finding-ID with the CLI you are (`claude`, `codex`, `gemini`, etc.). This is how the operator joins findings across models.
