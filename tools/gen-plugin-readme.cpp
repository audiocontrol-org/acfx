// Generate a plugin README (parameter reference + MIDI CC map) straight from the
// effect's own descriptor table and the shared firmware CC map, so the accuracy-
// critical parts (names, ranges, defaults, CC numbers) never drift from the code.
// The free-text "what it does" column is merged in by parameter name from a
// descriptions TSV (one "Group/Label<TAB>description" line per parameter).
//
// Build (see the `readme` just recipe):
//   c++ -std=c++20 -I core -I adapters/nucleo/support \
//       -DACFX_EFFECT_HEADER='"effects/.../foo.h"' -DACFX_EFFECT_TYPE=acfx::Foo \
//       tools/gen-plugin-readme.cpp -o gen && ./gen <descriptions.tsv> "<Product>" > README.md

#include ACFX_EFFECT_HEADER
#include "midi-cc-map.h"

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

using Effect = ACFX_EFFECT_TYPE;

static std::string unitSuffix(acfx::ParamUnit u) {
    switch (u) {
        case acfx::ParamUnit::hz:       return " Hz";
        case acfx::ParamUnit::decibels: return " dB";
        case acfx::ParamUnit::percent:  return " %";
        case acfx::ParamUnit::seconds:  return " s";
        default:                        return "";
    }
}

static std::string num(float v) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%.4g", static_cast<double>(v));
    return buf;
}

int main(int argc, char** argv) {
    if (argc < 3) { std::fprintf(stderr, "usage: gen <descriptions.tsv> <product>\n"); return 2; }
    const std::string descPath = argv[1];
    const std::string product  = argv[2];

    // Descriptions keyed by full "Group/Label" name.
    std::map<std::string, std::string> desc;
    { std::ifstream f(descPath);
      std::string line;
      while (std::getline(f, line)) {
          if (line.empty() || line[0] == '#') continue;
          const auto tab = line.find('\t');
          if (tab == std::string::npos) continue;
          desc[line.substr(0, tab)] = line.substr(tab + 1);
      } }

    // Inverse of the firmware CC map: param index -> CC number.
    std::map<int, int> idxToCc;
    for (const acfx::nucleo::CcBinding& b : acfx::nucleo::kCcBindings)
        idxToCc[b.paramIndex] = b.cc;

    const auto params = Effect::parameters();

    // Preserve first-seen group order.
    std::vector<std::string> groupOrder;
    std::map<std::string, std::vector<const acfx::ParameterDescriptor*>> byGroup;
    int missing = 0;
    for (const acfx::ParameterDescriptor& p : params) {
        std::string full(p.name);
        std::string group = full.substr(0, full.find('/'));
        if (byGroup.find(group) == byGroup.end()) groupOrder.push_back(group);
        byGroup[group].push_back(&p);
        if (desc.find(full) == desc.end()) { std::fprintf(stderr, "warning: no description for '%s'\n", full.c_str()); ++missing; }
    }

    std::printf("# %s — parameter reference\n\n", product.c_str());
    std::printf("Auto-generated from the effect's parameter table and the firmware MIDI CC map. "
                "Ranges, defaults and CC numbers are exact; descriptions are maintained per parameter.\n\n");

    std::printf("## Parameters\n\n");
    for (const std::string& g : groupOrder) {
        std::printf("### %s\n\n", g.c_str());
        std::printf("| Control | Range | Default | CC | What it does |\n");
        std::printf("|---|---|---|---|---|\n");
        for (const acfx::ParameterDescriptor* p : byGroup[g]) {
            std::string full(p->name);
            std::string label = full.substr(full.find('/') + 1);
            std::string range, deflt;
            if (p->kind == acfx::ParamKind::discrete && p->discreteCount >= 2) {
                std::string opts;
                for (int i = 0; i < p->discreteCount; ++i) { if (i) opts += " / "; opts += std::string(p->choices[static_cast<std::size_t>(i)]); }
                range = opts;
                const int di = static_cast<int>(p->defaultValue);
                deflt = (di >= 0 && di < p->discreteCount) ? std::string(p->choices[static_cast<std::size_t>(di)]) : num(p->defaultValue);
            } else {
                const std::string u = unitSuffix(p->unit);
                range = num(p->min) + "–" + num(p->max) + u;
                deflt = num(p->defaultValue) + u;
            }
            auto it = idxToCc.find(p->id.value);
            std::string cc = it != idxToCc.end() ? std::to_string(it->second) : "—";
            std::string d  = desc.count(full) ? desc[full] : "—";
            std::printf("| %s | %s | %s | %s | %s |\n", label.c_str(), range.c_str(), deflt.c_str(), cc.c_str(), d.c_str());
        }
        std::printf("\n");
    }

    // CC map sorted by CC number.
    std::printf("## MIDI CC map\n\n");
    std::printf("Move a plugin control (or send the CC to the hardware) to drive the matching parameter. "
                "Values are 0–127 across each control's range.\n\n");
    std::printf("| CC | Parameter |\n|---|---|\n");
    std::map<int, std::string> ccToName;
    for (const acfx::ParameterDescriptor& p : params) {
        auto it = idxToCc.find(p.id.value);
        if (it != idxToCc.end()) ccToName[it->second] = std::string(p.name);
    }
    for (const auto& [cc, name] : ccToName)
        std::printf("| %d | %s |\n", cc, name.c_str());

    return missing > 0 ? 1 : 0;   // non-zero if any parameter lacks a description
}
