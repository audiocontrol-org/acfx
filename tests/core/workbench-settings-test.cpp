#include <doctest/doctest.h>

#include "workbench-settings.h"

// T001 placeholder so the `test` preset configures + builds with stubs in place.
// The real SourceConfig serde contract test (round-trip + safe-default-on-garbage)
// lands in T003 and drives the T002 implementation.

TEST_CASE("workbench-settings TU links into the JUCE-free core test target") {
    using namespace acfx::workbench;
    // Default-constructed config is the safe default (live, empty path).
    SourceConfig cfg;
    CHECK(cfg.mode == SourceMode::live);
    CHECK(cfg.filePath.empty());
}
