#pragma once

#include <memory>
#include <stdexcept>

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_audio_utils/juce_audio_utils.h>

// The workbench audio source (T025): a built-in looping file player OR live input
// device, selectable at runtime (research.md decision 2). The player is the
// deterministic default for reproducible A/B; live input is the real sketch use.
// If neither is available the source raises a descriptive error — never silent
// zeros or mock audio (Constitution V).

namespace acfx::workbench {

class AudioSourceError : public std::runtime_error {
public:
    explicit AudioSourceError(const juce::String& what)
        : std::runtime_error(what.toStdString()) {}
};

class WorkbenchAudioSource {
public:
    WorkbenchAudioSource();

    // Loop the given audio file as the source. Throws AudioSourceError if the
    // file cannot be opened/decoded.
    void useFilePlayer(const juce::File& file);

    // Use the live device input. `availableInputChannels` is what the device
    // offers; throws AudioSourceError if there are none.
    void useLiveInput(int availableInputChannels);

    void prepare(double sampleRate, int blockSize);
    void release();

    // Fill `block` with the next chunk of source audio. For live input, `block`
    // already holds the device input on entry and is passed through unchanged.
    void fillBlock(juce::AudioBuffer<float>& block);

    bool isLiveInput() const noexcept { return live_; }

private:
    juce::AudioFormatManager formatManager_;
    std::unique_ptr<juce::AudioFormatReaderSource> readerSource_;
    juce::AudioTransportSource transport_;
    bool live_ = false;
    bool configured_ = false;
};

} // namespace acfx::workbench
