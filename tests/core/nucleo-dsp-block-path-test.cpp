#include <doctest/doctest.h>

#include <cstdint>
#include <vector>

#include "support/allocation-sentinel.h"

#include "audio-ring.h"
#include "dsp-block-path.h"
#include "sample-format.h"
#include "transport-stats.h"

// The fixed 48-frame DSP block path (T033; FR-030a, FR-036a, FR-037, FR-032).
//
// This is the CONSUMER side of the ring — the counterpart to
// nucleo-usb-out-path-test.cpp's producer side. The one property the whole
// file exists to pin is FR-030a's decoupling: the USB packet cadence varies
// from 0 to 49 frames (FR-028) and NONE of that variation may reach the
// effect, which only ever sees exactly kBlockFrames (48).
//
// The path is driven with a FAKE effect rather than a real one, for two
// reasons. First, the real AppEffect only exists on the firmware targets
// (ACFX_EFFECT_TYPE is a compile definition — see
// nucleo-effect-prepare-test.cpp's note). Second, and more importantly, a
// fake with a trivially invertible transfer characteristic is what makes the
// FR-037 claims CHECKABLE: the two channels are scaled by DIFFERENT factors
// (x2 and x-4), so a de-interleaving slip, a channel swap, or a lost channel
// stride changes the numbers rather than merely the ordering, and the values
// the fake wrote are read back out of the OUTPUT ring, which is what proves
// process() ran in place on the buffers that were then published.
//
// Cases:
// DB1 — no block is drawn unless the input ring is Running (Priming and
//       Stopped both draw nothing even when 48+ frames are sitting there).
// DB2 — Running but short: fewer than 48 frames draws nothing, consumes
//       nothing, and counts NOTHING — a block that never ran is not an
//       underrun (FR-030d).
// DB3 — exactly 48 available: one block, non-interleaved per-channel float*,
//       processed in place, in frame order, published to the output ring.
// DB4 — more than 48 available: ONE block per pass, remainder stays in the
//       ring; a pass with 47 left draws nothing.
// DB5 — FR-030a: a scripted sequence of odd packet writes (0, 1, 49, 13, ...)
//       still yields ONLY 48-frame blocks. Packet size never reaches process().
// DB6 — output-ring overrun: the oldest output frames are dropped and
//       outputOverruns increments once for the block that overran (FR-032).
// DB7 — a ring that supplies fewer frames than it advertised: the shortfall is
//       recorded in inputUnderruns rather than going out silently (FR-032).
// DB8 — no heap allocation across a block pass (FR-030, D16).

using namespace acfx::nucleo;

namespace {

// Input frame `n` carries two DIFFERENT values, so a channel swap is visible
// as a value change and not merely as a reordering.
float leftIn(int n) noexcept { return static_cast<float>(n + 1); }
float rightIn(int n) noexcept { return static_cast<float>(n + 1000); }

// The fake effect's per-channel transfer characteristic. Different factors per
// channel, and both exactly representable in float for every index used here.
float leftOut(int n) noexcept { return leftIn(n) * 2.0f; }
float rightOut(int n) noexcept { return rightIn(n) * -4.0f; }

// A fake effect with a per-channel scale, recording exactly what process() was
// handed. `blockSizes` is the FR-030a evidence: every entry must be 48.
struct ScalingEffect {
    std::vector<int> blockSizes;
    int calls = 0;
    int lastNumChannels = 0;
    bool everSawAliasedChannels = false;

