#pragma once

#include <cstdint>
#include <string>
#include <vector>

// A small, self-contained SHA-256 (FIPS 180-4) implementation. Host-tool-only
// (this is asset content-addressing for the lesson-asset fragment, not audio
// DSP), so it deliberately avoids pulling in a CPM crypto dependency for one
// hash function. No external deps, no platform headers.

namespace lessonassets {

// Returns the 64-character lowercase hex SHA-256 digest of `data`.
std::string sha256Hex(const std::vector<std::uint8_t>& data);

// Convenience overload for text content (JSON bytes, etc).
std::string sha256Hex(const std::string& text);

} // namespace lessonassets
