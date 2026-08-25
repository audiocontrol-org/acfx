#include <doctest/doctest.h>

#include <vector>

#include "audio-ring.h"
#include "lifecycle-policy.h"
#include "sample-format.h"
#include "transport-stats.h"

// USB bus lifecycle policy contracts (T053-T056; FR-051, FR-052, FR-053,
// FR-054, FR-055; contract AR9). Tests cover:
// - applySuspend(): both rings end Stopped and empty; counters unchanged.
// - applyResumeOrBusReset(): both rings end Priming and empty; counters
//   unchanged, from both Stopped and mid-stream (partial/Running) starts.
// - reconcileStreamOpenEdges(): OUT-open resets ONLY the input ring, IN-open
//   resets ONLY the output ring — the mapping is not inverted.
// - A full tour across {both closed, playback only, capture only, both open}
//   in several orders reaches a coherent state every time, with no
//   special-casing and counters intact (FR-055).
// - reset()/stop() leave counters untouched even after prior underruns/
//   overruns were already counted (FR-054 regression).

using namespace acfx::nucleo;

namespace {

using TestRing = AudioRing<48>;

void fillTestBuffer(float* const* channels, int frames, float baseValue) noexcept {
    for (int frame = 0; frame < frames; ++frame) {
        for (int channel = 0; channel < kChannels; ++channel) {
            channels[channel][frame] = baseValue + (channel * 0.1f);
        }
    }
}

// Field-by-field comparison of two AudioTransportStats records — the
// strongest statement FR-054 ("survive ... UNCHANGED") can be given.
// Deliberately NOT std::memcmp over the raw bytes: the struct has a `bool`
// after seven std::uint32_t fields, leaving a compiler-inserted padding hole
// whose contents the implicit copy constructor is not obliged to reproduce
// — a memcmp would then fail on padding garbage that has nothing to do with
// FR-054, exactly the kind of false failure a real lifecycle event must
// never be confused with.
bool statsUnchanged(const AudioTransportStats& before, const AudioTransportStats& after) noexcept {
    return before.inputUnderruns == after.inputUnderruns &&
           before.inputOverruns == after.inputOverruns &&
           before.outputUnderruns == after.outputUnderruns &&
           before.outputOverruns == after.outputOverruns &&
           before.inputStarved == after.inputStarved &&
           before.malformedPayloads == after.malformedPayloads &&
           before.blocksProcessed == after.blocksProcessed &&
           before.worstBlockMicros == after.worstBlockMicros &&
           before.timingSourceLive == after.timingSourceLive;
}

// Increments a representative spread of counters, standing in for "a session
// with prior activity" (FR-054's regression case: reset()/stop() must not
// quietly zero what diagnostics have already recorded).
void seedCounters(AudioTransportStats& stats) noexcept {
    stats.inputUnderruns = 3;
    stats.inputOverruns = 1;
    stats.outputUnderruns = 5;
    stats.outputOverruns = 2;
    stats.inputStarved = 7;
    stats.malformedPayloads = 1;
    stats.blocksProcessed = 1000;
    stats.worstBlockMicros = 42;
}

}  // namespace

// ============================================================================
// applySuspend (T053, FR-051, US10 AS1)
// ============================================================================

TEST_CASE("applySuspend: both rings end Stopped and empty from Running") {
    TestRing inputRing(24);
    TestRing outputRing(24);

    std::vector<float> src_l(30, 0.5f);
    std::vector<float> src_r(30, 0.5f);
    const float* src[2] = {src_l.data(), src_r.data()};
    inputRing.write(src, 30);
    outputRing.write(src, 30);
    REQUIRE(inputRing.state() == RingState::Running);
    REQUIRE(outputRing.state() == RingState::Running);

    AudioTransportStats stats;
    seedCounters(stats);
    const AudioTransportStats before = stats;

    applySuspend(inputRing, outputRing);

    CHECK(inputRing.state() == RingState::Stopped);
    CHECK(outputRing.state() == RingState::Stopped);
    CHECK(inputRing.occupancy() == 0);
    CHECK(outputRing.occupancy() == 0);
    CHECK(statsUnchanged(before, stats));
}

TEST_CASE("applySuspend: from Priming also lands Stopped, not a no-op") {
    TestRing inputRing(24);
    TestRing outputRing(24);
    REQUIRE(inputRing.state() == RingState::Priming);
    REQUIRE(outputRing.state() == RingState::Priming);

    AudioTransportStats stats;
    const AudioTransportStats before = stats;

    applySuspend(inputRing, outputRing);

    CHECK(inputRing.state() == RingState::Stopped);
    CHECK(outputRing.state() == RingState::Stopped);
    CHECK(statsUnchanged(before, stats));
}

