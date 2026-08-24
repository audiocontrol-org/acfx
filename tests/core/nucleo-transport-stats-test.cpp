#include <doctest/doctest.h>

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <type_traits>

#include "transport-stats.h"

// Transport-statistics contract (FR-034a, FR-029a, FR-034, I-TS1/TS1a/TS2/TS4).
//
// WHAT THIS FILE CAN AND CANNOT PROVE.
// AudioTransportStats is a PASSIVE RECORD: a plain aggregate of counters plus one
// free function. Several of its guarantees are therefore obligations on the code
// that WRITES the record -- the shim in nucleo-main.cpp -- not properties of the
// record itself, and they are dishonest to "test" here:
//
//   TS1  wrap-not-saturate  -- follows from the counters being std::uint32_t, for
//        which modular arithmetic is guaranteed by the language. Asserting
//        `UINT32_MAX + 1 == 0` would test the C++ standard, not this codebase. What
//        IS worth pinning, and is pinned below, is the TYPE CHOICE that makes the
//        guarantee true: a signed counter would make overflow undefined behaviour
//        rather than a wrap, and a saturating type would break delta-based readers.
//   TS2  blocksProcessed increments exactly once per DSP block -- a cadence
//        obligation of the block loop. Enforced at T033 (block assembly) and
//        observable via T036's timing; nothing about the struct can establish it.
//   TS1a mutual exclusivity -- likewise a discipline of the increment sites. What
//        is pinned here is that the two conditions have SEPARATE fields, so the
//        shim is able to keep them distinct; the choosing is tested where it happens.
//
// Tests below therefore cover:
// TS1  — counter TYPES and zero-initialisation; the record carries no runtime reset.
// TS1a — inputStarved and outputUnderruns are separate, independently settable
//        fields covering different conditions (capture-only silence vs an open
//        playback stream coming up short).
// TS2  — blocksProcessed exists as the rate denominator errorRate() divides by.
// TS4  — errorRate(): zero blocks yields 0, never a division by zero, NaN or
//        infinity; a normal rate computes exactly; a rate above 1.0 is legal.

using namespace acfx::nucleo;

// ============================================================================
// TS1: counter types and initial state
// ============================================================================

TEST_CASE("TS1: every counter is uint32_t, which is what makes wrap-not-saturate true") {
    // The guarantee is modulo 2^32. Unsigned integer arithmetic wraps by
    // definition; signed overflow would be undefined behaviour, and any
    // saturating type would silently break consumers that take deltas between
    // snapshots. Pinning the type is the honest way to assert the guarantee at
    // this layer.
    using U = std::uint32_t;
    static_assert(std::is_same_v<decltype(AudioTransportStats::inputUnderruns), U>);
    static_assert(std::is_same_v<decltype(AudioTransportStats::inputOverruns), U>);
    static_assert(std::is_same_v<decltype(AudioTransportStats::outputUnderruns), U>);
    static_assert(std::is_same_v<decltype(AudioTransportStats::outputOverruns), U>);
    static_assert(std::is_same_v<decltype(AudioTransportStats::inputStarved), U>);
    static_assert(std::is_same_v<decltype(AudioTransportStats::malformedPayloads), U>);
    static_assert(std::is_same_v<decltype(AudioTransportStats::inputFifoFlushes), U>);
    static_assert(std::is_same_v<decltype(AudioTransportStats::inputFifoFlushedFrames), U>);
    static_assert(std::is_same_v<decltype(AudioTransportStats::blocksProcessed), U>);
    static_assert(std::is_same_v<decltype(AudioTransportStats::worstBlockMicros), U>);

    // A counter genuinely holds the top of its range without clamping.
    AudioTransportStats stats;
    stats.inputUnderruns = std::numeric_limits<std::uint32_t>::max();
    CHECK(stats.inputUnderruns == std::numeric_limits<std::uint32_t>::max());
}

TEST_CASE("TS1: every counter zero-initialises") {
    const AudioTransportStats stats;
    CHECK(stats.inputUnderruns == 0);
    CHECK(stats.inputOverruns == 0);
    CHECK(stats.outputUnderruns == 0);
    CHECK(stats.outputOverruns == 0);
    CHECK(stats.inputStarved == 0);
    CHECK(stats.malformedPayloads == 0);
    // Clear-on-tear (FR-028a / FR-032): an EVENT count and a FRAME count, two
    // fields with two units, both zero-initialised like every other counter.
    CHECK(stats.inputFifoFlushes == 0);
    CHECK(stats.inputFifoFlushedFrames == 0);
    CHECK(stats.blocksProcessed == 0);
    CHECK(stats.worstBlockMicros == 0);
}

TEST_CASE("TS1: the record is a plain aggregate with no runtime reset") {
    // "Not resettable at runtime" (TS1). A reset method would let a caller erase
    // the transport history the diagnostics report, and would make deltas between
    // snapshots meaningless. Aggregate-ness is the structural expression of that:
    // the record carries data, not behaviour.
    static_assert(std::is_aggregate_v<AudioTransportStats>);
    static_assert(std::is_trivially_copyable_v<AudioTransportStats>);
}

