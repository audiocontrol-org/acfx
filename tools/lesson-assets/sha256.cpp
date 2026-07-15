#include "sha256.h"

#include <array>
#include <cstring>

// Reference implementation of FIPS 180-4 SHA-256, transcribed directly from
// the spec's constants/algorithm (no third-party source copied). Verified
// against known test vectors in the lesson-assets tool's own smoke check
// (asset-tool-main.cpp prints sha256("") at --self-test) and cross-checked
// against `shasum -a 256` on generated output files.

namespace lessonassets {
namespace {

constexpr std::array<std::uint32_t, 64> kRoundConstants = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2,
};

std::uint32_t rotr(std::uint32_t x, std::uint32_t n) noexcept { return (x >> n) | (x << (32u - n)); }

void processChunk(const std::uint8_t* chunk, std::array<std::uint32_t, 8>& h) noexcept {
    std::array<std::uint32_t, 64> w{};
    for (int i = 0; i < 16; ++i) {
        w[static_cast<std::size_t>(i)] = (static_cast<std::uint32_t>(chunk[i * 4 + 0]) << 24) |
                                          (static_cast<std::uint32_t>(chunk[i * 4 + 1]) << 16) |
                                          (static_cast<std::uint32_t>(chunk[i * 4 + 2]) << 8) |
                                          (static_cast<std::uint32_t>(chunk[i * 4 + 3]));
    }
    for (int i = 16; i < 64; ++i) {
        const std::uint32_t s0 = rotr(w[static_cast<std::size_t>(i - 15)], 7) ^
                                  rotr(w[static_cast<std::size_t>(i - 15)], 18) ^
                                  (w[static_cast<std::size_t>(i - 15)] >> 3);
        const std::uint32_t s1 = rotr(w[static_cast<std::size_t>(i - 2)], 17) ^
                                  rotr(w[static_cast<std::size_t>(i - 2)], 19) ^
                                  (w[static_cast<std::size_t>(i - 2)] >> 10);
        w[static_cast<std::size_t>(i)] = w[static_cast<std::size_t>(i - 16)] + s0 +
                                          w[static_cast<std::size_t>(i - 7)] + s1;
    }

    std::uint32_t a = h[0], b = h[1], c = h[2], d = h[3];
    std::uint32_t e = h[4], f = h[5], g = h[6], hh = h[7];

    for (int i = 0; i < 64; ++i) {
        const std::uint32_t s1 = rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25);
        const std::uint32_t ch = (e & f) ^ (~e & g);
        const std::uint32_t temp1 = hh + s1 + ch + kRoundConstants[static_cast<std::size_t>(i)] +
                                     w[static_cast<std::size_t>(i)];
        const std::uint32_t s0 = rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22);
        const std::uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
        const std::uint32_t temp2 = s0 + maj;

        hh = g;
        g = f;
        f = e;
        e = d + temp1;
        d = c;
        c = b;
        b = a;
        a = temp1 + temp2;
    }

    h[0] += a;
    h[1] += b;
    h[2] += c;
    h[3] += d;
    h[4] += e;
    h[5] += f;
    h[6] += g;
    h[7] += hh;
}

} // namespace

std::string sha256Hex(const std::vector<std::uint8_t>& data) {
    std::array<std::uint32_t, 8> h = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
                                       0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};

    // Padding: 0x80, zero bytes until length % 64 == 56, then the original
    // bit-length as a big-endian 64-bit integer.
    std::vector<std::uint8_t> padded(data);
    const std::uint64_t bitLen = static_cast<std::uint64_t>(data.size()) * 8u;
    padded.push_back(0x80);
    while (padded.size() % 64 != 56)
        padded.push_back(0x00);
    for (int i = 7; i >= 0; --i)
        padded.push_back(static_cast<std::uint8_t>((bitLen >> (i * 8)) & 0xFFu));

    for (std::size_t off = 0; off < padded.size(); off += 64)
        processChunk(padded.data() + off, h);

    static const char* kHex = "0123456789abcdef";
    std::string out;
    out.reserve(64);
    for (std::uint32_t word : h) {
        for (int shift = 24; shift >= 0; shift -= 8) {
            const std::uint8_t byte = static_cast<std::uint8_t>((word >> shift) & 0xFFu);
            out.push_back(kHex[byte >> 4]);
            out.push_back(kHex[byte & 0x0F]);
        }
    }
    return out;
}

std::string sha256Hex(const std::string& text) {
    std::vector<std::uint8_t> bytes(text.begin(), text.end());
    return sha256Hex(bytes);
}

} // namespace lessonassets
