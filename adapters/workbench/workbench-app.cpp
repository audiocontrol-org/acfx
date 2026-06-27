#include <array>
#include <atomic>
#include <cstdlib>
#include <memory>

#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include "audio-settings.h"
#include "audio-source.h"
#include "dsp/audio-block.h"
#include "dsp/process-context.h"
#include "effects/svf/svf-effect.h"
#include "midi-binding.h"
#include "parameter-view.h"
#include "processor-node/processor-node.h"
#include "source-bar.h"
#include "workbench-persistence.h"
#include "workbench-settings.h"

// The desktop sketch-and-hear workbench (T022, T026). Holds the effect behind the
// same host boundary the plugin uses — std::unique_ptr<ProcessorNode> =
// EffectNode<SvfEffect> — routes audio from the selected source through it, draws
// the auto-generated controls, binds MIDI CCs, and offers a dry/processed A/B
// toggle. Parameter edits from the GUI/MIDI threads go through
// ProcessorNode::setParameter, the RT-safe cross-thread ingress: the effect
// publishes each value as a lock-free atomic that process() consumes, so no
// separate workbench-side queue is needed.

namespace acfx::workbench {

namespace {
constexpr int kMaxChannels = 8;
} // namespace

class WorkbenchComponent final : public juce::AudioAppComponent,
                                 private juce::MidiInputCallback,
                                 private juce::ChangeListener {
public:
    WorkbenchComponent()
        : node_(std::make_unique<EffectNode<SvfEffect>>()),
          paramView_(node_->parameters(),
                     [this](ParamId id, float norm) { node_->setParameter(id, norm); }) {
        params_ = node_->parameters();

        // Default MIDI map: CC 74 -> cutoff (the conventional filter-cutoff CC).
        midi_.bind(74, ParamId{SvfEffect::kCutoff});
        midi_.bind(71, ParamId{SvfEffect::kResonance});

        addAndMakeVisible(paramView_);
        abToggle_.setButtonText("Process (A/B)");
        abToggle_.setToggleState(true, juce::dontSendNotification);
        abToggle_.onClick = [this] { processed_.store(abToggle_.getToggleState()); };
        addAndMakeVisible(abToggle_);

        // Audio Settings lives in its own window so the main window stays the
        // sketch-and-hear surface (FR-010). The button opens it on demand.
        audioSettingsButton_.setButtonText("Audio Settings...");
        audioSettingsButton_.onClick = [this] { showAudioSettings(); };
        addAndMakeVisible(audioSettingsButton_);

        // Source bar (FR-003/004): switch between live input and a looped file, no env
        // var required. Each change updates the message-thread state and then restarts
        // the audio so prepareToPlay reconfigures with the callback stopped (FR-008).
        sourceBar_.onSelectLive = [this] {
            if (mode_ == SourceMode::live)
                return;
            mode_ = SourceMode::live;
            restartAudio();
            saveSettings();
        };
        sourceBar_.onChooseFile = [this](const juce::File& file) {
            mode_ = SourceMode::file;
            sourceFile_ = file;
            restartAudio();
            saveSettings();
        };
        // Keep the picker's filter in lock-step with the formats the decoder actually
        // registers, so it never offers a file the source cannot read (AUDIT-20260627-06).
        sourceBar_.setFileFilter(source_.supportedFileWildcard());
        sourceBar_.onChooseCancelled = [this] {
            // Cancelling must never leave a broken no-source state: only fall back to
            // live if the current selection is a file with no usable file (FR-009).
            if (mode_ == SourceMode::file && !sourceFile_.existsAsFile()) {
                mode_ = SourceMode::live;
                restartAudio();
                saveSettings();
            }
        };
        addAndMakeVisible(sourceBar_);

        // Restore persisted selections (FR-006). Saved settings take precedence over
        // the ACFX_WORKBENCH_FILE first-run convenience (FR-004): the env var only
        // seeds the source when there is nothing saved yet. This sets the initial
        // message-thread state; prepareToPlay reconfigures from it thereafter.
        const LoadedSettings loaded = persistence_.load();
        juce::File requestedFile;
        if (loaded.source.mode == SourceMode::file && !loaded.source.filePath.empty()) {
            requestedFile = juce::File(juce::String::fromUTF8(
                loaded.source.filePath.c_str(),
                static_cast<int>(loaded.source.filePath.length())));
        } else if (const char* path = std::getenv("ACFX_WORKBENCH_FILE")) {
            requestedFile = juce::File(juce::String::fromUTF8(path));
        }
        if (requestedFile != juce::File{}) {
            // Validate BEFORE selecting file mode: a saved/seeded file that has since
            // been moved or deleted must not launch into a muted file source. Fall back
            // to live (a usable state) and surface the missing file — not silent silence
            // (FR-009 / SC-006; AUDIT-20260627-09). This surfaced fallback is the
            // spec-mandated safe state, distinct from a silent Constitution-V fallback.
            if (requestedFile.existsAsFile()) {
                mode_ = SourceMode::file;
                sourceFile_ = requestedFile;
            } else {
                mode_ = SourceMode::live;
                missingSavedFile_ = requestedFile.getFullPathName();
            }
        }

        // Remember the saved output AND input device names (if any) so either one that
        // has since vanished can be surfaced after the manager falls back (FR-009 edge
        // case; AUDIT-20260627-02). Also keep a copy of the saved device-state XML so a
        // fallback session does not clobber the saved preference on quit/auto-save
        // (AUDIT-20260627-01).
        if (loaded.deviceState != nullptr) {
            preferredOutputDevice_ = loaded.deviceState->getStringAttribute("audioOutputDeviceName");
            preferredInputDevice_ = loaded.deviceState->getStringAttribute("audioInputDeviceName");
            preferredDeviceState_ = std::make_unique<juce::XmlElement>(*loaded.deviceState);
        }

        setSize(520, 300);
        // Restore devices/rate/buffer (and enabled MIDI inputs) from the saved state; a
        // null state initialises defaults and a saved-but-missing device falls back to
        // an available one (selectDefaultDeviceOnFailure). This drives prepareToPlay,
        // which reads the source state set above.
        setAudioChannels(2, 2, loaded.deviceState.get());

        // MIDI inputs (US4): the AudioDeviceSelectorComponent's MIDI-inputs section is
        // the explicit per-device control, and the enabled set is persisted in the
        // device-manager state. On FIRST run only (no saved state) enable all inputs
        // once as a sensible default; thereafter the restored state decides which are
        // enabled. Registering the callback with an empty device id delivers messages
        // from only the ENABLED inputs, so a disabled controller has no effect (SC-005).
        // A callback alone enables no device, hence the first-run enable.
        if (loaded.deviceState == nullptr) {
            for (const auto& input : juce::MidiInput::getAvailableDevices())
                deviceManager.setMidiInputDeviceEnabled(input.identifier, true);
        }
        deviceManager.addMidiInputDeviceCallback({}, this);

        // Persist on every later device-configuration change (added last so the restore
        // and auto-enable above do not trigger redundant construction-time saves).
        deviceManager.addChangeListener(this);

        surfaceStartupIssues(loaded.corrupt);
    }

    ~WorkbenchComponent() override {
        deviceManager.removeChangeListener(this);
        saveSettings(); // persist the final selection on quit (FR-006)
        deviceManager.removeMidiInputDeviceCallback({}, this);
        shutdownAudio();
    }

    void prepareToPlay(int blockSize, double sampleRate) override {
        // Prepare for the device's ACTUAL output channel count (setAudioChannels
        // is a request, not a guarantee), bounded by kMaxChannels — so the count
        // the effect is prepared for matches the count process() drives.
        const int outputs = numOutputChannels();
        preparedChannels_ = juce::jlimit(1, kMaxChannels, outputs > 0 ? outputs : 2);
        const ProcessContext ctx{sampleRate, blockSize, preparedChannels_};
        node_->prepare(ctx);

        // The SINGLE source reconfigure point (FR-008): release any prior selection,
        // (re)configure from the current message-thread state, then prepare. JUCE
        // brackets prepareToPlay between audioDeviceStopped/audioDeviceAboutToStart, so
        // the audio callback is guaranteed STOPPED here — the source buffers are never
        // reassigned under a live callback. Every device/source change routes through a
        // device restart (restartAudio / the device selector), which re-enters here.
        source_.release();
        const int inputs = numInputChannels();
        try {
            // No silent fallback (Constitution V): configure exactly the selected
            // source; a failure (no input, unreadable file) is surfaced below.
            if (mode_ == SourceMode::file)
                source_.useFilePlayer(sourceFile_);
            else
                source_.useLiveInput(inputs);
            source_.prepare(sampleRate, blockSize);
            sourceReady_ = true;
        } catch (const AudioSourceError& e) {
            // Surface the failure to the operator instead of swallowing it — no
            // silent fallback to silence (Constitution V).
            sourceReady_ = false;
            const juce::String message(e.what());
            juce::MessageManager::callAsync([message] {
                juce::NativeMessageBox::showMessageBoxAsync(
                    juce::MessageBoxIconType::WarningIcon, "Audio source unavailable", message);
            });
        }
    }

    void getNextAudioBlock(const juce::AudioSourceChannelInfo& info) override {
        // Parameter edits are consumed inside SvfEffect::process() (atomic
        // pending), so the audio thread needs no separate apply step here.
        juce::AudioBuffer<float>& buffer = *info.buffer;
        const int startSample = info.startSample;
        const int numSamples = info.numSamples;

        // No usable source (prepare failed): output silence rather than read an
        // unprepared source. The failure was already surfaced in prepareToPlay.
        if (!sourceReady_) {
            buffer.clear(startSample, numSamples);
            return;
        }

        // Bound to the count the effect was prepared for (never exceed it).
        const int numChannels = juce::jmin(buffer.getNumChannels(), preparedChannels_);

        // Pull the source audio into the destination region. For live input the
        // device samples are already in this buffer — JUCE's AudioSourcePlayer
        // memcpy's inputChannelData into the output buffer before getNextAudioBlock
        // (juce_AudioSourcePlayer.cpp) — so fillBlock passes them through and the
        // SVF processes them in place. fillBlock is noexcept and lock-free.
        juce::AudioBuffer<float> region(buffer.getArrayOfWritePointers(),
                                        buffer.getNumChannels(), startSample, numSamples);
        source_.fillBlock(region);

        if (!processed_.load())
            return; // A/B: dry — leave the source audio untouched

        std::array<float*, kMaxChannels> chans{};
        for (int ch = 0; ch < numChannels; ++ch)
            chans[ch] = buffer.getWritePointer(ch, startSample);
        AudioBlock block(chans.data(), numChannels, numSamples);
        node_->processBlock(block);
    }

    void releaseResources() override { source_.release(); }

    void resized() override {
        auto area = getLocalBounds();
        audioSettingsButton_.setBounds(area.removeFromTop(32).reduced(8, 4));
        sourceBar_.setBounds(area.removeFromTop(36).reduced(4, 2));
        abToggle_.setBounds(area.removeFromBottom(32).reduced(8, 4));
        paramView_.setBounds(area);
    }

private:
    int numInputChannels() const {
        if (auto* device = deviceManager.getCurrentAudioDevice())
            return device->getActiveInputChannels().countNumberOfSetBits();
        return 0;
    }

    int numOutputChannels() const {
        if (auto* device = deviceManager.getCurrentAudioDevice())
            return device->getActiveOutputChannels().countNumberOfSetBits();
        return 0;
    }

    // Apply a source change with the audio callback STOPPED. restartLastAudioDevice()
    // drives audioDeviceStopped -> releaseResources -> audioDeviceAboutToStart ->
    // prepareToPlay, and prepareToPlay reconfigures the source from the updated
    // message-thread state. The swap therefore happens entirely inside that stopped
    // window (FR-008) — no mid-callback source change. Message-thread only.
    void restartAudio() { deviceManager.restartLastAudioDevice(); }

    // Open (creating on first use) the Audio Settings window. The selector's own edits
    // drive the device restart cycle, so a device change reconfigures the source via
    // prepareToPlay with the callback stopped — no extra wiring needed here.
    void showAudioSettings() {
        if (audioSettings_ == nullptr)
            audioSettings_ = std::make_unique<AudioSettingsWindow>(deviceManager);
        audioSettings_->setVisible(true);
        audioSettings_->toFront(true);
    }

    // The current source selection as the persistable SourceConfig (the JUCE ->
    // std::string boundary; serde itself is JUCE-free).
    SourceConfig currentSourceConfig() const {
        if (mode_ == SourceMode::file)
            return SourceConfig{SourceMode::file, sourceFile_.getFullPathName().toStdString()};
        return SourceConfig{SourceMode::live, ""};
    }

    // True when a saved preferred device (output or input) is not in the current
    // device list — i.e. the open device is an automatic fallback we must not persist
    // over the saved preference (AUDIT-20260627-01).
    bool preferredDeviceUnavailable() const {
        auto* type = deviceManager.getCurrentDeviceTypeObject();
        if (type == nullptr)
            return false;
        const bool outMissing = preferredOutputDevice_.isNotEmpty()
                                && !type->getDeviceNames(false).contains(preferredOutputDevice_);
        const bool inMissing = preferredInputDevice_.isNotEmpty()
                               && !type->getDeviceNames(true).contains(preferredInputDevice_);
        return outMissing || inMissing;
    }

    // Capture the live device state as the new preferred snapshot, so a later fallback
    // preserves the user's current (deliberate) selection rather than a stale one.
    void refreshPreferredSnapshot() {
        preferredDeviceState_ = deviceManager.createStateXml();
        if (preferredDeviceState_ != nullptr) {
            preferredOutputDevice_ =
                preferredDeviceState_->getStringAttribute("audioOutputDeviceName");
            preferredInputDevice_ =
                preferredDeviceState_->getStringAttribute("audioInputDeviceName");
        }
    }

    void saveSettings() {
        // While a preferred device is temporarily unavailable, preserve the saved
        // device-state rather than clobbering it with the fallback device, so the
        // preference is reselected when the device returns (AUDIT-20260627-01). The
        // tradeoff: a deliberate device change made DURING a fallback session is not
        // persisted until the preferred device is back — the conservative choice, since
        // a fallback and a deliberate pick are indistinguishable here. The source
        // selection is always persisted.
        if (preferredDeviceUnavailable() && preferredDeviceState_ != nullptr) {
            persistence_.savePreserving(preferredDeviceState_.get(), currentSourceConfig());
            return;
        }
        persistence_.save(deviceManager, currentSourceConfig());
        refreshPreferredSnapshot();
    }

    // Device-manager changes (device/rate/buffer/MIDI edits via the selector) persist
    // the new configuration (FR-006).
    void changeListenerCallback(juce::ChangeBroadcaster*) override { saveSettings(); }

    // Surface (never swallow) startup problems: unreadable saved settings, or a saved
    // output OR input device that is gone and was fallen back from (FR-009;
    // AUDIT-20260627-02). Defaults are already in effect by the time this runs; this
    // only informs the person. The active names are read from the live state XML — the
    // same attribute source as the saved names — so the comparison is apples-to-apples.
    void surfaceStartupIssues(bool corrupt) {
        juce::StringArray messages;
        if (corrupt)
            messages.add("Your saved workbench settings were unreadable; starting with "
                         "defaults.");
        if (missingSavedFile_.isNotEmpty())
            messages.add("Saved audio file \"" + missingSavedFile_ + "\" was not found; "
                         "using live input.");

        juce::String activeOutput, activeInput;
        if (auto state = deviceManager.createStateXml()) {
            activeOutput = state->getStringAttribute("audioOutputDeviceName");
            activeInput = state->getStringAttribute("audioInputDeviceName");
        }
        if (preferredOutputDevice_.isNotEmpty() && activeOutput.isNotEmpty()
            && preferredOutputDevice_ != activeOutput)
            messages.add("Saved output device \"" + preferredOutputDevice_ + "\" was "
                         "unavailable; using \"" + activeOutput + "\" instead.");
        if (preferredInputDevice_.isNotEmpty() && activeInput.isNotEmpty()
            && preferredInputDevice_ != activeInput)
            messages.add("Saved input device \"" + preferredInputDevice_ + "\" was "
                         "unavailable; using \"" + activeInput + "\" instead.");

        if (messages.isEmpty())
            return;
        const juce::String text = messages.joinIntoString("\n");
        juce::MessageManager::callAsync([text] {
            juce::NativeMessageBox::showMessageBoxAsync(
                juce::MessageBoxIconType::WarningIcon, "Workbench settings", text);
        });
    }

    void handleIncomingMidiMessage(juce::MidiInput*, const juce::MidiMessage& msg) override {
        midi_.handle(msg, [this](ParamId id, float norm) {
            node_->setParameter(id, norm); // core is thread-safe (atomic pending)
            // Reflect into the GUI on the message thread, guarded so a callback
            // queued before teardown never touches a destroyed component.
            juce::Component::SafePointer<ParameterView> safeView(&paramView_);
            juce::MessageManager::callAsync([safeView, id, norm] {
                if (safeView != nullptr)
                    safeView->setNormalized(id, norm);
            });
        });
    }

    std::unique_ptr<ProcessorNode> node_;
    span<const ParameterDescriptor> params_;
    ParameterView paramView_;
    MidiBinding midi_;
    WorkbenchAudioSource source_;
    juce::ToggleButton abToggle_;
    juce::TextButton audioSettingsButton_;
    SourceBar sourceBar_;
    std::unique_ptr<AudioSettingsWindow> audioSettings_;
    WorkbenchPersistence persistence_;

    // The saved device preference, kept so a temporarily-unavailable device is not
    // clobbered by the fallback on save (AUDIT-20260627-01/02). Refreshed to the live
    // state whenever a non-fallback save happens.
    juce::String preferredOutputDevice_;
    juce::String preferredInputDevice_;
    std::unique_ptr<juce::XmlElement> preferredDeviceState_;

    // A saved/seeded file source whose file was missing at launch — surfaced once at
    // startup; the source falls back to live (AUDIT-20260627-09).
    juce::String missingSavedFile_;

    // Current source selection, owned on the message thread and read by prepareToPlay
    // (the single reconfigure point). The source bar (T010) mutates these and then
    // calls restartAudio() to apply the change with the callback stopped.
    SourceMode mode_ = SourceMode::live;
    juce::File sourceFile_;

    int preparedChannels_ = 2;
    bool sourceReady_ = false;
    std::atomic<bool> processed_{true};
};

class WorkbenchApplication final : public juce::JUCEApplication {
public:
    const juce::String getApplicationName() override { return "acfx Workbench"; }
    const juce::String getApplicationVersion() override { return "0.1.0"; }

    void initialise(const juce::String&) override {
        mainWindow_ = std::make_unique<MainWindow>(getApplicationName());
    }
    void shutdown() override { mainWindow_ = nullptr; }

private:
    class MainWindow final : public juce::DocumentWindow {
    public:
        explicit MainWindow(const juce::String& name)
            : juce::DocumentWindow(name, juce::Colours::darkgrey,
                                   juce::DocumentWindow::allButtons) {
            setUsingNativeTitleBar(true);
            setContentOwned(new WorkbenchComponent(), true);
            setResizable(true, true);
            centreWithSize(getWidth(), getHeight());
            setVisible(true);
        }
        void closeButtonPressed() override {
            juce::JUCEApplication::getInstance()->systemRequestedQuit();
        }
    };

    std::unique_ptr<MainWindow> mainWindow_;
};

} // namespace acfx::workbench

START_JUCE_APPLICATION(acfx::workbench::WorkbenchApplication)