    void process(acfx::AudioBlock& io) noexcept {
        ++calls;
        blockSizes.push_back(io.numSamples());
        lastNumChannels = io.numChannels();
        if (io.numChannels() >= 2 && io.channel(0) == io.channel(1)) {
            everSawAliasedChannels = true;
        }

        // In place, per channel, non-interleaved.
        for (int frame = 0; frame < io.numSamples(); ++frame) {
            io.channel(0)[frame] *= 2.0f;
            io.channel(1)[frame] *= -4.0f;
        }
    }
};

// Push `frames` frames into `ring`, continuing the global frame numbering from
// `nextFrame` (which is advanced). Mirrors what the OUT path does with a USB
// payload, without involving the OUT path itself.
template <typename Ring>
void pushFrames(Ring& ring, int frames, int& nextFrame) {
    std::vector<float> left(static_cast<std::size_t>(frames > 0 ? frames : 1));
    std::vector<float> right(static_cast<std::size_t>(frames > 0 ? frames : 1));
    for (int frame = 0; frame < frames; ++frame) {
        left[static_cast<std::size_t>(frame)] = leftIn(nextFrame + frame);
        right[static_cast<std::size_t>(frame)] = rightIn(nextFrame + frame);
    }
    float* channels[kChannels] = {left.data(), right.data()};
    ring.write(channels, frames);
    nextFrame += frames;
}

// Drain `frames` frames out of a ring into caller-visible vectors.
template <typename Ring>
void drain(Ring& ring, int frames, std::vector<float>& left, std::vector<float>& right) {
    left.assign(static_cast<std::size_t>(frames), 0.0f);
    right.assign(static_cast<std::size_t>(frames), 0.0f);
    float* channels[kChannels] = {left.data(), right.data()};
    ring.read(channels, frames);
}

// Roomy enough that nothing in this file overflows by accident; overflow is
// exercised deliberately in DB6 with a deliberately small output ring.
using BigRing = AudioRing<512, kChannels>;

// A trivial clock source for these tests. DB1-DB8 exercise the block path's
// ring/effect/counter behaviour, not T036's timer — that coverage lives in
// nucleo-block-timer-test.cpp — so this only needs to satisfy runOneBlock()'s
// ClockSource duck type (`std::uint32_t now() noexcept`) with something that
// advances, the way a live counter would, rather than being a T036 fixture
// itself.
struct StepClock {
    std::uint32_t value = 0;
    std::uint32_t now() noexcept {
        value += 100;
        return value;
    }
};

// A stand-in for an input ring that ADVERTISES more than it delivers: it
// reports Running and an occupancy of one full block, but read() supplies only
// `supply_` real frames and zero-fills the rest, exactly as AudioRing::read()
// does on a genuine shortfall (AR2). The real ring cannot reach this state
// through the path's own gate — that is the point of DB7: the path must record
// what the ring REPORTS it supplied rather than trusting its own occupancy
// check, or FR-032's "no uncounted substitution" rests on an assumption.
class UnderSupplyingRing {
public:
    explicit UnderSupplyingRing(int supply) noexcept : supply_(supply) {}

    RingState state() const noexcept { return RingState::Running; }
    int occupancy() const noexcept { return kBlockFrames; }

    int read(float* const* dst, int frames) noexcept {
        const int available = (supply_ < frames) ? supply_ : frames;
        for (int frame = 0; frame < available; ++frame) {
            dst[0][frame] = leftIn(frame);
            dst[1][frame] = rightIn(frame);
        }
        for (int frame = available; frame < frames; ++frame) {
            for (int channel = 0; channel < kChannels; ++channel) {
                dst[channel][frame] = 0.0f;
            }
        }
        return frames - available;
    }

private:
    int supply_;
};

}  // namespace

// ============================================================================
// DB1 — the lifecycle gate (FR-030d, AR7)
// ============================================================================

TEST_CASE("DB1: no block is drawn while the input ring is Priming or Stopped") {
    ScalingEffect effect;
    AudioTransportStats stats;
    DspBlockPath path;
    BigRing output(0);
    StepClock clock;

    SUBCASE("Priming, with more than a block already buffered") {
        // Startup fill deliberately far above the 100 frames written, so the
        // ring is unambiguously Priming while holding twice a block.
        BigRing input(300);
        int nextFrame = 0;
        pushFrames(input, 100, nextFrame);
        REQUIRE(input.state() == RingState::Priming);
        REQUIRE(input.occupancy() == 100);

        const BlockPassResult pass = path.runOneBlock(input, output, effect, stats, clock);

        CHECK(pass.blockProcessed == false);
        CHECK(effect.calls == 0);
        CHECK(input.occupancy() == 100);  // nothing consumed
        CHECK(output.occupancy() == 0);
        CHECK(stats.blocksProcessed == 0u);
        CHECK(stats.inputUnderruns == 0u);
        CHECK(stats.outputOverruns == 0u);
    }

    SUBCASE("Stopped, with more than a block buffered after the stop") {
        BigRing input(0);
        int nextFrame = 0;
        input.stop();
        REQUIRE(input.state() == RingState::Stopped);
        // A write while Stopped fills the ring but does NOT promote it (AR7).
        pushFrames(input, 96, nextFrame);
        REQUIRE(input.state() == RingState::Stopped);
        REQUIRE(input.occupancy() == 96);

        const BlockPassResult pass = path.runOneBlock(input, output, effect, stats, clock);

        CHECK(pass.blockProcessed == false);
        CHECK(effect.calls == 0);
        CHECK(input.occupancy() == 96);
        CHECK(stats.blocksProcessed == 0u);
        CHECK(stats.inputUnderruns == 0u);
    }
}

