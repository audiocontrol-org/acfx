#pragma once

#include <cstdio>
#include <string>
#include <vector>

// Minimal, header-only JSON text helpers. This tool's output shapes are fixed
// and simple (flat objects, numeric arrays), so hand-rolled formatting avoids
// pulling in a JSON-library CPM dependency for a host tool that writes exactly
// two document shapes (response/impulse JSON + static.fragment.json).

namespace lessonassets {

// Formats a double with enough precision to round-trip a float, trimmed of
// trailing noise via %.9g (sufficient for float32-derived DSP data).
inline std::string jsonNumber(double v) {
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%.9g", v);
    return std::string(buf);
}

inline std::string jsonEscape(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 8);
    for (char c : s) {
        switch (c) {
        case '"':
            out += "\\\"";
            break;
        case '\\':
            out += "\\\\";
            break;
        case '\n':
            out += "\\n";
            break;
        default:
            out.push_back(c);
        }
    }
    return out;
}

inline std::string jsonNumberArray(const std::vector<double>& values) {
    std::string out = "[";
    for (std::size_t i = 0; i < values.size(); ++i) {
        if (i > 0)
            out += ",";
        out += jsonNumber(values[i]);
    }
    out += "]";
    return out;
}

} // namespace lessonassets