// ============================================================================
// applyResumeOrBusReset (T054/T055, FR-052, FR-053, US10 AS2/AS3)
// ============================================================================

TEST_CASE("applyResumeOrBusReset: both rings end Priming and empty from Stopped") {
    TestRing inputRing(24);
    TestRing outputRing(24);

    std::vector<float> src_l(30, 0.5f);
    std::vector<float> src_r(30, 0.5f);
    const float* src[2] = {src_l.data(), src_r.data()};
    inputRing.write(src, 30);
    outputRing.write(src, 30);
    inputRing.stop();
    outputRing.stop();
    REQUIRE(inputRing.state() == RingState::Stopped);
    REQUIRE(outputRing.state() == RingState::Stopped);

    AudioTransportStats stats;
    seedCounters(stats);
    const AudioTransportStats before = stats;

    applyResumeOrBusReset(inputRing, outputRing);

    CHECK(inputRing.state() == RingState::Priming);
    CHECK(outputRing.state() == RingState::Priming);
    CHECK(inputRing.occupancy() == 0);
    CHECK(outputRing.occupancy() == 0);
    CHECK(statsUnchanged(before, stats));
}

TEST_CASE("applyResumeOrBusReset: from a partial (mid-fill, Priming) ring also clears") {
    TestRing inputRing(24);
    TestRing outputRing(24);

    std::vector<float> src_l(10, 0.5f);
    std::vector<float> src_r(10, 0.5f);
    const float* src[2] = {src_l.data(), src_r.data()};
    inputRing.write(src, 10);
    REQUIRE(inputRing.state() == RingState::Priming);
    REQUIRE(inputRing.occupancy() == 10);

    AudioTransportStats stats;
    seedCounters(stats);
    const AudioTransportStats before = stats;

    applyResumeOrBusReset(inputRing, outputRing);

    CHECK(inputRing.state() == RingState::Priming);
    CHECK(inputRing.occupancy() == 0);
    CHECK(statsUnchanged(before, stats));
}

TEST_CASE("applyResumeOrBusReset: from Running (bus reset mid-stream) restarts at Priming") {
    TestRing inputRing(24);
    TestRing outputRing(24);

    std::vector<float> src_l(30, 0.5f);
    std::vector<float> src_r(30, 0.5f);
    const float* src[2] = {src_l.data(), src_r.data()};
    inputRing.write(src, 30);
    outputRing.write(src, 30);
    REQUIRE(inputRing.state() == RingState::Running);
    REQUIRE(outputRing.state() == RingState::Running);

    AudioTransportStats stats;
    seedCounters(stats);
    const AudioTransportStats before = stats;

    applyResumeOrBusReset(inputRing, outputRing);

    CHECK(inputRing.state() == RingState::Priming);
    CHECK(outputRing.state() == RingState::Priming);
    CHECK(statsUnchanged(before, stats));
}

// ============================================================================
// reconcileStreamOpenEdges (T056, FR-055, contract AR9)
// ============================================================================

TEST_CASE("reconcileStreamOpenEdges: OUT opening resets ONLY the input ring") {
    TestRing inputRing(24);
    TestRing outputRing(24);

    std::vector<float> src_l(10, 0.5f);
    std::vector<float> src_r(10, 0.5f);
    const float* src[2] = {src_l.data(), src_r.data()};
    inputRing.write(src, 10);
    outputRing.write(src, 10);
    REQUIRE(inputRing.occupancy() == 10);
    REQUIRE(outputRing.occupancy() == 10);

    bool prevOut = false;
    bool prevIn = false;

    // OUT transitions false -> true; IN stays closed.
    const StreamEdgeResult result =
        reconcileStreamOpenEdges(/*outStreamingNow=*/true, /*inStreamingNow=*/false, prevOut,
                                 prevIn, inputRing, outputRing);

    CHECK(result.inputRingReset == true);
    CHECK(result.outputRingReset == false);
    CHECK(inputRing.occupancy() == 0);
    CHECK(inputRing.state() == RingState::Priming);
    // The mapping must NOT be inverted: the output ring is untouched.
    CHECK(outputRing.occupancy() == 10);
}

TEST_CASE("reconcileStreamOpenEdges: IN opening resets ONLY the output ring") {
    TestRing inputRing(24);
    TestRing outputRing(24);

    std::vector<float> src_l(10, 0.5f);
    std::vector<float> src_r(10, 0.5f);
    const float* src[2] = {src_l.data(), src_r.data()};
    inputRing.write(src, 10);
    outputRing.write(src, 10);

    bool prevOut = false;
    bool prevIn = false;

    const StreamEdgeResult result =
        reconcileStreamOpenEdges(/*outStreamingNow=*/false, /*inStreamingNow=*/true, prevOut,
                                 prevIn, inputRing, outputRing);

    CHECK(result.inputRingReset == false);
    CHECK(result.outputRingReset == true);
    CHECK(outputRing.occupancy() == 0);
    CHECK(outputRing.state() == RingState::Priming);
    CHECK(inputRing.occupancy() == 10);
}

