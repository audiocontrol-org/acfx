#include <array>
#include <cstdio>
#include <iostream>
#include <string>

#include "fragment-writer.h"
#include "sha256.h"
#include "svf-asset-sweep.h"

// Native host CLI (T008): sweeps the real acfx::SvfEffect and writes the
// SVF lesson's static assets + static.fragment.json. Usage:
//
//   acfx_lesson_assets_tool [--out <dir>] [--source-hash <hash>] [--self-test]
//
// --out defaults to "build/lesson-assets/svf/" (relative to the current
// working directory the tool is invoked from -- typically the repo root via
// the `lesson-assets` CMake preset's build dir, but not tied to it; a
// standalone build-product output directory a caller may relocate).
//
// --source-hash: `sourceProvenance` (contracts/lesson-asset-manifest.md) --
// the DSP SOURCE hash the assets were produced from, "<coreTreeSha>:<webTreeSha>"
// (`git rev-parse HEAD:core` + `git rev-parse HEAD:adapters/web`, joined with
// ":"). Deliberately NOT whole-tree `git rev-parse HEAD`: a whole-tree hash
// would make the staleness guard (tools/staleness-guard.ts) fire after ANY
// commit anywhere in the repo, which is useless. Mirrors
// tools/manifest/provenance.ts's computeSourceProvenance() -- keep both in
// sync; the assembler fails loud if the two producers' fragments disagree.
// If --source-hash is omitted, the tool shells out to git at runtime
// (documented choice: avoids every caller having to plumb the hash through
// by hand; pass --source-hash explicitly for reproducible/offline builds
// where shelling out to git is undesirable).

namespace {

std::string runGitCommand(const std::string& command) {
    std::array<char, 128> buffer{};
    std::string result;
    FILE* pipe = popen(command.c_str(), "r");
    if (!pipe)
        throw std::runtime_error("failed to invoke `" + command + "`");
    while (fgets(buffer.data(), static_cast<int>(buffer.size()), pipe) != nullptr)
        result += buffer.data();
    const int status = pclose(pipe);
    while (!result.empty() && (result.back() == '\n' || result.back() == '\r'))
        result.pop_back();
    if (status != 0 || result.empty())
        throw std::runtime_error("`" + command + "` failed; pass --source-hash explicitly (not a git checkout?)");
    return result;
}

std::string computeSourceProvenance() {
    const std::string coreTreeSha = runGitCommand("git rev-parse HEAD:core 2>/dev/null");
    const std::string webTreeSha = runGitCommand("git rev-parse HEAD:adapters/web 2>/dev/null");
    return coreTreeSha + ":" + webTreeSha;
}

struct Options {
    std::string outDir = "build/lesson-assets/svf/";
    std::string sourceHash;
    bool selfTest = false;
};

Options parseArgs(int argc, char** argv) {
    Options opts;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--out" && i + 1 < argc) {
            opts.outDir = argv[++i];
        } else if (arg == "--source-hash" && i + 1 < argc) {
            opts.sourceHash = argv[++i];
        } else if (arg == "--self-test") {
            opts.selfTest = true;
        } else {
            std::cerr << "unrecognized argument: " << arg << "\n";
            std::cerr << "usage: acfx_lesson_assets_tool [--out <dir>] [--source-hash <hash>] "
                         "[--self-test]\n";
            std::exit(2);
        }
    }
    return opts;
}

} // namespace

int main(int argc, char** argv) {
    const Options opts = parseArgs(argc, argv);

    if (opts.selfTest) {
        // Known SHA-256 test vectors, printed for a human (or `shasum -a 256`)
        // to cross-check this tool's hand-rolled sha256.cpp against.
        std::cout << "sha256(\"\")    = " << lessonassets::sha256Hex(std::string()) << "\n";
        std::cout << "sha256(\"abc\") = " << lessonassets::sha256Hex(std::string("abc")) << "\n";
        return 0;
    }

    try {
        const std::string sourceHash = opts.sourceHash.empty() ? computeSourceProvenance() : opts.sourceHash;
        const std::string provenance = "tools/lesson-assets@" + sourceHash;

        const std::vector<lessonassets::SvfAssetPreset> presets = lessonassets::defaultPresets();
        const std::vector<lessonassets::AssetEntry> entries =
            lessonassets::sweepSvfPresets(opts.outDir, presets, provenance);

        const std::string fragmentPath = lessonassets::writeFragment(opts.outDir, sourceHash, entries);

        std::cout << "lesson-assets: wrote " << entries.size() << " asset(s) to " << opts.outDir << "\n";
        std::cout << "lesson-assets: wrote fragment " << fragmentPath << "\n";
        std::cout << "lesson-assets: sourceProvenance = " << sourceHash << "\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "lesson-assets: error: " << e.what() << "\n";
        return 1;
    }
}
