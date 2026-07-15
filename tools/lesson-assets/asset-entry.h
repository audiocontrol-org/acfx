#pragma once

#include <map>
#include <optional>
#include <string>
#include <vector>

// One produced asset, matching the AssetEntry shape from
// specs/svf-training-site/contracts/lesson-asset-manifest.md -- with `path`
// (a relative filename inside the tool's --out dir) standing in for the
// contract's final `url` field. This tool is the STATIC producer, not the
// manifest assembler (a later, separate task): the assembler resolves each
// fragment's `path` to an uploaded, absolute, content-hashed CDN `url` when
// it writes the committed site/public/manifest/svf.json. See
// contracts/lesson-asset-manifest.md "Producers -> fragments -> assembler".

namespace lessonassets {

struct AssetEntry {
    std::string kind; // "audio" | "response" | "impulse" (pole-zero intentionally not produced here)
    std::string path; // filename relative to the --out dir, content-hashed
    std::string sha256;
    std::string contentType;
    std::map<std::string, double> params; // Record<string, number> in the contract
    int sampleRate = 0;
    std::string provenance; // "tools/lesson-assets@<sourceProvenance>"
};

} // namespace lessonassets