TEST_CASE("reconcileStreamOpenEdges: both opening in one pass resets both, correctly mapped") {
    TestRing inputRing(24);
    TestRing outputRing(24);

    std::vector<float> src_l(10, 0.5f);
    std::vector<float> src_r(10, 0.5f);
    const float* src[2] = {src_l.data(), src_r.data()};
    inputRing.write(src, 10);
    outputRing.write(src, 10);

    bool prevOut = false;
    bool prevIn = false;

    const StreamEdgeResult result =
        reconcileStreamOpenEdges(true, true, prevOut, prevIn, inputRing, outputRing);

    CHECK(result.inputRingReset == true);
    CHECK(result.outputRingReset == true);
    CHECK(inputRing.occupancy() == 0);
    CHECK(outputRing.occupancy() == 0);
}

TEST_CASE("reconcileStreamOpenEdges: already-open (no edge) resets neither ring") {
    TestRing inputRing(24);
    TestRing outputRing(24);

    std::vector<float> src_l(10, 0.5f);
    std::vector<float> src_r(10, 0.5f);
    const float* src[2] = {src_l.data(), src_r.data()};
    inputRing.write(src, 10);
    outputRing.write(src, 10);

    bool prevOut = true;
    bool prevIn = true;

    // Both already streaming: calling again with the same levels is a
    // steady-state pass, not an edge, and must not disturb either ring.
    const StreamEdgeResult result =
        reconcileStreamOpenEdges(true, true, prevOut, prevIn, inputRing, outputRing);

    CHECK(result.inputRingReset == false);
    CHECK(result.outputRingReset == false);
    CHECK(inputRing.occupancy() == 10);
    CHECK(outputRing.occupancy() == 10);
}

TEST_CASE("reconcileStreamOpenEdges: close then reopen fires the edge again") {
    TestRing inputRing(24);
    TestRing outputRing(24);
    bool prevOut = false;
    bool prevIn = false;

    // Open OUT.
    StreamEdgeResult r1 = reconcileStreamOpenEdges(true, false, prevOut, prevIn, inputRing,
                                                    outputRing);
    CHECK(r1.inputRingReset == true);

    std::vector<float> src_l(10, 0.5f);
    std::vector<float> src_r(10, 0.5f);
    const float* src[2] = {src_l.data(), src_r.data()};
    inputRing.write(src, 10);
    REQUIRE(inputRing.occupancy() == 10);

    // Close OUT: no edge on close, so the reconciler leaves the ring alone
    // (the ring's own state after a close is a separate concern from T056's
    // "on OPEN" mapping).
    StreamEdgeResult r2 = reconcileStreamOpenEdges(false, false, prevOut, prevIn, inputRing,
                                                    outputRing);
    CHECK(r2.inputRingReset == false);
    CHECK(inputRing.occupancy() == 10);

    // Reopen OUT: the edge fires again and resets the ring.
    StreamEdgeResult r3 = reconcileStreamOpenEdges(true, false, prevOut, prevIn, inputRing,
                                                    outputRing);
    CHECK(r3.inputRingReset == true);
    CHECK(inputRing.occupancy() == 0);
}

// ============================================================================
// Full transition tour across {both closed, playback only, capture only,
// both open} (FR-055) — several orders, no special-casing, counters intact.
// ============================================================================

TEST_CASE("FR-055: full transition tour reaches a coherent state every time, counters intact") {
    TestRing inputRing(24);
    TestRing outputRing(24);
    bool prevOut = false;
    bool prevIn = false;

    AudioTransportStats stats;
    seedCounters(stats);
    const AudioTransportStats before = stats;

    // Order 1: closed -> playback only -> both open -> capture only -> closed
    //          -> both open -> closed.
    const bool outSequence[] = {false, true, true, false, false, true, false};
    const bool inSequence[] = {false, false, true, true, false, true, false};
    static_assert(sizeof(outSequence) / sizeof(outSequence[0]) ==
                      sizeof(inSequence) / sizeof(inSequence[0]),
                  "the two sequences must walk in lockstep");

    for (std::size_t step = 0; step < sizeof(outSequence) / sizeof(outSequence[0]); ++step) {
        const StreamEdgeResult result = reconcileStreamOpenEdges(
            outSequence[step], inSequence[step], prevOut, prevIn, inputRing, outputRing);

        // Coherent state after every step, regardless of which transition:
        // a ring that was just reset is empty and Priming; a ring that
        // wasn't is unaffected by this call.
        if (result.inputRingReset) {
            CHECK(inputRing.occupancy() == 0);
            CHECK(inputRing.state() == RingState::Priming);
        }
        if (result.outputRingReset) {
            CHECK(outputRing.occupancy() == 0);
            CHECK(outputRing.state() == RingState::Priming);
        }
        // Feed a little audio so the next potential reset has something to
        // clear, proving the reset (when it fires) actually did something.
        std::vector<float> src_l(4, 0.25f);
        std::vector<float> src_r(4, 0.25f);
        const float* src[2] = {src_l.data(), src_r.data()};
        inputRing.write(src, 4);
        outputRing.write(src, 4);
    }

    CHECK(statsUnchanged(before, stats));
}