// ============================================================================
// DB2 — Running but short of a block (FR-030d: not an underrun)
// ============================================================================

TEST_CASE("DB2: Running with fewer than 48 frames draws nothing and counts nothing") {
    ScalingEffect effect;
    AudioTransportStats stats;
    DspBlockPath path;
    BigRing input(1);  // promoted to Running by the first write
    BigRing output(0);
    StepClock clock;

    int nextFrame = 0;
    pushFrames(input, 47, nextFrame);
    REQUIRE(input.state() == RingState::Running);
    REQUIRE(input.occupancy() == 47);

    const BlockPassResult pass = path.runOneBlock(input, output, effect, stats, clock);

    CHECK(pass.blockProcessed == false);
    CHECK(effect.calls == 0);
    // The 47 frames are still there, unconsumed and unsubstituted: the block
    // simply has not become available yet.
    CHECK(input.occupancy() == 47);
    CHECK(output.occupancy() == 0);
    CHECK(stats.blocksProcessed == 0u);
    CHECK(stats.inputUnderruns == 0u);

    // One more frame is all it takes.
    pushFrames(input, 1, nextFrame);
    const BlockPassResult second = path.runOneBlock(input, output, effect, stats, clock);
    CHECK(second.blockProcessed == true);
    CHECK(effect.calls == 1);
    CHECK(stats.blocksProcessed == 1u);
}

// ============================================================================
// DB3 — exactly one block: FR-036a size, FR-037 presentation (in place,
//       non-interleaved, per channel), published to the output ring
// ============================================================================

TEST_CASE("DB3: exactly 48 frames yields one in-place 48-frame non-interleaved block") {
    ScalingEffect effect;
    AudioTransportStats stats;
    DspBlockPath path;
    BigRing input(1);
    BigRing output(0);
    StepClock clock;

    int nextFrame = 0;
    pushFrames(input, kBlockFrames, nextFrame);
    REQUIRE(input.state() == RingState::Running);

    const BlockPassResult pass = path.runOneBlock(input, output, effect, stats, clock);

    CHECK(pass.blockProcessed == true);
    CHECK(pass.framesSubstituted == 0);
    CHECK(pass.framesDropped == 0);

    // FR-036a / FR-037: exactly 48 frames, 2 channels, distinct channel
    // pointers (non-interleaved, not two views of one interleaved buffer).
    REQUIRE(effect.calls == 1);
    CHECK(effect.blockSizes.at(0) == kBlockFrames);
    CHECK(effect.lastNumChannels == kChannels);
    CHECK(effect.everSawAliasedChannels == false);

    CHECK(input.occupancy() == 0);
    CHECK(output.occupancy() == kBlockFrames);
    CHECK(stats.blocksProcessed == 1u);
    CHECK(stats.inputUnderruns == 0u);
    CHECK(stats.outputOverruns == 0u);

    // What the output ring holds is what the effect WROTE — proving the block
    // was processed in place and the mutated buffers were published, in frame
    // order, with the two channels kept apart.
    std::vector<float> left;
    std::vector<float> right;
    drain(output, kBlockFrames, left, right);
    for (int frame = 0; frame < kBlockFrames; ++frame) {
        CHECK(left[static_cast<std::size_t>(frame)] == doctest::Approx(leftOut(frame)));
        CHECK(right[static_cast<std::size_t>(frame)] == doctest::Approx(rightOut(frame)));
    }
}

// ============================================================================
// DB4 — bounded work: ONE block per pass, remainder retained
// ============================================================================

