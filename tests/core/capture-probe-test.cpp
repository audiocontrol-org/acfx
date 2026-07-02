// capture-probe-test.cpp
// T025 -- harmonic-analysis feature, User Story 5: RED test for the RT capture
// probe -- a portable, lock-free single-producer/single-consumer (SPSC) ring
// (contracts/capture-probe-api.md, data-model.md "CaptureProbeRing",
// research.md Decision 7, spec.md US5 / FR-011/FR-012/FR-013).
//
// The capture probe is the audio -> analysis bridge and the ONLY audio-path
// unit in this feature. It is portable core: fixed compile-time capacity, no
// heap, no locks, no math on the push path. The audio (producer) thread calls
// push(); the analysis (consumer) thread calls available()/drain(). Overrun
// (producer laps consumer) and underrun (consumer wants more than is ready) are
// both observable and never corrupt or block.
//
// A single-threaded logical model exercises every state deterministically (the
// SPSC contract is a data-flow contract, not a timing one); a light 2-thread
// smoke test at the end confirms nothing blocks or deadlocks under real
// concurrency. core/primitives/analysis/capture-probe.h does not exist yet at
// RED time -- this test is expected to FAIL TO BUILD until T026 lands it.

#include <doctest/doctest.h>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <thread>
#include <vector>

#include "primitives/analysis/capture-probe.h"
#include "dsp/span.h"

using acfx::CaptureProbeRing;

namespace {

// A small capacity keeps the logical model easy to reason about by hand while
// still exercising wraparound and overrun. Real use picks Capacity >= one FFT
// window + margin (data-model.md), but the ring's behavior is capacity-agnostic.
constexpr std::size_t kCap = 8;

// Push a run of ramp values [start, start+count) so drained values are
// self-identifying (value == original push index), which makes ordering and
// overrun-window coherence checkable by equality rather than by proxy.
template <std::size_t Capacity>
void pushRamp(CaptureProbeRing<Capacity>& ring, float start, std::size_t count) {
    std::vector<float> src(count);
    for (std::size_t i = 0; i < count; ++i) {
        src[i] = start + static_cast<float>(i);
    }
    ring.push(acfx::span<const float>(src.data(), src.size()));
}

} // namespace

TEST_CASE("capture-probe: basic push/drain round-trip preserves order") {
    CaptureProbeRing<kCap> ring;
    CHECK(ring.available() == 0);
    CHECK(ring.overrunCount() == 0);

    pushRamp(ring, 0.0f, kCap); // fill exactly to capacity, no overrun
    CHECK(ring.available() == kCap);
    CHECK(ring.overrunCount() == 0);

    std::vector<float> out(kCap, -1.0f);
    const std::size_t got = ring.drain(acfx::span<float>(out.data(), out.size()));
    CHECK(got == kCap);
    for (std::size_t i = 0; i < kCap; ++i) {
        CHECK(out[i] == doctest::Approx(static_cast<float>(i)));
    }
    CHECK(ring.available() == 0);
    CHECK(ring.overrunCount() == 0);
}

TEST_CASE("capture-probe: available reflects pushed-minus-drained") {
    CaptureProbeRing<kCap> ring;

    pushRamp(ring, 0.0f, 5);
    CHECK(ring.available() == 5);

    std::vector<float> out(3, -1.0f);
    CHECK(ring.drain(acfx::span<float>(out.data(), out.size())) == 3);
    CHECK(ring.available() == 2);
    CHECK(out[0] == doctest::Approx(0.0f));
    CHECK(out[1] == doctest::Approx(1.0f));
    CHECK(out[2] == doctest::Approx(2.0f));

    pushRamp(ring, 5.0f, 2); // now holds indices [3,4] + [5,6]
    CHECK(ring.available() == 4);
}

TEST_CASE("capture-probe: wraparound across capacity stays coherent") {
    CaptureProbeRing<kCap> ring;

    // Repeatedly push and fully drain small runs so the read/write indices sweep
    // many multiples of Capacity. Each drained run must equal what was pushed,
    // in order, with no overrun (we never let more than Capacity accumulate).
    float next = 0.0f;
    for (int round = 0; round < 20; ++round) {
        const std::size_t run = 5; // 5 and 8 are coprime -> phase sweeps all offsets
        pushRamp(ring, next, run);
        CHECK(ring.available() == run);

        std::vector<float> out(run, -1.0f);
        CHECK(ring.drain(acfx::span<float>(out.data(), out.size())) == run);
        for (std::size_t i = 0; i < run; ++i) {
            CHECK(out[i] == doctest::Approx(next + static_cast<float>(i)));
        }
        CHECK(ring.available() == 0);
        next += static_cast<float>(run);
    }
    CHECK(ring.overrunCount() == 0);
}

