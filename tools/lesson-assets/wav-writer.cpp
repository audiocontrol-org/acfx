#include "wav-writer.h"

#include <algorithm>
#include <cstring>

namespace lessonassets {
namespace {

void pushU32(std::vector<std::uint8_t>& out, std::uint32_t v) {
    out.push_back(static_cast<std::uint8_t>(v & 0xFF));
    out.push_back(static_cast<std::uint8_t>((v >> 8) & 0xFF));
    out.push_back(static_cast<std::uint8_t>((v >> 16) & 0xFF));
    out.push_back(static_cast<std::uint8_t>((v >> 24) & 0xFF));
}

void pushU16(std::vector<std::uint8_t>& out, std::uint16_t v) {
    out.push_back(static_cast<std::uint8_t>(v & 0xFF));
    out.push_back(static_cast<std::uint8_t>((v >> 8) & 0xFF));
}

void pushTag(std::vector<std::uint8_t>& out, const char* tag) {
    out.insert(out.end(), tag, tag + 4);
}

} // namespace

std::vector<std::uint8_t> buildMonoWav16(const std::vector<float>& samples, int sampleRate) {
    constexpr std::uint16_t kNumChannels = 1;
    constexpr std::uint16_t kBitsPerSample = 16;
    const std::uint32_t dataBytes = static_cast<std::uint32_t>(samples.size()) * sizeof(std::int16_t);
    const std::uint32_t byteRate =
        static_cast<std::uint32_t>(sampleRate) * kNumChannels * (kBitsPerSample / 8);
    const std::uint16_t blockAlign = static_cast<std::uint16_t>(kNumChannels * (kBitsPerSample / 8));

    std::vector<std::uint8_t> out;
    out.reserve(44 + dataBytes);

    // RIFF header
    pushTag(out, "RIFF");
    pushU32(out, 36 + dataBytes); // chunk size (file size - 8)
    pushTag(out, "WAVE");

    // fmt subchunk
    pushTag(out, "fmt ");
    pushU32(out, 16); // PCM fmt chunk size
    pushU16(out, 1);  // audio format: PCM
    pushU16(out, kNumChannels);
    pushU32(out, static_cast<std::uint32_t>(sampleRate));
    pushU32(out, byteRate);
    pushU16(out, blockAlign);
    pushU16(out, kBitsPerSample);

    // data subchunk
    pushTag(out, "data");
    pushU32(out, dataBytes);
    for (float s : samples) {
        const float clamped = std::clamp(s, -1.0f, 1.0f);
        const std::int16_t pcm = static_cast<std::int16_t>(clamped * 32767.0f);
        out.push_back(static_cast<std::uint8_t>(pcm & 0xFF));
        out.push_back(static_cast<std::uint8_t>((pcm >> 8) & 0xFF));
    }
    return out;
}

} // namespace lessonassets
