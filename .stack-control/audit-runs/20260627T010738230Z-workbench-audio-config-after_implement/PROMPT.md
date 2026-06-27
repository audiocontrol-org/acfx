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
- 5420a3615ad2e99c: adapters/workbench/workbench-app.cpp, adapters/workbench/workbench-persistence.cpp, adapters/workbench/workbench-persistence.h, adapters/workbench/workbench-settings.cpp, adapters/workbench/workbench-settings.h
- b803fcb7f17ed923: .github/workflows/ci.yml
- cc36a7e4cc6d3feb: specs/workbench-audio-config/tasks.md, tests/CMakeLists.txt, tests/core/workbench-settings-test.cpp

## Chunk d0c555613386cd51
Files in scope: README.md, ROADMAP.md, adapters/workbench/CMakeLists.txt, adapters/workbench/audio-settings.cpp, adapters/workbench/audio-settings.h, adapters/workbench/audio-source.cpp, adapters/workbench/audio-source.h, adapters/workbench/source-bar.cpp, adapters/workbench/source-bar.h

## Diffs

### README.md
diff --git a/README.md b/README.md
index 021dcd3..ab48f5f 100644
--- a/README.md
+++ b/README.md
@@ -53,7 +53,19 @@ cmake --build --preset desktop --target acfx_workbench
 ```
 
 Launch the built app: auto-generated controls for cutoff / resonance / mode, a
-built-in player or live input, a bound MIDI CC, and a dry/processed A/B toggle.
+dry/processed A/B toggle, and in-UI audio configuration:
+
+- **Audio Settings…** opens a window (JUCE's device selector) to choose the audio
+  **input/output device**, sample rate, buffer size, and which **MIDI inputs** drive
+  the parameter CCs.
+- The **source bar** switches between **Live** input and a **file** you pick with
+  **Load file…** (looped through the filter) — no environment variable required.
+- All of these selections — device, rate/buffer, source (including the chosen file),
+  and MIDI inputs — are **remembered across launches**. Device/source changes apply
+  with the audio engine stopped, so switching never glitches or stalls the stream.
+- Failures (a device that won't open, an unreadable/missing file, unreadable saved
+  settings) are surfaced and leave the workbench in a safe, usable state — never
+  silent silence or placeholder audio.
 
 ### Desktop plugin (VST3 / AU / CLAP) — Scenario C
 
@@ -63,8 +75,10 @@ cmake --preset desktop
 cmake --build --preset desktop --target acfx_plugin_VST3 acfx_plugin_AU acfx_plugin_CLAP
 ```
 
-To play the built-in player for reproducible A/B, point the workbench at a file
-with `ACFX_WORKBENCH_FILE=/path/to/audio.wav`; otherwise it uses the live input.
+The workbench's built-in player is reached from the UI (the **Load file…** button);
+`ACFX_WORKBENCH_FILE=/path/to/audio.wav` remains only as a **first-run convenience**
+that seeds the source when nothing has been saved yet — a saved selection always takes
+precedence.
 
 ### Hardware cross-compile — Scenario D
 


### ROADMAP.md
diff --git a/ROADMAP.md b/ROADMAP.md
index f6e98ba..36db2c2 100644
--- a/ROADMAP.md
+++ b/ROADMAP.md
@@ -27,4 +27,5 @@ Milestone 1: prove the acfx spine end-to-end with a State-Variable Filter — co
 
 ## design:feature/workbench-audio-config
 - status: planned
+- analyze-clean: yes
 - spec: specs/workbench-audio-config
\ No newline at end of file


### adapters/workbench/CMakeLists.txt
diff --git a/adapters/workbench/CMakeLists.txt b/adapters/workbench/CMakeLists.txt
index 58dfe55..f0b4d49 100644
--- a/adapters/workbench/CMakeLists.txt
+++ b/adapters/workbench/CMakeLists.txt
@@ -11,6 +11,10 @@ target_sources(acfx_workbench PRIVATE
   workbench-app.cpp
   parameter-view.cpp
   audio-source.cpp
+  audio-settings.cpp
+  source-bar.cpp
+  workbench-settings.cpp
+  workbench-persistence.cpp
 )
 
 target_compile_features(acfx_workbench PRIVATE cxx_std_20)


