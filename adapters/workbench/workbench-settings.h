#pragma once

#include <string>

// The workbench's persistable source selection — the one pure, JUCE-free,
// device-free seam of the audio-config feature (contracts/source-config.md).
// SourceConfig uses std::string (not juce::String) on purpose: this header and its
// serialize/parse pair compile into the JUCE-free host test target (acfx_core_tests),
// so the seam is unit-tested with no JUCE/app/device context. The workbench converts
// std::string <-> juce::String at the UI/file boundary.

namespace acfx::workbench {

enum class SourceMode { live, file };

struct SourceConfig {
    SourceMode mode = SourceMode::live;
    std::string filePath; // empty unless mode == file
};

inline bool operator==(const SourceConfig& a, const SourceConfig& b) {
    return a.mode == b.mode && a.filePath == b.filePath;
}
inline bool operator!=(const SourceConfig& a, const SourceConfig& b) { return !(a == b); }

// Pure value transforms (message thread; never the audio callback). serialize emits a
// stable settings string; parse never throws and returns the safe default
// SourceConfig{ live, "" } on empty/garbage/unknown-mode input, and only returns file
// mode when a non-empty path is present (contracts/source-config.md).
std::string serialize(const SourceConfig& cfg);
SourceConfig parse(const std::string& text);

} // namespace acfx::workbench
