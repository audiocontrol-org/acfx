#pragma once

// The prepared run conditions handed to Effect::prepare. Provided by the adapter
// before audio starts and on any device change. maxBlockSize/numChannels bound
// every subsequent process() call (no reallocation mid-stream). No platform headers.

namespace acfx {

struct ProcessContext {
    double sampleRate;  // e.g. 48000.0
    int maxBlockSize;   // upper bound on AudioBlock::numSamples
    int numChannels;    // upper bound on AudioBlock::numChannels
};

} // namespace acfx
