// Integration check for T056's stream-open reconciliation against the real
// ring + DSP pipeline (US10 / FR-055 / AR9). The unit test
// nucleo-lifecycle-policy-test.cpp proves reconcileStreamOpenEdges() resets the
// right ring on the right edge in isolation; THIS file drives the whole polled
// service-loop pipeline — reconcile -> fill input (OUT) -> runOneBlock (DSP) ->
// drain output (IN) — across the first-open sequence, because a reset that is
// correct in isolation could still starve the FIRST stream if the pipeline
// cannot recover from it. That failure mode (first stream silent, later streams
// fine) is exactly what a hardware observation suggested, and it is decidable
// here without hardware.
//
// The question each case answers: after the stream-open resets fire, does audio
// eventually flow to the output ring and STAY flowing (sustained non-silent,
// correctly transformed frames)? "Recovers" = the logic is sound and any
// hardware silence is environmental; "never primes" = a real reconcile bug.

#include <doctest/doctest.h>

#include <cstdint>
#include <vector>

#include "audio-ring.h"
#include "dsp-block-path.h"
#include "lifecycle-policy.h"
#include "transport-stats.h"

using namespace acfx::nucleo;

namespace {

constexpr int kChannels = 2;

// A per-channel scaling effect (2x left, -4x right) so a processed block is
// distinguishable from silence and from a passthrough.
struct ScalingEffect {
    void process(acfx::AudioBlock& io) noexcept {
        for (int f = 0; f < io.numSamples(); ++f) {
            io.channel(0)[f] *= 2.0f;
            io.channel(1)[f] *= -4.0f;
        }
    }
};

struct StepClock {
    std::uint32_t value = 0;
    std::uint32_t now() noexcept { value += 100; return value; }
};

// One packet's worth of non-zero audio pushed to the input ring, mirroring what
// ServiceUsbAudioOut does with a USB payload (without the OUT path itself).
template <typename Ring>
void pushPacket(Ring& ring, int frames, float value) {
    std::vector<float> l(static_cast<std::size_t>(frames), value);
    std::vector<float> r(static_cast<std::size_t>(frames), value);
    float* ch[kChannels] = {l.data(), r.data()};
    ring.write(ch, frames);
}

// Drain up to one block from the output ring, but ONLY when Running — mirroring
// the IN path's AR7 gate. Returns the peak magnitude drained (0 if not Running
// or nothing real came out).
template <typename Ring>
float drainIfRunning(Ring& ring) {
    if (ring.state() != RingState::Running) return 0.0f;
    std::vector<float> l(static_cast<std::size_t>(kBlockFrames), 0.0f);
    std::vector<float> r(static_cast<std::size_t>(kBlockFrames), 0.0f);
    float* ch[kChannels] = {l.data(), r.data()};
    ring.read(ch, kBlockFrames);
    float peak = 0.0f;
    for (int f = 0; f < kBlockFrames; ++f) {
        peak = peak > (l[f] < 0 ? -l[f] : l[f]) ? peak : (l[f] < 0 ? -l[f] : l[f]);
    }
    return peak;
}

using TestRing = AudioRing<512, kChannels>;
constexpr int kFill = 2 * kBlockFrames;  // startup fill: two blocks

// Run one faithful service-loop pass and return the output peak drained this
// pass. `feedInput` simulates the OUT stream delivering a packet; the caller
// controls it so a closed OUT stream delivers nothing.
struct Pipeline {
    TestRing input{kFill};
    TestRing output{kFill};
    DspBlockPath path;
    ScalingEffect effect;
    AudioTransportStats stats;
    StepClock clock;
    bool prevOut = false;
    bool prevIn = false;

    float pass(bool outStreaming, bool inStreaming, bool feedInput) {
        reconcileStreamOpenEdges(outStreaming, inStreaming, prevOut, prevIn, input, output);
        if (feedInput && outStreaming) pushPacket(input, kBlockFrames, 0.25f);
        static_cast<void>(path.runOneBlock(input, output, effect, stats, clock));
        return inStreaming ? drainIfRunning(output) : 0.0f;
    }
};

}  // namespace

// LI1: the ordinary first-open sequence — OUT opens, then IN opens a few passes
// later (after the output ring has already primed and been reset by IN-open) —
// must reach sustained non-silent output.
TEST_CASE("LI1: first duplex open (OUT then IN) reaches sustained output") {
    Pipeline p;
    float lastPeaks = 0.0f;
    // OUT open first for 5 passes (input primes, DSP fills output).
    for (int i = 0; i < 5; ++i) p.pass(/*out*/true, /*in*/false, /*feed*/true);
    // IN opens (resets the output ring) and both stream for many passes.
    float peak = 0.0f;
    for (int i = 0; i < 40; ++i) peak = p.pass(true, true, true);
    // By the end audio must be flowing and correctly transformed (left = 2x0.25).
    CHECK(peak == doctest::Approx(0.5f));
}

// LI2: IN opens FIRST, then OUT — the reverse order a host may choose.
TEST_CASE("LI2: first duplex open (IN then OUT) reaches sustained output") {
    Pipeline p;
    for (int i = 0; i < 3; ++i) p.pass(/*out*/false, /*in*/true, /*feed*/false);
    float peak = 0.0f;
    for (int i = 0; i < 40; ++i) peak = p.pass(true, true, true);
    CHECK(peak == doctest::Approx(0.5f));
}

// LI3: THE HYPOTHESIS — a host that toggles the OUT alt-setting repeatedly
// during setup (open/close/open/close) makes reconcile reset the input ring on
// every open edge. If each reset wipes the partial fill before it reaches the
// startup threshold, the first stream could be starved for as long as the host
// keeps probing. This asserts that ONCE THE TOGGLING STOPS the pipeline still
// reaches sustained output (i.e. the reset is not a permanent trap) AND checks
// how long recovery takes.
TEST_CASE("LI3: repeated OUT open/close during setup does not permanently starve") {
    Pipeline p;
    // 8 rounds of: open+feed one packet, then close (no feed). Each reopen edge
    // resets the input ring, discarding the single packet before it can prime.
    for (int round = 0; round < 8; ++round) {
        p.pass(/*out*/true, /*in*/false, /*feed*/true);   // open: reset, then 1 packet
        p.pass(/*out*/false, /*in*/false, /*feed*/false);  // close: no reset, no feed
    }
    // During toggling the input ring never accumulated kFill, so no block ran.
    // Now the host settles on OUT+IN open and streams steadily.
    float peak = 0.0f;
    for (int i = 0; i < 40; ++i) peak = p.pass(true, true, true);
    CHECK(peak == doctest::Approx(0.5f));
}

// LI4: steady duplex with NO further edges must keep flowing indefinitely — the
// reconcile must not fire on a non-edge (that would re-reset every pass and
// permanently starve, which IS the shape of the observed hardware silence).
TEST_CASE("LI4: steady duplex (no edges) never re-resets and keeps flowing") {
    Pipeline p;
    // Prime both directions.
    for (int i = 0; i < 40; ++i) p.pass(true, true, true);
    // Now run 200 more steady passes; output must remain non-silent every pass.
    int silentPasses = 0;
    for (int i = 0; i < 200; ++i) {
        const float peak = p.pass(true, true, true);
        if (peak < 0.4f) ++silentPasses;
    }
    CHECK(silentPasses == 0);
}
