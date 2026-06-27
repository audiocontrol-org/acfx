#include "audio-source.h"

#include <cstdint>
#include <memory>

namespace acfx::workbench {

WorkbenchAudioSource::WorkbenchAudioSource() { formatManager_.registerBasicFormats(); }

void WorkbenchAudioSource::useFilePlayer(const juce::File& file) {
    if (configured_.load(std::memory_order_acquire))
        throw AudioSourceError("Cannot reselect the audio source while the stream is "
                               "running; release (stop audio) before reconfiguring.");
    if (!file.existsAsFile())
        throw AudioSourceError("Audio file does not exist: " + file.getFullPathName());

    std::unique_ptr<juce::AudioFormatReader> reader(formatManager_.createReaderFor(file));
    if (reader == nullptr)
        throw AudioSourceError("No decoder for audio file: " + file.getFullPathName());

    const int numChannels = static_cast<int>(reader->numChannels);
    // Validate length BEFORE narrowing to int (lengthInSamples is 64-bit). Bound
    // the in-memory buffer to a sane size (~93 min at 48 kHz) so a huge/corrupt
    // header can't overflow the int or exhaust memory.
    const std::int64_t lengthSamples = reader->lengthInSamples;
    constexpr std::int64_t kMaxSamples = std::int64_t{1} << 28;
    if (numChannels <= 0 || lengthSamples <= 0)
        throw AudioSourceError("Audio file is empty: " + file.getFullPathName());
    if (lengthSamples > kMaxSamples)
        throw AudioSourceError("Audio file is too long to load into memory: " +
                               file.getFullPathName());
    const int numSamples = static_cast<int>(lengthSamples);

    // Decode the whole file into memory on this (setup) thread. The audio thread
    // only ever reads fileBuffer_ thereafter — no reader, no transport, no lock.
    // A failed/partial decode (corrupt or truncated file) must surface, not install a
    // half-decoded buffer as a valid source (Constitution V; no silent placeholder
    // audio). Throw BEFORE mutating hasFile_/live_ so the prior source is preserved.
    juce::AudioBuffer<float> decoded(numChannels, numSamples);
    if (!reader->read(&decoded, 0, numSamples, 0, true, true))
        throw AudioSourceError("Failed to decode audio file (corrupt or truncated): " +
                               file.getFullPathName());

    fileBuffer_ = std::move(decoded);
    playPos_.store(0, std::memory_order_relaxed);
    hasFile_.store(true, std::memory_order_release);
    live_.store(false, std::memory_order_release);
}

void WorkbenchAudioSource::useLiveInput(int availableInputChannels) {
    if (configured_.load(std::memory_order_acquire))
        throw AudioSourceError("Cannot reselect the audio source while the stream is "
                               "running; release (stop audio) before reconfiguring.");
    if (availableInputChannels <= 0)
        throw AudioSourceError("Live input selected but the audio device offers no "
                               "input channels.");
    hasFile_.store(false, std::memory_order_release); // symmetric with useFilePlayer
    live_.store(true, std::memory_order_release);
}

void WorkbenchAudioSource::prepare(double, int) {
    if (!live_.load(std::memory_order_acquire) && !hasFile_.load(std::memory_order_acquire))
        throw AudioSourceError("No audio source configured: select the built-in "
                               "player (with a file) or a live input device.");
    configured_.store(true, std::memory_order_release);
}

void WorkbenchAudioSource::release() { configured_.store(false, std::memory_order_release); }

void WorkbenchAudioSource::fillBlock(juce::AudioBuffer<float>& block) noexcept {
    if (live_.load(std::memory_order_relaxed))
        return; // device input is already present in `block`

    const int fileLen = fileBuffer_.getNumSamples();
    if (!hasFile_.load(std::memory_order_relaxed) || fileLen <= 0) {
        block.clear();
        return;
    }

    const int fileChannels = fileBuffer_.getNumChannels();
    const int numSamples = block.getNumSamples();
    const int startPos = playPos_.load(std::memory_order_relaxed);

    for (int ch = 0; ch < block.getNumChannels(); ++ch) {
        const float* src = fileBuffer_.getReadPointer(ch < fileChannels ? ch : fileChannels - 1);
        float* dst = block.getWritePointer(ch);
        int pos = startPos;
        for (int i = 0; i < numSamples; ++i) {
            dst[i] = src[pos];
            if (++pos >= fileLen)
                pos = 0;
        }
    }

    playPos_.store((startPos + numSamples) % fileLen, std::memory_order_relaxed);
}

} // namespace acfx::workbench
