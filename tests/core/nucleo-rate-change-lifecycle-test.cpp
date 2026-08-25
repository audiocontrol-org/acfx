#include <doctest/doctest.h>

#include <cstdint>

#include "rate-change.h"

// Rate-change latch exactly-once consumption contract (FR-006, research §R9).
//
// The latch synchronizes a rate-change request from the USB SET callback
// (producer, runs at interrupt time) to the poll-loop service step (consumer,
// runs in the main loop). The key guarantee is exactly-once consumption: the
// first consumePendingRateChange() after a requestRateChange() succeeds and
// yields the rate, but subsequent calls (with no new request) return false
// so the service step does not re-prepare the effect multiple times.

using namespace acfx::nucleo;

TEST_CASE("Rate-change latch: first consume succeeds, second returns false "
          "(exactly-once)") {
    RateChangeLatch latch;

    // Request a rate change to 44100 Hz.
    latch.requestRateChange(44100);

    // First consume: should return true and yield 44100.
    std::uint32_t rate1 = 0;
    CHECK(latch.consumePendingRateChange(rate1) == true);
    CHECK(rate1 == 44100);

    // Second consume (no new request): should return false (consumed exactly once).
    // This is the critical assertion: if the latch is buggy (e.g., never clears
    // the pending flag), it will return true here, failing this check.
    std::uint32_t rate2 = 0;
    CHECK(latch.consumePendingRateChange(rate2) == false);
}

TEST_CASE("Rate-change latch: new request re-arms after consumption") {
    RateChangeLatch latch;

    // Request and consume 44100.
    latch.requestRateChange(44100);
    std::uint32_t rate1 = 0;
    CHECK(latch.consumePendingRateChange(rate1) == true);
    CHECK(rate1 == 44100);

    // Second consume returns false (no new request yet).
    std::uint32_t rate2 = 0;
    CHECK(latch.consumePendingRateChange(rate2) == false);

    // Fresh request for 48000 re-arms the latch.
    latch.requestRateChange(48000);

    // Next consume should return true and yield the new rate.
    std::uint32_t rate3 = 0;
    CHECK(latch.consumePendingRateChange(rate3) == true);
    CHECK(rate3 == 48000);

    // And it's consumed exactly once.
    std::uint32_t rate4 = 0;
    CHECK(latch.consumePendingRateChange(rate4) == false);
}

TEST_CASE("Rate-change latch: consume on empty latch returns false") {
    RateChangeLatch latch;

    // No request has been made yet.
    std::uint32_t rate = 0;
    CHECK(latch.consumePendingRateChange(rate) == false);
}
