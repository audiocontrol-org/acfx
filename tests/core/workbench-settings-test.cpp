#include <doctest/doctest.h>

#include <string>

#include "workbench-settings.h"

// T003 — the SourceConfig serialize/parse contract (contracts/source-config.md). This
// is the one pure, JUCE-free, device-free seam of the workbench-audio-config feature,
// so it is the one piece worth a host-side unit test. Drives the T002 implementation.

using namespace acfx::workbench;

TEST_CASE("SourceConfig round-trips live mode") {
    const SourceConfig cfg{SourceMode::live, ""};
    CHECK(parse(serialize(cfg)) == cfg);
}

TEST_CASE("SourceConfig round-trips file mode with a plain path") {
    const SourceConfig cfg{SourceMode::file, "/tmp/loop.wav"};
    const SourceConfig out = parse(serialize(cfg));
    CHECK(out.mode == SourceMode::file);
    CHECK(out.filePath == "/tmp/loop.wav");
    CHECK(out == cfg);
}

TEST_CASE("SourceConfig round-trips file paths with spaces and unicode") {
    for (const std::string& path : {std::string("/Users/me/My Loops/take 1.wav"),
                                    std::string("/musique/échantillon/clip ☂.aiff"),
                                    std::string("/data/混音/loop.flac")}) {
        const SourceConfig cfg{SourceMode::file, path};
        const SourceConfig out = parse(serialize(cfg));
        CHECK(out.mode == SourceMode::file);
        CHECK(out.filePath == path);
    }
}

TEST_CASE("parse returns the safe default on empty / garbage / unknown-mode input") {
    const SourceConfig def{SourceMode::live, ""};
    CHECK(parse("") == def);
    CHECK(parse("nonsense") == def);
    CHECK(parse("mode=banana") == def);
    CHECK(parse("\n\n") == def);
}

TEST_CASE("parse never returns half-populated file mode (file token, empty path -> live)") {
    // A 'file' selection with no path is not a valid file source: it must default to
    // live rather than enter a broken empty-path file state.
    const SourceConfig out = parse(serialize(SourceConfig{SourceMode::file, ""}));
    CHECK(out.mode == SourceMode::live);
    CHECK(out.filePath.empty());
}

TEST_CASE("serialize emits the file path verbatim") {
    const std::string path = "/a/b c/d.wav";
    const std::string text = serialize(SourceConfig{SourceMode::file, path});
    CHECK(text.find(path) != std::string::npos);
}