### adapters/workbench/audio-settings.cpp
diff --git a/adapters/workbench/audio-settings.cpp b/adapters/workbench/audio-settings.cpp
new file mode 100644
index 0000000..80eae1c
--- /dev/null
+++ b/adapters/workbench/audio-settings.cpp
@@ -0,0 +1,28 @@
+#include "audio-settings.h"
+
+// T006 (US1) — host the standard JUCE device selector in its own window.
+
+namespace acfx::workbench {
+
+AudioSettingsWindow::AudioSettingsWindow(juce::AudioDeviceManager& deviceManager)
+    : juce::DocumentWindow("Audio Settings", juce::Colours::darkgrey,
+                           juce::DocumentWindow::closeButton) {
+    setUsingNativeTitleBar(true);
+
+    // (deviceManager, minIn=0, maxIn=2, minOut=0, maxOut=2, showMidiInputs=true,
+    // showMidiOutputs=false, showChannelsAsStereoPairs=true, hideAdvanced=false) — one
+    // component covers device + rate/buffer + MIDI-input selection (FR-001/002/005),
+    // and its own UI surfaces device-open failures (FR-009), keeping the previous
+    // working device when a new one fails to open.
+    auto* selector = new juce::AudioDeviceSelectorComponent(
+        deviceManager, 0, 2, 0, 2, true, false, true, false);
+    selector->setSize(500, 480);
+    setContentOwned(selector, true); // window takes ownership and sizes to the content
+
+    setResizable(true, false);
+    centreWithSize(getWidth(), getHeight());
+}
+
+void AudioSettingsWindow::closeButtonPressed() { setVisible(false); }
+
+} // namespace acfx::workbench


### adapters/workbench/audio-settings.h
diff --git a/adapters/workbench/audio-settings.h b/adapters/workbench/audio-settings.h
new file mode 100644
index 0000000..9fc165b
--- /dev/null
+++ b/adapters/workbench/audio-settings.h
@@ -0,0 +1,28 @@
+#pragma once
+
+#include <juce_audio_utils/juce_audio_utils.h>
+#include <juce_gui_basics/juce_gui_basics.h>
+
+// A separate Audio Settings window hosting JUCE's standard
+// AudioDeviceSelectorComponent — input/output device, sample rate, buffer size, and
+// MIDI input selection in one tested component (research.md decision 2). It drives the
+// shared AudioDeviceManager directly; its edits fire the same audio stop/start cycle
+// the source lifecycle reconfigures through, so device changes are RT-safe by
+// construction (FR-008). Closing the window hides it (the manager state persists), so
+// it lives off the main window without cluttering the sketch-and-hear controls (FR-010).
+
+namespace acfx::workbench {
+
+class AudioSettingsWindow final : public juce::DocumentWindow {
+public:
+    explicit AudioSettingsWindow(juce::AudioDeviceManager& deviceManager);
+
+    // Hide rather than destroy on close — the workbench owns this window's lifetime
+    // and reopens it instantly with the device-manager state intact.
+    void closeButtonPressed() override;
+
+private:
+    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AudioSettingsWindow)
+};
+
+} // namespace acfx::workbench


