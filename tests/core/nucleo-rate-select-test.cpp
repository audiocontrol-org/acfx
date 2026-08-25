#include <doctest/doctest.h>

#include <cstdint>

#include "sample-rate.h"

// Sample-rate selection contract (FR-004, US2 / research §R3). Tests cover:
// SR1 — Supported rates: only 44100 and 48000 Hz are accepted by
//       isSupportedSampleRate(), all others (32000, 88200, 96000, 0, etc.) rejected.
// SR2 — Supported-rate table: kSupportedSampleRatesHz[] contains exactly {44100, 48000}
//       in order, with count kSupportedSampleRatesCount == 2.
// SR3 — Default rate: kDefaultSampleRateHz == 48000 (FR-004, R3).

using namespace acfx::nucleo;

// ============================================================================
// SR1: Supported rates
// ============================================================================

TEST_CASE("SR1: 44100 Hz is supported") {
    CHECK(isSupportedSampleRate(44100));
}

TEST_CASE("SR1: 48000 Hz is supported") {
    CHECK(isSupportedSampleRate(48000));
}

TEST_CASE("SR1: 32000 Hz is NOT supported") {
    CHECK_FALSE(isSupportedSampleRate(32000));
}

TEST_CASE("SR1: 88200 Hz is NOT supported") {
    CHECK_FALSE(isSupportedSampleRate(88200));
}

TEST_CASE("SR1: 96000 Hz is NOT supported") {
    CHECK_FALSE(isSupportedSampleRate(96000));
}

TEST_CASE("SR1: 0 Hz is NOT supported") {
    CHECK_FALSE(isSupportedSampleRate(0));
}

// ============================================================================
// SR2: Supported-rate table
// ============================================================================

TEST_CASE("SR2: kSupportedSampleRatesCount is 2") {
    CHECK(kSupportedSampleRatesCount == 2);
}

TEST_CASE("SR2: kSupportedSampleRatesHz[0] is 44100") {
    CHECK(kSupportedSampleRatesHz[0] == 44100);
}

TEST_CASE("SR2: kSupportedSampleRatesHz[1] is 48000") {
    CHECK(kSupportedSampleRatesHz[1] == 48000);
}

// ============================================================================
// SR3: Default rate
// ============================================================================

TEST_CASE("SR3: kDefaultSampleRateHz is 48000") {
    CHECK(kDefaultSampleRateHz == 48000);
}
