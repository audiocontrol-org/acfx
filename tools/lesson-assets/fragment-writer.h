#pragma once

#include <string>
#include <vector>

#include "asset-entry.h"

// Writes the SINGLE static.fragment.json this tool owns (T010). Per
// contracts/lesson-asset-manifest.md: "static.fragment.json (native host
// asset-tool): audio + response/pole-zero/impulse entries" -- one producer,
// one writer, never the final assembled manifest.

namespace lessonassets {

// Serializes {sourceProvenance, assets: AssetEntry[]} to JSON text.
std::string serializeFragment(const std::string& sourceProvenance, const std::vector<AssetEntry>& assets);

// Writes the fragment JSON to `<outDir>/static.fragment.json`. Returns the
// full path written, or throws std::runtime_error on I/O failure.
std::string writeFragment(const std::string& outDir, const std::string& sourceProvenance,
                           const std::vector<AssetEntry>& assets);

} // namespace lessonassets