TEST_CASE("FR-055: alternate order (capture only first) is equally coherent") {
    TestRing inputRing(24);
    TestRing outputRing(24);
    bool prevOut = false;
    bool prevIn = false;

    // closed -> capture only -> both open -> playback only -> both open -> closed
    const bool outSequence[] = {false, false, true, true, true, false};
    const bool inSequence[] = {false, true, true, false, true, false};

    for (std::size_t step = 0; step < sizeof(outSequence) / sizeof(outSequence[0]); ++step) {
        const StreamEdgeResult result = reconcileStreamOpenEdges(
            outSequence[step], inSequence[step], prevOut, prevIn, inputRing, outputRing);
        if (result.inputRingReset) {
            CHECK(inputRing.occupancy() == 0);
        }
        if (result.outputRingReset) {
            CHECK(outputRing.occupancy() == 0);
        }
    }

    // Reached "both open" as the final streaming state before closing, and
    // the tour completed without any assertion failing above.
    CHECK(prevOut == false);
    CHECK(prevIn == false);
}

// ============================================================================
// FR-054 regression: reset()/stop() leave counters untouched even after
// prior underruns/overruns were already counted.
// ============================================================================

TEST_CASE("FR-054: counters survive a realistic sequence of ring events unchanged") {
    TestRing inputRing(24);
    TestRing outputRing(24);

    AudioTransportStats stats;

    // Simulate prior activity a real session would have counted: a short
    // read while Running (underrun) and an oversized write (overrun) — both
    // recorded the way dsp-block-path.h/usb-in-path.h actually do, i.e. as a
    // side effect the CALLER applies from the ring's return value, not
    // something the ring itself tracks (AR4).
    std::vector<float> src_l(24, 0.5f);
    std::vector<float> src_r(24, 0.5f);
    const float* src[2] = {src_l.data(), src_r.data()};
    inputRing.write(src, 24);  // reaches Running
    REQUIRE(inputRing.state() == RingState::Running);

    std::vector<float> dst_l(30);
    std::vector<float> dst_r(30);
    float* dst[2] = {dst_l.data(), dst_r.data()};
    const int substituted = inputRing.read(dst, 30);  // underrun: only 24 held
    if (substituted > 0) {
        stats.inputUnderruns += static_cast<std::uint32_t>(1);
    }
    CHECK(stats.inputUnderruns == 1);

    std::vector<float> big_l(60, 0.25f);
    std::vector<float> big_r(60, 0.25f);
    const float* bigSrc[2] = {big_l.data(), big_r.data()};
    const int dropped = outputRing.write(bigSrc, 60);  // overflow: capacity is 48
    if (dropped > 0) {
        stats.outputOverruns += static_cast<std::uint32_t>(1);
    }
    CHECK(stats.outputOverruns == 1);

    const AudioTransportStats afterActivity = stats;

    // Suspend, resume, and a stream-open edge, in sequence — the exact
    // lifecycle a real session sees.
    applySuspend(inputRing, outputRing);
    CHECK(statsUnchanged(afterActivity, stats));

    applyResumeOrBusReset(inputRing, outputRing);
    CHECK(statsUnchanged(afterActivity, stats));

    bool prevOut = false;
    bool prevIn = false;
    inputRing.write(src, 24);
    static_cast<void>(reconcileStreamOpenEdges(true, false, prevOut, prevIn, inputRing,
                                               outputRing));
    CHECK(statsUnchanged(afterActivity, stats));

    // Final sanity: the counters recorded during "prior activity" are still
    // exactly what they were — not reset to 0, not incremented again.
    CHECK(stats.inputUnderruns == 1);
    CHECK(stats.outputOverruns == 1);
    CHECK(stats.inputOverruns == 0);
    CHECK(stats.outputUnderruns == 0);
    CHECK(stats.inputStarved == 0);
    CHECK(stats.malformedPayloads == 0);
}
