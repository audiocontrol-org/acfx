#include <doctest/doctest.h>

#include <cstdint>
#include <vector>

#include "block-timer.h"
#include "dsp-block-path.h"
#include "sample-format.h"
#include "transport-stats.h"

// The DWT CYCCNT block timer (T036; FR-034, research R6).
//
// support/block-timer.h holds the platform-independent conversion and
// max-tracking LOGIC, driven through a duck-typed ClockSource
// (`std::uint32_t now() noexcept`) so the host can script exactly the cycle
// sequence each case needs, without ever touching DWT->CYCCNT itself — the
// real register lives behind adapters/nucleo/dsp-block-service.h's
// DwtCycleClock, which is firmware-only and untestable here (T038 owns
// hardware verification).
//
// Cases:
// BT1 — cycles -> microseconds at 168 MHz: a known cycle delta yields a known
//       microsecond count, including the documented truncation of a
//       sub-168-cycle span to 0.
// BT2 — worstBlockMicros tracks the MAXIMUM across several blocks, never the
//       last value and never an average.
// BT3 — the counter wrapping across 2^32 still yields the correct positive
//       delta: unsigned subtraction, no special case (R6).
// BT4 — a full runOneBlock() pass times ONLY the process() call, and
//       blocksProcessed / worstBlockMicros both update from that ONE pass —
//       proving the timer is wired into the real block path, not just
//       block-timer.h's free functions in isolation.

using namespace acfx::nucleo;

namespace {

// A clock driven by an explicit script: each now() call returns the next
// entry, in order. Lets a test state the exact cycle readings runOneBlock()
// (or block-timer.h's functions directly) will see, including a deliberate
// wrap across 2^32.
class ScriptedClock {
public:
    explicit ScriptedClock(std::vector<std::uint32_t> script) : script_(std::move(script)) {}

    std::uint32_t now() noexcept {
        const std::uint32_t value = script_.at(index_);
        if (index_ + 1 < script_.size()) {
            ++index_;
        }
        return value;
    }

private:
    std::vector<std::uint32_t> script_;
    std::size_t index_ = 0;
};

// A fake effect whose process() call does nothing observable to the audio
// (BT4 is about the TIMER, not the DSP), so the ScriptedClock's two readings
// around it are the only thing under test.
struct NoopEffect {
    int calls = 0;
    void process(acfx::AudioBlock&) noexcept { ++calls; }
};

}  // namespace

// ============================================================================
// BT1 — cycles -> microseconds at 168 MHz
// ============================================================================

TEST_CASE("BT1: cycles convert to microseconds at 168 cycles per microsecond") {
    // 168 cycles is exactly 1 microsecond at 168 MHz (research R6).
    CHECK(CyclesToMicros(168u) == 1u);
    CHECK(CyclesToMicros(168u * 5u) == 5u);
    CHECK(CyclesToMicros(168u * 1000u) == 1000u);

    // A span shorter than one microsecond truncates to 0 -- documented in
    // block-timer.h's file header as the CORRECT answer for a genuinely
    // trivial span, distinct from the "dead timer" 0 T037 guards against.
    CHECK(CyclesToMicros(167u) == 0u);
    CHECK(CyclesToMicros(1u) == 0u);
    CHECK(CyclesToMicros(0u) == 0u);
}

// ============================================================================
// BT2 — worstBlockMicros is a MAXIMUM, never the last value or an average
// ============================================================================

TEST_CASE("BT2: worstBlockMicros tracks the maximum across several blocks") {
    AudioTransportStats stats;

    // Three blocks: 10us, 50us, 20us in that order. The maximum (50) must
    // survive being followed by a SMALLER block -- a "last value wins"
    // mutation would report 20 here instead.
    RecordBlockTiming(0u, 168u * 10u, stats);
    CHECK(stats.worstBlockMicros == 10u);

    RecordBlockTiming(0u, 168u * 50u, stats);
    CHECK(stats.worstBlockMicros == 50u);

    RecordBlockTiming(0u, 168u * 20u, stats);
    CHECK(stats.worstBlockMicros == 50u);  // unchanged: 20 < the standing max

    // An average of {10, 50, 20} is 80/3 ~= 26.67 -- nowhere near 50, so this
    // also rules out an averaging mutation, not just a last-value one.
    CHECK(stats.worstBlockMicros != 26u);
    CHECK(stats.worstBlockMicros != 27u);
}

// ============================================================================
// BT3 — CYCCNT wrapping across 2^32 still yields the correct positive delta
// ============================================================================

TEST_CASE("BT3: a wrap across 2^32 still yields the correct positive microsecond delta") {
    AudioTransportStats stats;

    // start is 100 cycles before the counter wraps; end is 50 cycles after it
    // wraps back through 0. The true elapsed span is 100 + 50 = 150 cycles,
    // which unsigned subtraction gives directly: 50u - (0xFFFFFFFFu - 99u).
    const std::uint32_t start = 0xFFFFFFFFu - 99u;  // 100 cycles before wrap
    const std::uint32_t end = 50u;                  // 50 cycles after wrap
    RecordBlockTiming(start, end, stats);

    // 150 cycles at 168 cycles/us truncates to 0us -- still exercises the
    // wrap arithmetic; BT3b below uses a wrap large enough to read a nonzero
    // microsecond count so the conversion itself is also checked post-wrap.
    CHECK(stats.worstBlockMicros == CyclesToMicros(150u));
}

TEST_CASE("BT3b: a wrap large enough to read a nonzero microsecond count") {
    AudioTransportStats stats;

    // 168 * 1000 = 168000 cycles total span, split across the wrap boundary:
    // 100000 cycles before wrapping, 68000 after.
    const std::uint32_t start = 0xFFFFFFFFu - 99999u;
    const std::uint32_t end = 68000u;
    RecordBlockTiming(start, end, stats);

    CHECK(stats.worstBlockMicros == 1000u);
}

// ============================================================================
// BT4 — the real block path times ONLY process(), and blocksProcessed /
// worstBlockMicros both update from the same pass
// ============================================================================

TEST_CASE("BT4: runOneBlock times only the process() call and updates both counters") {
    NoopEffect effect;
    AudioTransportStats stats;
    DspBlockPath path;
    AudioRing<64, kChannels> input(1);
    AudioRing<64, kChannels> output(0);

    std::vector<float> left(kBlockFrames, 0.0f);
    std::vector<float> right(kBlockFrames, 0.0f);
    float* channels[kChannels] = {left.data(), right.data()};
    input.write(channels, kBlockFrames);
    REQUIRE(input.state() == RingState::Running);

    // Two readings: whatever runOneBlock() reads immediately before and
    // immediately after process(). A third, later value is scripted but MUST
    // NOT be consumed -- proving the timer brackets exactly that one call
    // and nothing after it (an extra clock.now() call downstream, e.g. around
    // the ring publish, would consume it and desync every later assertion).
    ScriptedClock clock({1000u, 1000u + 168u * 7u, 999999999u});

    const BlockPassResult pass = path.runOneBlock(input, output, effect, stats, clock);

    CHECK(pass.blockProcessed == true);
    CHECK(effect.calls == 1);
    CHECK(stats.blocksProcessed == 1u);      // T033's rate denominator
    CHECK(stats.worstBlockMicros == 7u);     // 168*7 cycles -> 7 microseconds
}
