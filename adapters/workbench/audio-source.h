#pragma once

#include <atomic>
#include <stdexcept>

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_formats/juce_audio_formats.h>

// The workbench audio source (T025): a built-in looping file player OR live input
// device, selectable at runtime (research.md decision 2). The player is the
// deterministic default for reproducible A/B; live input is the real sketch use.
// If neither is available the source raises a descriptive error — never silent
// zeros or mock audio (Constitution V).
//
// RT-safety (Constitution VI): the file is decoded into an in-memory buffer
// before the stream starts (off the audio thread); fillBlock() then reads that
// buffer at an atomic play position with no locks and no allocation. Source
// selection (useFilePlayer / useLiveInput) is a RECONFIGURE operation, legitimately
// repeated whenever the workbench switches device or source — but ONLY while the
// audio callback is stopped. The lifecycle invariant is "release before reconfigure":
// the workbench calls release() (the audio-stopped window JUCE's restart cycle
// provides) before reselecting, then prepare(). That invariant is ENFORCED — a
// selection call while still configured (stream running) throws — so the audio thread
// never reads fileBuffer_/live_/hasFile_ while they are being reassigned.

namespace acfx::workbench {

class AudioSourceError : public std::runtime_error {
public:
    explicit AudioSourceError(const juce::String& what)
        : std::runtime_error(what.toStdString()) {}
};

class WorkbenchAudioSource {
public:
    WorkbenchAudioSource();

    // Decode the given file into memory and select it as the source. Throws
    // AudioSourceError if the file cannot be opened/decoded, or if the source is
    // still configured (the stream must be released/stopped before reselecting — the
    // release-before-reconfigure invariant). Call on the message thread, off the
    // audio thread.
    void useFilePlayer(const juce::File& file);

    // Use the live device input. `availableInputChannels` is what the device
    // offers; throws AudioSourceError if there are none, or if the source is still
    // configured (release before reselecting).
    void useLiveInput(int availableInputChannels);

    void prepare(double sampleRate, int blockSize);
    void release();

    // Fill `block` with the next chunk of source audio. For live input, `block`
    // already holds the device input on entry and is passed through unchanged.
    // Runs on the audio thread: never throws, never allocates, takes no locks.
    void fillBlock(juce::AudioBuffer<float>& block) noexcept;

    bool isLiveInput() const noexcept { return live_.load(std::memory_order_relaxed); }

private:
    juce::AudioFormatManager formatManager_;
    juce::AudioBuffer<float> fileBuffer_; // whole file decoded into memory at setup
    std::atomic<int> playPos_{0};
    std::atomic<bool> live_{false};
    std::atomic<bool> hasFile_{false};
    // Atomic like its siblings: written on the audio/device thread (prepare/
    // release) and read on the message thread (the selection-call guard), so the
    // guard reliably observes a running stream.
    std::atomic<bool> configured_{false};
};

} // namespace acfx::workbench
