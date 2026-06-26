#include "audio-source.h"

namespace acfx::workbench {

WorkbenchAudioSource::WorkbenchAudioSource() { formatManager_.registerBasicFormats(); }

void WorkbenchAudioSource::useFilePlayer(const juce::File& file) {
    if (!file.existsAsFile())
        throw AudioSourceError("Audio file does not exist: " + file.getFullPathName());

    juce::AudioFormatReader* reader = formatManager_.createReaderFor(file);
    if (reader == nullptr)
        throw AudioSourceError("No decoder for audio file: " + file.getFullPathName());

    readerSource_ = std::make_unique<juce::AudioFormatReaderSource>(reader, /*deleteReader=*/true);
    readerSource_->setLooping(true);
    transport_.setSource(readerSource_.get());
    live_ = false;

    // If audio is already running (runtime source switch from the message
    // thread), prepare the freshly-installed transport for the current device
    // settings before the next audio callback reads from it.
    if (configured_) {
        transport_.prepareToPlay(blockSize_, sampleRate_);
        transport_.start();
    }
}

void WorkbenchAudioSource::useLiveInput(int availableInputChannels) {
    if (availableInputChannels <= 0)
        throw AudioSourceError("Live input selected but the audio device offers no "
                               "input channels.");
    live_ = true;
    readerSource_.reset();
    transport_.setSource(nullptr);
}

void WorkbenchAudioSource::prepare(double sampleRate, int blockSize) {
    if (!live_ && readerSource_ == nullptr)
        throw AudioSourceError("No audio source configured: select the built-in "
                               "player (with a file) or a live input device.");
    sampleRate_ = sampleRate;
    blockSize_ = blockSize;
    if (!live_) {
        transport_.prepareToPlay(blockSize, sampleRate);
        transport_.start(); // start off the audio thread, once, during prepare
    }
    configured_ = true;
}

void WorkbenchAudioSource::release() {
    if (!live_)
        transport_.releaseResources();
    configured_ = false;
}

void WorkbenchAudioSource::fillBlock(juce::AudioBuffer<float>& block) noexcept {
    // Audio thread: no throw, no allocation. An unconfigured or live source
    // leaves/zeroes the block instead of raising.
    if (live_)
        return; // device input is already present in `block`

    if (!configured_ || readerSource_ == nullptr) {
        block.clear();
        return;
    }

    juce::AudioSourceChannelInfo info(&block, 0, block.getNumSamples());
    transport_.getNextAudioBlock(info);
}

} // namespace acfx::workbench
