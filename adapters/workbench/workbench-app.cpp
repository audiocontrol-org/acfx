#include "workbench-app.h"

#include <array>
#include <cstdlib>

#include "dsp/audio-block.h"
#include "dsp/process-context.h"
#include ACFX_EFFECT_HEADER

// The desktop sketch-and-hear workbench (T022, T026). Holds the effect behind the
// same host boundary the plugin uses — std::unique_ptr<ProcessorNode> =
// EffectNode<SvfEffect> — routes audio from the selected source through it, draws
// the auto-generated controls, binds MIDI CCs, and offers a dry/processed A/B
// toggle. Parameter edits from the GUI/MIDI threads go through
// ProcessorNode::setParameter, the RT-safe cross-thread ingress: the effect
// publishes each value as a lock-free atomic that process() consumes, so no
// separate workbench-side queue is needed.
//
// The class is DECLARED in workbench-app.h (so workbench-main.cpp, the JUCE app
// bootstrap, can construct one) and DEFINED here, out-of-line, per member.

namespace acfx::workbench {

// The concrete effect this build targets. Injected by the build via compile
// definitions (ACFX_EFFECT_TYPE / ACFX_EFFECT_HEADER) so the SAME adapter source
// builds a workbench for any type satisfying acfx::Effect — no per-effect source.
using AppEffect = ACFX_EFFECT_TYPE;

namespace {
constexpr int kMaxChannels = 8;
// Fundamental control row (~24px) + THD+N row + kHarmonicNumHarmonics rows
// (~14px each) + a little breathing room -- see harmonic-readout.cpp's layout.
constexpr int kHarmonicReadoutHeight = 28 + (1 + kHarmonicNumHarmonics) * 14 + 12;
} // namespace

WorkbenchComponent::WorkbenchComponent()
    : node_(std::make_unique<EffectNode<AppEffect>>()),
      paramView_(node_->parameters(),
                 [this](ParamId id, float norm) { node_->setParameter(id, norm); }),
      harmonicReadout_(harmonicProbe_) {
    params_ = node_->parameters();

    // Default MIDI map: bind a small fixed set of CCs to the effect's FIRST
    // parameters by index (effect-generic — no effect-specific enum members).
    // CC 74 -> param 0, CC 71 -> param 1. For SVF that is cutoff/resonance
    // (preserves prior behaviour); other effects map to their first two params.
    if (params_.size() > 0)
        midi_.bind(74, ParamId{0});
    if (params_.size() > 1)
        midi_.bind(71, ParamId{1});

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

    // Input/output level meter: shows on screen whether audio is arriving, and logs
    // the peaks ~once per second so the same answer is visible in the tailable log.
    meter_.onLog = [this](float inputPeak, float outputPeak) {
        juce::Logger::writeToLog(
            "level  in=" + juce::Decibels::toString(juce::Decibels::gainToDecibels(inputPeak))
            + "  out=" + juce::Decibels::toString(juce::Decibels::gainToDecibels(outputPeak)));
    };
    addAndMakeVisible(meter_);

    // Live harmonic readout (T030, US5, FR-014/FR-016): the audio thread only
    // ever calls harmonicProbe_.push() (see getNextAudioBlock) -- a bounded,
    // lock-free copy, no analysis, no allocation. This component's own message-
    // thread Timer drains that ring through the shared acfx::analysis::LiveReadout
    // and repaints the broadband harmonic spectrum + running THD+N figure.
    addAndMakeVisible(harmonicReadout_);

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

    setSize(520, 344 + kHarmonicReadoutHeight);
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

WorkbenchComponent::~WorkbenchComponent() {
    deviceManager.removeChangeListener(this);
    saveSettings(); // persist the final selection on quit (FR-006)
    deviceManager.removeMidiInputDeviceCallback({}, this);
    shutdownAudio();
}

void WorkbenchComponent::prepareToPlay(int blockSize, double sampleRate) {
    // Prepare for the device's ACTUAL output channel count (setAudioChannels
    // is a request, not a guarantee), bounded by kMaxChannels — so the count
    // the effect is prepared for matches the count process() drives.
    const int outputs = numOutputChannels();
    preparedChannels_ = juce::jlimit(1, kMaxChannels, outputs > 0 ? outputs : 2);
    const ProcessContext ctx{sampleRate, blockSize, preparedChannels_};
    node_->prepare(ctx);

    // (Re)configure the live harmonic readout for the device's actual sample
    // rate (message thread only; the analysis engine stays off the audio path,
    // FR-016). Mirrors node_->prepare(ctx) immediately above.
    harmonicReadout_.prepare(sampleRate);

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

    // Log the resolved audio configuration (no audio data — just the state). This is
    // the first place to look when diagnosing "no input": in=0 means the device has
    // no input channels (or the OS denied microphone access).
    juce::String deviceName("(none)");
    if (auto* dev = deviceManager.getCurrentAudioDevice())
        deviceName = dev->getName();
    juce::Logger::writeToLog(
        "prepareToPlay  device=\"" + deviceName + "\"  sr=" + juce::String(sampleRate, 0)
        + "  block=" + juce::String(blockSize) + "  in=" + juce::String(inputs)
        + "  out=" + juce::String(preparedChannels_) + "  source="
        + (mode_ == SourceMode::file ? "file:" + sourceFile_.getFullPathName()
                                     : juce::String("live"))
        + "  sourceReady=" + (sourceReady_ ? "yes" : "no"));
}

void WorkbenchComponent::getNextAudioBlock(const juce::AudioSourceChannelInfo& info) {
    // Parameter edits are consumed inside SvfEffect::process() (atomic
    // pending), so the audio thread needs no separate apply step here.
    juce::AudioBuffer<float>& buffer = *info.buffer;
    const int startSample = info.startSample;
    const int numSamples = info.numSamples;

    // No usable source (prepare failed): output silence rather than read an
    // unprepared source. The failure was already surfaced in prepareToPlay.
    if (!sourceReady_) {
        buffer.clear(startSample, numSamples);
        meter_.pushPeaks(0.0f, 0.0f);
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

    // Input level = the source audio BEFORE the filter (read-only scan; no alloc or
    // lock, RT-safe). Stored to the meter via lock-free atomics so the message
    // thread can show + log it — this is how "is audio arriving?" becomes visible.
    const float inputPeak = region.getMagnitude(0, numSamples);

    if (!processed_.load()) {
        meter_.pushPeaks(inputPeak, inputPeak); // A/B: dry — output equals input
        // Harmonic readout (T030): a bounded, lock-free, RT-safe handoff of the
        // block just sent to the output device — one representative channel
        // (channel 0), no analysis on this thread (FR-016). See
        // harmonic-readout.h/HarmonicReadout::timerCallback for the drain side.
        harmonicProbe_.push(acfx::span<const float>(region.getReadPointer(0),
                                                     static_cast<std::size_t>(numSamples)));
        return;
    }

    std::array<float*, kMaxChannels> chans{};
    for (int ch = 0; ch < numChannels; ++ch)
        chans[static_cast<std::size_t>(ch)] = buffer.getWritePointer(ch, startSample);
    AudioBlock block(chans.data(), numChannels, numSamples);
    node_->processBlock(block);

    meter_.pushPeaks(inputPeak, region.getMagnitude(0, numSamples));
    // Harmonic readout (T030): push the POST-effect output — the audio-thread
    // side of the RT capture probe (bounded copy, no allocation, no analysis).
    harmonicProbe_.push(acfx::span<const float>(region.getReadPointer(0),
                                                 static_cast<std::size_t>(numSamples)));
}

void WorkbenchComponent::releaseResources() { source_.release(); }

void WorkbenchComponent::resized() {
    auto area = getLocalBounds();
    audioSettingsButton_.setBounds(area.removeFromTop(32).reduced(8, 4));
    sourceBar_.setBounds(area.removeFromTop(36).reduced(4, 2));
    abToggle_.setBounds(area.removeFromBottom(32).reduced(8, 4));
    meter_.setBounds(area.removeFromBottom(44).reduced(8, 4));
    harmonicReadout_.setBounds(area.removeFromBottom(kHarmonicReadoutHeight).reduced(8, 4));
    paramView_.setBounds(area);
}

int WorkbenchComponent::numInputChannels() const {
    if (auto* device = deviceManager.getCurrentAudioDevice())
        return device->getActiveInputChannels().countNumberOfSetBits();
    return 0;
}

int WorkbenchComponent::numOutputChannels() const {
    if (auto* device = deviceManager.getCurrentAudioDevice())
        return device->getActiveOutputChannels().countNumberOfSetBits();
    return 0;
}

void WorkbenchComponent::restartAudio() { deviceManager.restartLastAudioDevice(); }

void WorkbenchComponent::showAudioSettings() {
    if (audioSettings_ == nullptr)
        audioSettings_ = std::make_unique<AudioSettingsWindow>(deviceManager);
    audioSettings_->setVisible(true);
    audioSettings_->toFront(true);
}

SourceConfig WorkbenchComponent::currentSourceConfig() const {
    if (mode_ == SourceMode::file)
        return SourceConfig{SourceMode::file, sourceFile_.getFullPathName().toStdString()};
    return SourceConfig{SourceMode::live, ""};
}

bool WorkbenchComponent::preferredDeviceUnavailable() const {
    auto* type = deviceManager.getCurrentDeviceTypeObject();
    if (type == nullptr)
        return false;
    const bool outMissing = preferredOutputDevice_.isNotEmpty()
                            && !type->getDeviceNames(false).contains(preferredOutputDevice_);
    const bool inMissing = preferredInputDevice_.isNotEmpty()
                           && !type->getDeviceNames(true).contains(preferredInputDevice_);
    return outMissing || inMissing;
}

void WorkbenchComponent::refreshPreferredSnapshot() {
    preferredDeviceState_ = deviceManager.createStateXml();
    if (preferredDeviceState_ != nullptr) {
        preferredOutputDevice_ =
            preferredDeviceState_->getStringAttribute("audioOutputDeviceName");
        preferredInputDevice_ =
            preferredDeviceState_->getStringAttribute("audioInputDeviceName");
    }
}

void WorkbenchComponent::saveSettings() {
    // While preferred device is unavailable, preserve the saved state so it is
    // reselected when the device returns (AUDIT-20260627-01). The source selection
    // is always persisted.
    if (preferredDeviceUnavailable() && preferredDeviceState_ != nullptr) {
        persistence_.savePreserving(preferredDeviceState_.get(), currentSourceConfig());
        return;
    }
    persistence_.save(deviceManager, currentSourceConfig());
    refreshPreferredSnapshot();
}

void WorkbenchComponent::changeListenerCallback(juce::ChangeBroadcaster*) { saveSettings(); }

void WorkbenchComponent::surfaceStartupIssues(bool corrupt) {
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

void WorkbenchComponent::handleIncomingMidiMessage(juce::MidiInput*, const juce::MidiMessage& msg) {
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

} // namespace acfx::workbench