TEST_CASE("DB4: one block per pass; the surplus stays in the ring for later passes") {
    ScalingEffect effect;
    AudioTransportStats stats;
    DspBlockPath path;
    BigRing input(1);
    BigRing output(0);
    StepClock clock;

    int nextFrame = 0;
    pushFrames(input, 130, nextFrame);  // two whole blocks plus 34 frames

    CHECK(path.runOneBlock(input, output, effect, stats, clock).blockProcessed == true);
    CHECK(input.occupancy() == 130 - kBlockFrames);
    CHECK(effect.calls == 1);

    CHECK(path.runOneBlock(input, output, effect, stats, clock).blockProcessed == true);
    CHECK(input.occupancy() == 130 - 2 * kBlockFrames);
    CHECK(effect.calls == 2);

    // 34 frames left: not a block, so the third pass draws nothing at all.
    const BlockPassResult third = path.runOneBlock(input, output, effect, stats, clock);
    CHECK(third.blockProcessed == false);
    CHECK(effect.calls == 2);
    CHECK(input.occupancy() == 34);

    CHECK(stats.blocksProcessed == 2u);
    CHECK(output.occupancy() == 2 * kBlockFrames);

    // The two blocks are contiguous across the pass boundary: frame 47 of the
    // first block and frame 48 (the first of the second) are adjacent input
    // frames, so nothing was dropped or duplicated at the seam.
    std::vector<float> left;
    std::vector<float> right;
    drain(output, 2 * kBlockFrames, left, right);
    for (int frame = 0; frame < 2 * kBlockFrames; ++frame) {
        CHECK(left[static_cast<std::size_t>(frame)] == doctest::Approx(leftOut(frame)));
        CHECK(right[static_cast<std::size_t>(frame)] == doctest::Approx(rightOut(frame)));
    }
}

// ============================================================================
// DB5 — FR-030a: packet size does NOT propagate into block size
// ============================================================================

TEST_CASE("DB5: odd packet sizes are absorbed by the ring; the effect only ever sees 48") {
    ScalingEffect effect;
    AudioTransportStats stats;
    DspBlockPath path;
    BigRing input(1);
    BigRing output(0);
    StepClock clock;

    // Every legal packet size from FR-028's 0..49 range, deliberately including
    // the extremes and nothing resembling 48-frame framing.
    const int packets[] = {0, 1, 49, 13, 49, 2, 47, 49, 0, 33, 49, 7, 49, 21, 49};

    int nextFrame = 0;
    int framesWritten = 0;
    for (const int packetFrames : packets) {
        pushFrames(input, packetFrames, nextFrame);
        framesWritten += packetFrames;
        // The service loop runs a pass after every packet, exactly as
        // nucleo-main.cpp's loop does.
        path.runOneBlock(input, output, effect, stats, clock);
    }

    // THE FR-030a ASSERTION: nothing in `packets` reached process().
    REQUIRE(effect.calls > 0);
    for (const int size : effect.blockSizes) {
        CHECK(size == kBlockFrames);
    }
    CHECK(static_cast<std::uint32_t>(effect.calls) == stats.blocksProcessed);

    // No frame was invented or lost: what the ring still holds plus what the
    // blocks carried out equals what was written in.
    const int framesBlocked = effect.calls * kBlockFrames;
    CHECK(framesBlocked + input.occupancy() == framesWritten);
    CHECK(stats.inputUnderruns == 0u);
    CHECK(stats.outputOverruns == 0u);

    // And the blocked audio is still the input sequence in order, unbroken by
    // the packet boundaries it was assembled across.
    std::vector<float> left;
    std::vector<float> right;
    drain(output, framesBlocked, left, right);
    for (int frame = 0; frame < framesBlocked; ++frame) {
        CHECK(left[static_cast<std::size_t>(frame)] == doctest::Approx(leftOut(frame)));
        CHECK(right[static_cast<std::size_t>(frame)] == doctest::Approx(rightOut(frame)));
    }
}

// ============================================================================
// DB6 — output-ring overrun is counted, never silent (FR-031, FR-032)
// ============================================================================

