#include "fragment-writer.h"

#include <filesystem>
#include <fstream>
#include <stdexcept>

#include "json-utils.h"

namespace lessonassets {
namespace {

std::string serializeParams(const std::map<std::string, double>& params) {
    std::string out = "{";
    bool first = true;
    for (const auto& [key, value] : params) {
        if (!first)
            out += ",";
        first = false;
        out += "\"" + jsonEscape(key) + "\":" + jsonNumber(value);
    }
    out += "}";
    return out;
}

std::string serializeEntry(const AssetEntry& entry) {
    std::string out = "{";
    out += "\"kind\":\"" + jsonEscape(entry.kind) + "\",";
    out += "\"path\":\"" + jsonEscape(entry.path) + "\",";
    out += "\"sha256\":\"" + jsonEscape(entry.sha256) + "\",";
    out += "\"contentType\":\"" + jsonEscape(entry.contentType) + "\",";
    out += "\"params\":" + serializeParams(entry.params) + ",";
    out += "\"sampleRate\":" + jsonNumber(entry.sampleRate) + ",";
    out += "\"provenance\":\"" + jsonEscape(entry.provenance) + "\"";
    out += "}";
    return out;
}

} // namespace

std::string serializeFragment(const std::string& sourceProvenance, const std::vector<AssetEntry>& assets) {
    std::string out = "{\n";
    out += "  \"sourceProvenance\": \"" + jsonEscape(sourceProvenance) + "\",\n";
    out += "  \"assets\": [\n";
    for (std::size_t i = 0; i < assets.size(); ++i) {
        out += "    " + serializeEntry(assets[i]);
        if (i + 1 < assets.size())
            out += ",";
        out += "\n";
    }
    out += "  ]\n";
    out += "}\n";
    return out;
}

std::string writeFragment(const std::string& outDir, const std::string& sourceProvenance,
                           const std::vector<AssetEntry>& assets) {
    std::filesystem::create_directories(outDir);
    const std::filesystem::path path = std::filesystem::path(outDir) / "static.fragment.json";
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!file)
        throw std::runtime_error("failed to open for write: " + path.string());
    file << serializeFragment(sourceProvenance, assets);
    if (!file)
        throw std::runtime_error("failed writing: " + path.string());
    return path.string();
}

} // namespace lessonassets
