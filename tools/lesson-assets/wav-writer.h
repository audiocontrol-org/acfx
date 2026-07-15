#pragma once

#include <cstdint>
#include <vector>

// Builds a minimal 16-bit PCM mono WAV file in memory (RIFF/WAVE header +
// data chunk). Returned as bytes rather than written straight to disk so the
// caller can SHA-256 the exact content before choosing the content-addressed
// filename (asset-tool-main.cpp / svf-asset-sweep.cpp).

namespace lessonassets {

// `samples` are floats in [-1, 1]; out-of-range values are clamped (defensive
// only — the SVF effect should never produce them for the sweep's inputs).
std::vector<std::uint8_t> buildMonoWav16(const std::vector<float>& samples, int sampleRate);

} // namespace lessonassets