### adapters/workbench/audio-source.cpp
diff --git a/adapters/workbench/audio-source.cpp b/adapters/workbench/audio-source.cpp
index f5d6aea..3ee7ad6 100644
--- a/adapters/workbench/audio-source.cpp
+++ b/adapters/workbench/audio-source.cpp
@@ -9,8 +9,8 @@ WorkbenchAudioSource::WorkbenchAudioSource() { formatManager_.registerBasicForma
 
 void WorkbenchAudioSource::useFilePlayer(const juce::File& file) {
     if (configured_.load(std::memory_order_acquire))
-        throw AudioSourceError("Audio source must be selected before the stream "
-                               "starts; stop audio to switch sources.");
+        throw AudioSourceError("Cannot reselect the audio source while the stream is "
+                               "running; release (stop audio) before reconfiguring.");
     if (!file.existsAsFile())
         throw AudioSourceError("Audio file does not exist: " + file.getFullPathName());
 
@@ -44,8 +44,8 @@ void WorkbenchAudioSource::useFilePlayer(const juce::File& file) {
 
 void WorkbenchAudioSource::useLiveInput(int availableInputChannels) {
     if (configured_.load(std::memory_order_acquire))
-        throw AudioSourceError("Audio source must be selected before the stream "
-                               "starts; stop audio to switch sources.");
+        throw AudioSourceError("Cannot reselect the audio source while the stream is "
+                               "running; release (stop audio) before reconfiguring.");
     if (availableInputChannels <= 0)
         throw AudioSourceError("Live input selected but the audio device offers no "
                                "input channels.");


### adapters/workbench/audio-source.h
diff --git a/adapters/workbench/audio-source.h b/adapters/workbench/audio-source.h
index aaa78ea..5d3e20a 100644
--- a/adapters/workbench/audio-source.h
+++ b/adapters/workbench/audio-source.h
@@ -15,10 +15,13 @@
 // RT-safety (Constitution VI): the file is decoded into an in-memory buffer
 // before the stream starts (off the audio thread); fillBlock() then reads that
 // buffer at an atomic play position with no locks and no allocation. Source
-// selection (useFilePlayer / useLiveInput) is a setup-time operation: it must
-// happen before prepare(), and switching sources requires stopping the stream
-// first. That precondition is ENFORCED — a selection call while already
-// configured throws — so the audio thread never reads a buffer being reassigned.
+// selection (useFilePlayer / useLiveInput) is a RECONFIGURE operation, legitimately
+// repeated whenever the workbench switches device or source — but ONLY while the
+// audio callback is stopped. The lifecycle invariant is "release before reconfigure":
+// the workbench calls release() (the audio-stopped window JUCE's restart cycle
+// provides) before reselecting, then prepare(). That invariant is ENFORCED — a
+// selection call while still configured (stream running) throws — so the audio thread
+// never reads fileBuffer_/live_/hasFile_ while they are being reassigned.
 
 namespace acfx::workbench {
 
@@ -33,12 +36,15 @@ public:
     WorkbenchAudioSource();
 
     // Decode the given file into memory and select it as the source. Throws
-    // AudioSourceError if the file cannot be opened/decoded. Call at setup (off
-    // the audio thread).
+    // AudioSourceError if the file cannot be opened/decoded, or if the source is
+    // still configured (the stream must be released/stopped before reselecting — the
+    // release-before-reconfigure invariant). Call on the message thread, off the
+    // audio thread.
     void useFilePlayer(const juce::File& file);
 
     // Use the live device input. `availableInputChannels` is what the device
-    // offers; throws AudioSourceError if there are none.
+    // offers; throws AudioSourceError if there are none, or if the source is still
+    // configured (release before reselecting).
     void useLiveInput(int availableInputChannels);
 
     void prepare(double sampleRate, int blockSize);


### adapters/workbench/source-bar.cpp
diff --git a/adapters/workbench/source-bar.cpp b/adapters/workbench/source-bar.cpp
new file mode 100644
index 0000000..4c897c0
--- /dev/null
+++ b/adapters/workbench/source-bar.cpp
@@ -0,0 +1,45 @@
+#include "source-bar.h"
+
+// T009 (US2) — Live/File source selection UI. Callbacks only; the workbench applies
+// the actual source change through the audio-stopped restart.
+
+namespace acfx::workbench {
+
+SourceBar::SourceBar() {
+    liveButton_.onClick = [this] {
+        if (onSelectLive)
+            onSelectLive();
+    };
+    fileButton_.onClick = [this] { openChooser(); };
+    addAndMakeVisible(liveButton_);
+    addAndMakeVisible(fileButton_);
+}
+
+void SourceBar::openChooser() {
+    // Async chooser: keeps the message thread responsive and never touches the audio
+    // thread. The FileChooser must outlive the launch, so it is owned by the bar.
+    chooser_ = std::make_unique<juce::FileChooser>(
+        "Choose an audio file to loop", juce::File{}, "*.wav;*.aiff;*.aif;*.flac;*.mp3");
+    const auto flags =
+        juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles;
+    chooser_->launchAsync(flags, [this](const juce::FileChooser& fc) {
+        const juce::File result = fc.getResult();
+        if (result == juce::File{}) {
+            // Cancelled with no selection — let the workbench keep a valid source
+            // (revert to Live if none) rather than enter a broken no-source state.
+            if (onChooseCancelled)
+                onChooseCancelled();
+            return;
+        }
+        if (onChooseFile)
+            onChooseFile(result);
+    });
+}
+
+void SourceBar::resized() {
+    auto area = getLocalBounds().reduced(4);
+    liveButton_.setBounds(area.removeFromLeft(area.getWidth() / 2).reduced(4, 0));
+    fileButton_.setBounds(area.reduced(4, 0));
+}
+
+} // namespace acfx::workbench


### adapters/workbench/source-bar.h
diff --git a/adapters/workbench/source-bar.h b/adapters/workbench/source-bar.h
new file mode 100644
index 0000000..3bb8fd6
--- /dev/null
+++ b/adapters/workbench/source-bar.h
@@ -0,0 +1,40 @@
+#pragma once
+
+#include <functional>
+#include <memory>
+
+#include <juce_gui_basics/juce_gui_basics.h>
+
+// The main-window source bar: a "Live" button plus a "Load file..." button
+// (research.md decision 4). It owns NO audio logic — it emits callbacks only, so the
+// workbench decides how to apply a source change (always via the audio-stopped
+// restart). The file chooser is async (juce::FileChooser::launchAsync) so it never
+// blocks the message thread; on a valid pick it reports the file, and on cancel it
+// reports the cancellation so the workbench can keep a valid source (FR-003/004/009).
+
+namespace acfx::workbench {
+
+class SourceBar final : public juce::Component {
+public:
+    SourceBar();
+
+    // The user chose live input.
+    std::function<void()> onSelectLive;
+    // The user picked a valid file in the chooser.
+    std::function<void(const juce::File&)> onChooseFile;
+    // The user opened the chooser but cancelled without choosing a file.
+    std::function<void()> onChooseCancelled;
+
+    void resized() override;
+
+private:
+    void openChooser();
+
+    juce::TextButton liveButton_{"Live"};
+    juce::TextButton fileButton_{"Load file..."};
+    std::unique_ptr<juce::FileChooser> chooser_;
+
+    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SourceBar)
+};
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