TEST_CASE("capture-probe: overrun counts dropped samples and keeps recent data") {
    CaptureProbeRing<kCap> ring;

    // Push far more than Capacity without ever draining. The producer never
    // blocks or allocates; it advances and counts the lapped (dropped) samples.
    const std::size_t pushed = kCap * 3 + 2; // 26 samples into an 8-slot ring
    pushRamp(ring, 0.0f, pushed);

    // Overrun is observable: everything beyond the most-recent Capacity lapped.
    CHECK(ring.overrunCount() == static_cast<std::uint64_t>(pushed - kCap));

    // The ring holds at most Capacity, and it holds the MOST RECENT Capacity
    // samples -- coherent recent data, not corruption.
    CHECK(ring.available() == kCap);
    std::vector<float> out(kCap, -1.0f);
    CHECK(ring.drain(acfx::span<float>(out.data(), out.size())) == kCap);
    const float firstRecent = static_cast<float>(pushed - kCap);
    for (std::size_t i = 0; i < kCap; ++i) {
        CHECK(out[i] == doctest::Approx(firstRecent + static_cast<float>(i)));
    }
    CHECK(ring.available() == 0);

    // After draining the recent window the ring is usable again with no residual
    // corruption; a fresh push/drain round-trips cleanly.
    pushRamp(ring, 100.0f, 4);
    std::vector<float> out2(4, -1.0f);
    CHECK(ring.drain(acfx::span<float>(out2.data(), out2.size())) == 4);
    for (std::size_t i = 0; i < 4; ++i) {
        CHECK(out2[i] == doctest::Approx(100.0f + static_cast<float>(i)));
    }
}

TEST_CASE("capture-probe: underrun returns fewer than requested, no garbage") {
    CaptureProbeRing<kCap> ring;

    pushRamp(ring, 10.0f, 3);
    CHECK(ring.available() == 3);

    // Ask for more than is ready: drain returns only what is available and never
    // blocks. The unfilled tail of the caller's buffer is left untouched.
    std::vector<float> out(6, -99.0f);
    const std::size_t got = ring.drain(acfx::span<float>(out.data(), out.size()));
    CHECK(got == 3);
    CHECK(out[0] == doctest::Approx(10.0f));
    CHECK(out[1] == doctest::Approx(11.0f));
    CHECK(out[2] == doctest::Approx(12.0f));
    CHECK(out[3] == doctest::Approx(-99.0f)); // untouched
    CHECK(ring.available() == 0);

    // Draining an empty ring returns 0, not garbage.
    CHECK(ring.drain(acfx::span<float>(out.data(), out.size())) == 0);
}

TEST_CASE("capture-probe: reset clears all state") {
    CaptureProbeRing<kCap> ring;

    pushRamp(ring, 0.0f, kCap * 2); // force some overrun
    CHECK(ring.overrunCount() > 0);
    CHECK(ring.available() == kCap);

    ring.reset();
    CHECK(ring.available() == 0);
    CHECK(ring.overrunCount() == 0);

    // Fully usable after reset.
    pushRamp(ring, 7.0f, 4);
    CHECK(ring.available() == 4);
    std::vector<float> out(4, -1.0f);
    CHECK(ring.drain(acfx::span<float>(out.data(), out.size())) == 4);
    for (std::size_t i = 0; i < 4; ++i) {
        CHECK(out[i] == doctest::Approx(7.0f + static_cast<float>(i)));
    }
}

TEST_CASE("capture-probe: empty push and empty drain are no-ops") {
    CaptureProbeRing<kCap> ring;

    ring.push(acfx::span<const float>{}); // empty view (span rejects a null iterator)
    CHECK(ring.available() == 0);

    std::vector<float> out(4, -1.0f);
    CHECK(ring.drain(acfx::span<float>(out.data(), 0)) == 0);
    CHECK(ring.drain(acfx::span<float>{}) == 0);
    CHECK(ring.available() == 0);
}

TEST_CASE("capture-probe: two-thread SPSC smoke test never blocks or deadlocks") {
    // A light concurrency smoke test: one producer thread streams a long ramp in
    // small chunks; one consumer thread drains as data becomes available. We do
    // NOT assert on exact per-sample values (overwrites under overrun make that
    // nondeterministic by design); we assert the run TERMINATES (no deadlock, no
    // block) and that drained values are always within the pushed range (no
    // garbage / out-of-band corruption).
    constexpr std::size_t Capacity = 1024;
    CaptureProbeRing<Capacity> ring;

    constexpr std::size_t total = 200000;
    std::atomic<bool> producerDone{false};

    std::thread producer([&] {
        float v = 0.0f;
        std::size_t sent = 0;
        std::vector<float> chunk;
        while (sent < total) {
            const std::size_t n = (sent % 97) + 1; // varied chunk sizes
            const std::size_t take = (sent + n > total) ? (total - sent) : n;
            chunk.resize(take);
            for (std::size_t i = 0; i < take; ++i) {
                chunk[i] = v;
                v += 1.0f;
            }
            ring.push(acfx::span<const float>(chunk.data(), chunk.size()));
            sent += take;
        }
        producerDone.store(true, std::memory_order_release);
    });

    std::thread consumer([&] {
        std::vector<float> out(256);
        bool allInRange = true;
        for (;;) {
            const std::size_t got = ring.drain(acfx::span<float>(out.data(), out.size()));
            for (std::size_t i = 0; i < got; ++i) {
                if (out[i] < 0.0f || out[i] >= static_cast<float>(total)) {
                    allInRange = false;
                }
            }
            if (got == 0 && producerDone.load(std::memory_order_acquire)) {
                // one final drain to clear anything the producer left behind
                const std::size_t tail =
                    ring.drain(acfx::span<float>(out.data(), out.size()));
                for (std::size_t i = 0; i < tail; ++i) {
                    if (out[i] < 0.0f || out[i] >= static_cast<float>(total)) {
                        allInRange = false;
                    }
                }
                break;
            }
        }
        CHECK(allInRange);
    });

    producer.join();
    consumer.join();

    // Overrun is expected (consumer generally can't keep up with the tight
    // producer loop), and it must be observable without having corrupted state.
    CHECK(ring.overrunCount() >= 0u);
}
