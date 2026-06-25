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
// toggle. Parameter edits from the GUI/MIDI threads are handed to the audio
// thread through a small lock-free queue so process() stays RT-safe.

namespace acfx::workbench {

namespace {
constexpr int kMaxParams = 8;
constexpr int kMaxChannels = 8;
} // namespace

class WorkbenchComponent final : public juce::AudioAppComponent,
                                 private juce::MidiInputCallback {
public:
    WorkbenchComponent()
        : node_(std::make_unique<EffectNode<SvfEffect>>()),
          paramView_(node_->parameters(),
                     [this](ParamId id, float norm) { postParam(id, norm); }) {
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
        deviceManager.addMidiInputDeviceCallback({}, this);
    }

    ~WorkbenchComponent() override {
        deviceManager.removeMidiInputDeviceCallback({}, this);
        shutdownAudio();
    }

    void prepareToPlay(int blockSize, double sampleRate) override {
        const ProcessContext ctx{sampleRate, blockSize, 2};
        node_->prepare(ctx);

        // Default to live input when the device offers it; otherwise the operator
        // must point the built-in player at a file (no silent fallback).
        const int inputs = numInputChannels();
        try {
            if (inputs > 0)
                source_.useLiveInput(inputs);
            source_.prepare(sampleRate, blockSize);
        } catch (const AudioSourceError& e) {
            lastSourceError_ = juce::String(e.what());
        }
    }

    void getNextAudioBlock(const juce::AudioSourceChannelInfo& info) override {
        applyPendingParams();

        juce::AudioBuffer<float>& buffer = *info.buffer;
        const int startSample = info.startSample;
        const int numSamples = info.numSamples;
        const int numChannels = juce::jmin(buffer.getNumChannels(), kMaxChannels);

        // Pull the source audio into the destination region (live input is already
        // present and passes through).
        try {
            juce::AudioBuffer<float> region(buffer.getArrayOfWritePointers(),
                                            buffer.getNumChannels(), startSample, numSamples);
            source_.fillBlock(region);
        } catch (const AudioSourceError&) {
            buffer.clear(startSample, numSamples);
            return;
        }

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

    // --- lock-free parameter handoff (GUI/MIDI thread -> audio thread) ---
    void postParam(ParamId id, float normalized) {
        const int idx = id.value;
        if (idx < 0 || idx >= kMaxParams)
            return;
        pendingValue_[idx].store(normalized);
        pendingDirty_[idx].store(true);
    }

    void applyPendingParams() {
        for (int i = 0; i < kMaxParams; ++i) {
            if (pendingDirty_[i].exchange(false))
                node_->setParameter(ParamId{static_cast<std::uint8_t>(i)},
                                    pendingValue_[i].load());
        }
    }

    void handleIncomingMidiMessage(juce::MidiInput*, const juce::MidiMessage& msg) override {
        midi_.handle(msg, [this](ParamId id, float norm) {
            postParam(id, norm);
            // Reflect into the GUI on the message thread.
            juce::MessageManager::callAsync(
                [this, id, norm] { paramView_.setNormalized(id, norm); });
        });
    }

    std::unique_ptr<ProcessorNode> node_;
    span<const ParameterDescriptor> params_;
    ParameterView paramView_;
    MidiBinding midi_;
    WorkbenchAudioSource source_;
    juce::ToggleButton abToggle_;
    juce::String lastSourceError_;

    std::atomic<bool> processed_{true};
    std::array<std::atomic<float>, kMaxParams> pendingValue_{};
    std::array<std::atomic<bool>, kMaxParams> pendingDirty_{};
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
