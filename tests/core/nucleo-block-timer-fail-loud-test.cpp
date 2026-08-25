#include <doctest/doctest.h>

#include <cstdint>

#include "audio-ring.h"
#include "block-timer.h"
#include "dsp-block-path.h"
#include "sample-format.h"
#include "transport-stats.h"

// The fail-loud guard on a DEAD DWT timing source (T037; FR-034b, I-TS4,
// research R6).
//
// THE ANTI-PATTERN THIS FILE EXISTS TO RULE OUT. It would be trivial to write
// a "stuck at zero" detector that never actually fires — an
// `if (false) { markDead(); }` shape that compiles, looks correct on
// inspection, and passes every test because no test ever puts the detector
// in the one state where it must trip. FL1 below is the POSITIVE CONTROL:
// it feeds VerifyClockSourceLive() a clock that returns the exact reading a
// genuinely broken DWT gives (permanently 0) and asserts the fail-loud path
// actually fires -- not merely that it compiles. FL4's mutation evidence (see
// the report this task returns) disables the guard and shows FL1 then FAILS,
// which is the proof this positive control is not vacuous.
//
// Cases:
// FL1 — POSITIVE CONTROL: a clock stuck at zero is detected as dead.
// FL2 — NEGATIVE CONTROL: a live, advancing clock is NOT flagged dead.
// FL3 — once dead, worstBlockMicros is pinned at the sentinel and stays
//       there even if RecordBlockTiming() is called again — the exact
//       "worstBlockMicros = 0" failure FR-034b names cannot creep back in
//       through a later call.
// FL4 — the real block path (runOneBlock(), not block-timer.h's functions in
//       isolation): a dead clock still lets blocks run and blocksProcessed
//       still climbs (FR-034c: it is a common denominator, not gated on
//       timer health), but worstBlockMicros never leaves the sentinel.

using namespace acfx::nucleo;

namespace {

// THE positive control's clock: every call returns the exact reading a DWT
// that never started counting gives, on real hardware, forever.
struct StuckAtZeroClock {
    std::uint32_t now() noexcept { return 0u; }
};

// THE negative control's clock: advances by a fixed step every call, exactly
// as a genuinely free-running CYCCNT would between two reads.
struct AdvancingClock {
    std::uint32_t value = 0;
    std::uint32_t now() noexcept {
        value += 97;
        return value;
    }
};

struct NoopEffect {
    int calls = 0;
    void process(acfx::AudioBlock&) noexcept { ++calls; }
};

}  // namespace

// ============================================================================
// FL1 — POSITIVE CONTROL: a stuck-at-zero clock trips the fail-loud path
// ============================================================================

TEST_CASE("FL1 (POSITIVE CONTROL): a clock stuck at zero is detected as dead") {
    StuckAtZeroClock clock;
    CHECK(VerifyClockSourceLive(clock) == false);

    AudioTransportStats stats;
    // Deliberately NOT pre-set: this is what a fresh record looks like before
    // InitializeBlockTimer() runs, mirroring startup.
    REQUIRE(stats.timingSourceLive == false);

    InitializeBlockTimer(clock, stats);

    // THE FR-034b assertion: the dead source is flagged, AND
    // worstBlockMicros is NOT left at (or set to) a confusable 0 -- it reads
    // the distinct sentinel that can never be mistaken for a measurement.
    CHECK(stats.timingSourceLive == false);
    CHECK(stats.worstBlockMicros == kBlockTimerDeadSentinel);
    CHECK(stats.worstBlockMicros != 0u);
}

// ============================================================================
// FL2 — NEGATIVE CONTROL: a live clock is not flagged dead
// ============================================================================

TEST_CASE("FL2 (NEGATIVE CONTROL): a live, advancing clock is not flagged dead") {
    AdvancingClock clock;
    CHECK(VerifyClockSourceLive(clock) == true);

    AudioTransportStats stats;
    InitializeBlockTimer(clock, stats);

    CHECK(stats.timingSourceLive == true);
    // A live clock's verification must not disturb worstBlockMicros -- no
    // block has run yet, so it stays at the ordinary zero-blocks-processed
    // default, not the dead-timer sentinel.
    CHECK(stats.worstBlockMicros == 0u);
}

// ============================================================================
// FL3 — the sentinel survives later RecordBlockTiming() calls, by
// CONSTRUCTION (block-timer.h's kMaxPossibleBlockMicros static_assert), not
// by a conditional that has to remember to check
// ============================================================================

TEST_CASE("FL3: worstBlockMicros stays pinned at the sentinel across later "
          "per-block calls, including ones fed the LARGEST possible delta") {
    AudioTransportStats stats;
    StuckAtZeroClock clock;
    InitializeBlockTimer(clock, stats);
    REQUIRE(stats.worstBlockMicros == kBlockTimerDeadSentinel);

    // The stuck-at-zero case: every block reads a 0-cycle delta.
    RecordBlockTiming(0u, 0u, stats);
    CHECK(stats.worstBlockMicros == kBlockTimerDeadSentinel);

    // The adversarial case: feed the LARGEST cycle delta a 32-bit counter can
    // ever produce (start=1, end=0, which is 0xFFFFFFFF cycles under unsigned
    // subtraction). block-timer.h's kMaxPossibleBlockMicros static_assert is
    // what guarantees even this cannot reach the sentinel -- proving the
    // protection holds for every representable delta, not merely for the
    // specific 0-delta a stuck-at-zero clock happens to produce.
    RecordBlockTiming(1u, 0u, stats);
    CHECK(stats.worstBlockMicros == kBlockTimerDeadSentinel);
    CHECK(stats.timingSourceLive == false);
}

// ============================================================================
// FL4 — the real block path: a dead timer never corrupts worstBlockMicros,
// and does not stop blocksProcessed from being the FR-034c denominator
// ============================================================================

TEST_CASE("FL4: runOneBlock with a dead clock leaves worstBlockMicros at the "
          "sentinel while blocksProcessed still climbs") {
    NoopEffect effect;
    AudioTransportStats stats;
    StuckAtZeroClock clock;
    InitializeBlockTimer(clock, stats);
    REQUIRE(stats.timingSourceLive == false);
    REQUIRE(stats.worstBlockMicros == kBlockTimerDeadSentinel);

    DspBlockPath path;
    AudioRing<64, kChannels> input(1);
    AudioRing<64, kChannels> output(0);
    float left[kBlockFrames] = {};
    float right[kBlockFrames] = {};
    float* channels[kChannels] = {left, right};
    input.write(channels, kBlockFrames);
    REQUIRE(input.state() == RingState::Running);

    const BlockPassResult pass = path.runOneBlock(input, output, effect, stats, clock);

    CHECK(pass.blockProcessed == true);
    CHECK(effect.calls == 1);
    // FR-034c: blocksProcessed is a common elapsed-work denominator,
    // independent of the timer's health -- a dead timer must not also blind
    // every OTHER rate this record supports.
    CHECK(stats.blocksProcessed == 1u);
    // Exercised end to end through the real path: the block's (0-cycle,
    // stuck) timing reading never overwrites the sentinel.
    CHECK(stats.worstBlockMicros == kBlockTimerDeadSentinel);
}
