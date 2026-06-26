#include "workbench-settings.h"

#include <cstddef>

// SourceConfig serialize/parse (contracts/source-config.md). Pure std::string value
// transforms — no JUCE, no device, no audio thread. The format is a single mode token,
// and for file mode a newline followed by the path VERBATIM. Taking everything after
// the first newline as the path means the path may itself contain any character
// (spaces, unicode, even embedded newlines on filesystems that allow them) and still
// round-trips exactly.

namespace acfx::workbench {

namespace {
constexpr const char* kLiveToken = "live";
constexpr const char* kFileToken = "file";
} // namespace

std::string serialize(const SourceConfig& cfg) {
    // A file source needs a path to be valid; a file config with an empty path is not
    // a usable file source, so it serializes as live (symmetric with parse, which
    // refuses to reconstruct an empty-path file mode).
    if (cfg.mode == SourceMode::file && !cfg.filePath.empty())
        return std::string(kFileToken) + '\n' + cfg.filePath;
    return kLiveToken;
}

SourceConfig parse(const std::string& text) {
    const std::size_t newline = text.find('\n');
    const std::string modeToken =
        newline == std::string::npos ? text : text.substr(0, newline);
    const std::string pathPart =
        newline == std::string::npos ? std::string{} : text.substr(newline + 1);

    // Only reconstruct a file source when the token is exactly "file" AND a non-empty
    // path is present. Everything else — empty, garbage, unknown mode, or a file token
    // with no path — is the safe default (live, empty path); never throws.
    if (modeToken == kFileToken && !pathPart.empty())
        return SourceConfig{SourceMode::file, pathPart};
    return SourceConfig{SourceMode::live, ""};
}

} // namespace acfx::workbench