TEST_CASE("DB6: an output-ring overrun drops the oldest frames and counts outputOverruns") {
    ScalingEffect effect;
    AudioTransportStats stats;
    DspBlockPath path;
    BigRing input(1);
    StepClock clock;

    // 64 frames: the first block fits, the second overruns by exactly 32.
    AudioRing<64, kChannels> output(0);

    int nextFrame = 0;
    pushFrames(input, 2 * kBlockFrames, nextFrame);

    const BlockPassResult first = path.runOneBlock(input, output, effect, stats, clock);
    CHECK(first.blockProcessed == true);
    CHECK(first.framesDropped == 0);
    CHECK(stats.outputOverruns == 0u);

    const BlockPassResult second = path.runOneBlock(input, output, effect, stats, clock);
    CHECK(second.blockProcessed == true);
    CHECK(second.framesDropped == 2 * kBlockFrames - 64);  // 32
    // An EVENT count: one increment for the one block that overran, matching
    // the OUT path's inputOverruns convention.
    CHECK(stats.outputOverruns == 1u);
    CHECK(stats.blocksProcessed == 2u);
    CHECK(output.occupancy() == 64);

    // AR3: the OLDEST output frames went, the newest survived — the ring holds
    // the last 64 processed frames, i.e. input frames 32..95.
    std::vector<float> left;
    std::vector<float> right;
    drain(output, 64, left, right);
    for (int frame = 0; frame < 64; ++frame) {
        const int sourceFrame = frame + 32;
        CHECK(left[static_cast<std::size_t>(frame)] == doctest::Approx(leftOut(sourceFrame)));
        CHECK(right[static_cast<std::size_t>(frame)] == doctest::Approx(rightOut(sourceFrame)));
    }
}

// ============================================================================
// DB7 — a short ring read is recorded, not assumed away (FR-031, FR-032)
// ============================================================================

TEST_CASE("DB7: a ring supplying fewer frames than it advertised counts inputUnderruns") {
    ScalingEffect effect;
    AudioTransportStats stats;
    DspBlockPath path;
    BigRing output(0);
    UnderSupplyingRing input(20);  // claims a block, delivers 20 frames
    StepClock clock;

    const BlockPassResult pass = path.runOneBlock(input, output, effect, stats, clock);

    // The read has already consumed the ring's frames by the time the shortfall
    // is known, so the block runs — discarding it would be a second, uncounted
    // loss on top of the first.
    CHECK(pass.blockProcessed == true);
    CHECK(pass.framesSubstituted == kBlockFrames - 20);
    CHECK(stats.inputUnderruns == 1u);  // event count, one per block
    CHECK(stats.blocksProcessed == 1u);
    CHECK(effect.calls == 1);
    CHECK(effect.blockSizes.at(0) == kBlockFrames);

    // The substituted tail is silence carried through the effect, not stale
    // audio: 0 * 2 and 0 * -4 are both 0.
    std::vector<float> left;
    std::vector<float> right;
    drain(output, kBlockFrames, left, right);
    for (int frame = 20; frame < kBlockFrames; ++frame) {
        CHECK(left[static_cast<std::size_t>(frame)] == doctest::Approx(0.0f));
        CHECK(right[static_cast<std::size_t>(frame)] == doctest::Approx(0.0f));
    }
}

// ============================================================================
// DB8 — the audio path allocates nothing (FR-030, D16)
// ============================================================================

TEST_CASE("DB8: a block pass performs no heap allocation") {
    // The fake effect's own recording vector would allocate, so it is grown to
    // its final capacity BEFORE the measured region — what is under test here
    // is the block path, not the test double.
    ScalingEffect effect;
    effect.blockSizes.reserve(8);
    AudioTransportStats stats;
    DspBlockPath path;
    BigRing input(1);
    BigRing output(0);
    StepClock clock;

    int nextFrame = 0;
    pushFrames(input, 4 * kBlockFrames, nextFrame);

    acfx::test::AllocationSentinel::reset();
    for (int pass = 0; pass < 4; ++pass) {
        path.runOneBlock(input, output, effect, stats, clock);
    }
    const std::size_t allocations = acfx::test::AllocationSentinel::allocations();

    CHECK(allocations == 0u);
    CHECK(stats.blocksProcessed == 4u);
}