// ============================================================================
// TS1a: inputStarved and outputUnderruns are distinct conditions
// ============================================================================

TEST_CASE("TS1a: capture-only silence and playback underrun are separate fields") {
    // D22 / FR-029a. inputStarved means the host opened capture with no playback
    // stream feeding the device -- silence is the correct output and nothing has
    // failed. outputUnderruns means an open playback stream whose ring came up
    // short -- audio was lost. Folding them into one counter would make a healthy
    // capture-only session indistinguishable from a glitching duplex one.
    static_assert(offsetof(AudioTransportStats, inputStarved) !=
                  offsetof(AudioTransportStats, outputUnderruns));

    AudioTransportStats stats;
    stats.inputStarved = 7;
    CHECK(stats.inputStarved == 7);
    CHECK(stats.outputUnderruns == 0);
}

TEST_CASE("TS1a: recording one condition never disturbs the other") {
    AudioTransportStats stats;

    // A capture-only stretch: starvation accrues, underruns do not.
    stats.inputStarved = 12;
    CHECK(stats.inputStarved == 12);
    CHECK(stats.outputUnderruns == 0);

    // A later duplex stretch that glitches: underruns accrue, and the earlier
    // starvation count survives untouched. Both are lifetime totals.
    stats.outputUnderruns = 3;
    CHECK(stats.outputUnderruns == 3);
    CHECK(stats.inputStarved == 12);
}

// ============================================================================
// TS2: blocksProcessed is the rate denominator
// ============================================================================

TEST_CASE("TS2: blocksProcessed is the denominator errorRate divides by") {
    // The once-per-block cadence itself is the block loop's obligation (T033),
    // not the record's. What the record owes is a denominator that errorRate
    // actually uses -- proven by holding the numerator fixed and varying only
    // blocksProcessed.
    AudioTransportStats stats;
    stats.outputUnderruns = 10;

    stats.blocksProcessed = 100;
    const double rateOverHundred = errorRate(stats.outputUnderruns, stats);

    stats.blocksProcessed = 200;
    const double rateOverTwoHundred = errorRate(stats.outputUnderruns, stats);

    CHECK(rateOverHundred == doctest::Approx(0.1));
    CHECK(rateOverTwoHundred == doctest::Approx(0.05));
    CHECK(rateOverTwoHundred < rateOverHundred);
}

// ============================================================================
// TS4: errorRate over zero blocks
// ============================================================================

TEST_CASE("TS4: zero blocks yields a rate of 0, not a division by zero") {
    // "A rate over zero blocks is undefined, not infinite." The count is ignored
    // when nothing has been processed: before the first block there is no
    // denominator, and reporting infinity or NaN would poison every consumer.
    AudioTransportStats stats;
    stats.blocksProcessed = 0;

    stats.outputUnderruns = 0;
    CHECK(errorRate(stats.outputUnderruns, stats) == 0.0);

    stats.outputUnderruns = 10;
    const double rate = errorRate(stats.outputUnderruns, stats);
    CHECK(rate == 0.0);
    CHECK(!std::isinf(rate));
    CHECK(!std::isnan(rate));
}

TEST_CASE("TS4: a zero count over real blocks is a genuine zero rate") {
    AudioTransportStats stats;
    stats.blocksProcessed = 100;
    stats.outputUnderruns = 0;
    CHECK(errorRate(stats.outputUnderruns, stats) == 0.0);
}

TEST_CASE("TS4: a normal rate is count divided by blocksProcessed") {
    AudioTransportStats stats;
    stats.blocksProcessed = 100;
    stats.outputUnderruns = 10;
    CHECK(errorRate(stats.outputUnderruns, stats) == doctest::Approx(0.1));
}

TEST_CASE("TS4: a rate above 1.0 is legal, not clamped") {
    // More than one error per block is possible -- a single block can both
    // underrun and see a malformed payload -- and FR-034c reports a normalized
    // health rate, not a proportion of failed transfers. Clamping to 1.0 would
    // hide the worst cases exactly when they matter most.
    AudioTransportStats stats;
    stats.blocksProcessed = 100;
    stats.outputUnderruns = 150;
    CHECK(errorRate(stats.outputUnderruns, stats) == doctest::Approx(1.5));
}

TEST_CASE("TS4: the count argument is independent of which counter supplied it") {
    // errorRate takes a bare count, so any counter can be expressed against the
    // same denominator. One case with two different counters is enough to pin
    // that; repeating it per field would add cases without adding coverage.
    AudioTransportStats stats;
    stats.blocksProcessed = 200;
    stats.inputUnderruns = 50;
    stats.malformedPayloads = 50;

    CHECK(errorRate(stats.inputUnderruns, stats) == doctest::Approx(0.25));
    CHECK(errorRate(stats.malformedPayloads, stats) ==
          errorRate(stats.inputUnderruns, stats));
}
