#include <doctest/doctest.h>

#include <cstdint>
#include <optional>

#include "midi-cc-map.h"

// MIDI CC mapping contract (I-MC1, I-MC2, I-MC3).
//
// Contract: `std::optional<int> acfx::nucleo::mapCcToParam(std::uint8_t cc, int paramCount) noexcept`
// is pure and stateless. It resolves a CC number to a parameter index bounded by paramCount,
// or returns std::nullopt if the CC is unmapped or the resolved index is out of range.
//
// Concrete convention (workbench match):
//   CC 74 -> parameter index 0
//   CC 71 -> parameter index 1
//   All other CCs -> unmapped (return nullopt)
//
// Tests cover:
// MC1 — Unmapped CCs return nullopt (no state corruption).
// MC2 — An index beyond paramCount is never returned (bounds checked).
// MC3 — Pure/stateless: identical inputs always give identical outputs.

using namespace acfx::nucleo;

// ============================================================================
// MC1: Unmapped CCs return nullopt
// ============================================================================

TEST_CASE("MC1: unmapped CC (0) returns nullopt") {
    // CC 0 is not in the default binding table.
    const auto result = mapCcToParam(0, 10);
    CHECK(result == std::nullopt);
}

TEST_CASE("MC1: unmapped CC (20) returns nullopt") {
    // CC 20 is not in the default binding table.
    const auto result = mapCcToParam(20, 10);
    CHECK(result == std::nullopt);
}

TEST_CASE("MC1: unmapped CC (100) returns nullopt") {
    // CC 100 is not in the default binding table.
    const auto result = mapCcToParam(100, 10);
    CHECK(result == std::nullopt);
}

// ============================================================================
// MC2: Index beyond paramCount is never returned
// ============================================================================

TEST_CASE("MC2: CC 74 returns nullopt when paramCount=0 (index 0 >= paramCount)") {
    // CC 74 maps to index 0, but paramCount=0 means there are no valid parameters.
    // The function must return nullopt to prevent out-of-range access.
    const auto result = mapCcToParam(74, 0);
    CHECK(result == std::nullopt);
}

TEST_CASE("MC2: CC 71 returns nullopt when paramCount=1 (index 1 >= paramCount)") {
    // CC 71 maps to index 1, but paramCount=1 means only parameter 0 exists.
    // Index 1 is out of range, so return nullopt.
    const auto result = mapCcToParam(71, 1);
    CHECK(result == std::nullopt);
}

TEST_CASE("MC2: CC 74 returns 0 when paramCount=1 (index 0 < paramCount)") {
    // CC 74 maps to index 0, and paramCount=1, so index 0 is valid.
    const auto result = mapCcToParam(74, 1);
    REQUIRE(result.has_value());
    CHECK(*result == 0);
}

TEST_CASE("MC2: CC 71 returns 1 when paramCount=2 (index 1 < paramCount)") {
    // CC 71 maps to index 1, and paramCount=2, so index 1 is valid.
    const auto result = mapCcToParam(71, 2);
    REQUIRE(result.has_value());
    CHECK(*result == 1);
}

// ============================================================================
// Concrete convention: CC 74 <-> index 0, CC 71 <-> index 1
// ============================================================================

TEST_CASE("CC 74 maps to parameter index 0 (sufficient paramCount)") {
    const auto result = mapCcToParam(74, 10);
    REQUIRE(result.has_value());
    CHECK(*result == 0);
}

TEST_CASE("CC 71 maps to parameter index 1 (sufficient paramCount)") {
    const auto result = mapCcToParam(71, 10);
    REQUIRE(result.has_value());
    CHECK(*result == 1);
}

TEST_CASE("CC 74 with paramCount=100 returns 0") {
    const auto result = mapCcToParam(74, 100);
    REQUIRE(result.has_value());
    CHECK(*result == 0);
}

TEST_CASE("CC 71 with paramCount=100 returns 1") {
    const auto result = mapCcToParam(71, 100);
    REQUIRE(result.has_value());
    CHECK(*result == 1);
}

// ============================================================================
// MC3: Pure and stateless (identical inputs give identical outputs)
// ============================================================================

TEST_CASE("MC3: CC 74 gives same result on repeated calls") {
    const auto result1 = mapCcToParam(74, 10);
    const auto result2 = mapCcToParam(74, 10);
    const auto result3 = mapCcToParam(74, 10);
    CHECK(result1 == result2);
    CHECK(result2 == result3);
}

TEST_CASE("MC3: CC 71 gives same result on repeated calls") {
    const auto result1 = mapCcToParam(71, 10);
    const auto result2 = mapCcToParam(71, 10);
    const auto result3 = mapCcToParam(71, 10);
    CHECK(result1 == result2);
    CHECK(result2 == result3);
}

TEST_CASE("MC3: unmapped CC gives same result on repeated calls") {
    const auto result1 = mapCcToParam(20, 10);
    const auto result2 = mapCcToParam(20, 10);
    const auto result3 = mapCcToParam(20, 10);
    CHECK(result1 == result2);
    CHECK(result2 == result3);
    CHECK(result1 == std::nullopt);
}

TEST_CASE("MC3: CC 74 with paramCount=0 gives same result on repeated calls") {
    const auto result1 = mapCcToParam(74, 0);
    const auto result2 = mapCcToParam(74, 0);
    CHECK(result1 == result2);
    CHECK(result1 == std::nullopt);
}

// ============================================================================
// Combined: verify no state interaction between calls
// ============================================================================

TEST_CASE("MC3: interleaved calls with different CCs show no state corruption") {
    // Call CC 74, then CC 71, then CC 74 again. If there were shared state,
    // the second call might corrupt the first. Verify they are independent.
    const auto r1 = mapCcToParam(74, 10);
    const auto r2 = mapCcToParam(71, 10);
    const auto r3 = mapCcToParam(74, 10);
    const auto r4 = mapCcToParam(20, 10);

    CHECK(r1.has_value());
    CHECK(*r1 == 0);
    CHECK(r2.has_value());
    CHECK(*r2 == 1);
    CHECK(r3.has_value());
    CHECK(*r3 == 0);
    CHECK(r4 == std::nullopt);

    // r1 and r3 are identical, proving r2 and r4 did not corrupt state.
    CHECK(r1 == r3);
}

// ============================================================================
// MC3: modulation-parameter CC bindings (ModulatedDelayEffect indices 6..18).
// paramCount = 21 (kDelayTime..kLofiBits). Locks in the CC->index map so a
// future table edit that reshuffles or drops a modulation binding fails here.
// ============================================================================

TEST_CASE("MC3: modulation CCs resolve to their parameter indices (paramCount=21)") {
    using acfx::nucleo::mapCcToParam;
    const int n = 21;
    struct { std::uint8_t cc; int idx; } cases[] = {
        {78, 6}, {79, 7}, {80, 8},        // delay-line LFO
        {81, 9}, {82, 10}, {83, 11},      // cutoff LFO
        {85, 12}, {86, 13}, {87, 14},     // resonance LFO
        {88, 15}, {89, 16},               // wow
        {90, 17}, {91, 18},               // flutter
    };
    for (const auto& c : cases) {
        const auto r = mapCcToParam(c.cc, n);
        REQUIRE(r.has_value());
        CHECK(*r == c.idx);
    }
    // CC84 is deliberately NOT bound (standard Portamento-Control CC).
    CHECK(mapCcToParam(84, n) == std::nullopt);
}
