// Reads a vector JSON (schema in the plan), runs it through the SVF ABI, and
// prints {"version":1,"output":[...]} to stdout. Minimal hand-rolled JSON I/O so
// the reference has no external deps. Test-support code (not shipped).
#include "svf-web-abi.h"
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

static std::string slurp(const char* path) {
    FILE* f = std::fopen(path, "rb");
    if (!f) { std::fprintf(stderr, "cannot open %s\n", path); std::exit(2); }
    std::string s; char buf[4096]; size_t r;
    while ((r = std::fread(buf, 1, sizeof(buf), f)) > 0) s.append(buf, r);
    std::fclose(f);
    return s;
}

// Extract every number appearing in the JSON value for `key` (a flat array or bare number).
static std::vector<double> numbersAfter(const std::string& j, const std::string& key) {
    std::vector<double> out;
    size_t k = j.find("\"" + key + "\"");
    if (k == std::string::npos) return out;
    size_t colon = j.find(':', k);
    if (colon == std::string::npos) return out;
    // Skip whitespace after colon
    size_t start = colon + 1;
    while (start < j.size() && (j[start] == ' ' || j[start] == '\n' || j[start] == '\t')) ++start;
    if (start >= j.size()) return out;
    // Check if it's an array or a bare value
    if (j[start] == '[') {
        // Array: find matching ']'
        size_t lb = start, rb = j.find(']', lb);
        if (rb == std::string::npos) return out;
        size_t i = lb + 1;
        while (i < rb) {
            char* end = nullptr;
            double v = std::strtod(j.c_str() + i, &end);
            if (end == j.c_str() + i) { ++i; continue; }
            out.push_back(v);
            i = static_cast<size_t>(end - j.c_str());
        }
    } else {
        // Bare number: parse one number
        char* end = nullptr;
        double v = std::strtod(j.c_str() + start, &end);
        if (end != j.c_str() + start) out.push_back(v);
    }
    return out;
}

int main(int argc, char** argv) {
    if (argc < 2) { std::fprintf(stderr, "usage: svf-reference <vector.json>\n"); return 2; }
    const std::string j = slurp(argv[1]);
    const std::vector<double> sr = numbersAfter(j, "sampleRate");
    const std::vector<double> input = numbersAfter(j, "input");
    // params: pairs of id,norm harvested from the "params" array's numbers.
    const std::vector<double> params = numbersAfter(j, "params");

    std::vector<float> buf(input.size());
    for (size_t i = 0; i < input.size(); ++i) buf[i] = static_cast<float>(input[i]);

    SvfHandle* h = svf_create();
    svf_prepare(h, sr.empty() ? 48000.0 : sr[0], static_cast<int>(buf.size()), 1);
    for (size_t p = 0; p + 1 < params.size(); p += 2)
        svf_set_param(h, static_cast<unsigned char>(params[p]), static_cast<float>(params[p + 1]));
    svf_process(h, buf.data(), static_cast<int>(buf.size()));
    svf_destroy(h);

    std::printf("{\"version\":1,\"output\":[");
    for (size_t i = 0; i < buf.size(); ++i)
        std::printf("%s%.9g", i ? "," : "", static_cast<double>(buf[i]));
    std::printf("]}\n");
    return 0;
}
