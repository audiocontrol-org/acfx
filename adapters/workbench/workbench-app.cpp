#include <array>
#include <atomic>
#include <memory>

#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include "audio-source.h"
#include "dsp/audio-block.h"
#include "dsp/process-context.h"
#include "effects/svf/svf-effect.h"
#include "midi-binding.h"
#include "parameter-view.h"
#include "processor-node/processor-node.h"

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
                                 private juce::MidiInputCallback {
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

        setSize(520, 220);
        // Stereo in/out: input present for live-input mode.
        setAudioChannels(2, 2);
        // Enable the available MIDI inputs — registering a callback alone does
        // not enable any device, so without this the CC bindings stay inert.
        for (const auto& input : juce::MidiInput::getAvailableDevices())
            deviceManager.setMidiInputDeviceEnabled(input.identifier, true);
        deviceManager.addMidiInputDeviceCallback({}, this);
    }

    ~WorkbenchComponent() override {
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

        // Default to live input when the device offers it; otherwise the operator
        // must point the built-in player at a file (no silent fallback).
        const int inputs = numInputChannels();
        try {
            if (inputs > 0)
                source_.useLiveInput(inputs);
            source_.prepare(sampleRate, blockSize);
        } catch (const AudioSourceError& e) {
            // Surface the failure to the operator instead of swallowing it — no
            // silent fallback to silence (Constitution V).
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
        // Bound to the count the effect was prepared for (never exceed it).
        const int numChannels = juce::jmin(buffer.getNumChannels(), preparedChannels_);

        // Pull the source audio into the destination region (live input is already
        // present and passes through). fillBlock is noexcept and lock-free.
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

    int preparedChannels_ = 2;
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
