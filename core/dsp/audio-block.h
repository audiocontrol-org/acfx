#pragma once

#include "dsp/span.h"

// A fixed-size, non-owning, non-allocating view of multichannel audio passed to
// process() (FR-002). In-place processing: input and output alias. numSamples
// varies per call but never exceeds the prepared maxBlockSize. No platform headers.

namespace acfx {

class AudioBlock {
public:
    AudioBlock(float* const* channels, int numChannels, int numSamples) noexcept
        : channels_(channels), numChannels_(numChannels), numSamples_(numSamples) {}

    int numChannels() const noexcept { return numChannels_; }
    int numSamples() const noexcept { return numSamples_; }

    // Mutable view of one channel's samples (in-place processing).
    float* channel(int ch) const noexcept { return channels_[ch]; }

private:
    float* const* channels_; // non-owning: points at the adapter's buffers
    int numChannels_;
    int numSamples_;
};

} // namespace acfx
