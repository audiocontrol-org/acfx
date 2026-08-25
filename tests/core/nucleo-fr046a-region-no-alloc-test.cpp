#include <doctest/doctest.h>

#include <array>
#include <cstdint>
#include <cstring>
#include <vector>

#include "dsp/param-id.h"

#include "audio-ring.h"
#include "dsp-block-path.h"
#include "parameter-shadow.h"
#include "sample-format.h"
#include "transport-stats.h"
#include "usb-out-path.h"
#include "support/allocation-sentinel.h"

// T066 — the FR-046a/FR-046b no-allocation / no-lock assertion, over the
// region FR-046a names EXPLICITLY, as one composed unit:
//
//   1. payload truncation      usb-out-path.h    (FR-028a)
//   2. format conversion       sample-format.h   (FR-038)
//   3. ring access             audio-ring.h      (FR-030)
//   4. the parameter dirty-flag walk   parameter-shadow.h  (FR-042)
//   5. process()                dsp-block-path.h  (the DSP block path)
//
// Four of these five already carry a dynamic AllocationSentinel assertion of
// their own: OP8 (nucleo-usb-out-path-test.cpp) for (1), SF4
// (nucleo-sample-format-no-allocation-test.cpp) for (2), AR5
// (nucleo-audio-ring-no-allocation-test.cpp) for (3), and DB8
// (nucleo-dsp-block-path-test.cpp) for (5). Stage (4) — the parameter
// dirty-flag walk — had ONLY a family of `static_assert(noexcept(...))`
// checks (PS6 in nucleo-parameter-shadow-test.cpp): noexcept is necessary but
// not sufficient, because a function can be noexcept and still allocate (it
// would simply std::terminate on operator new failure rather than throw).
// FR-046a calls this stage out BY NAME precisely because "it does not look
// like audio code" and is the one that gets overlooked — and that is exactly
// what happened here until this file. REGION-A below closes that gap with a
// genuine dynamic sentinel assertion.
//
// REGION-B composes all five stages into ONE pass under a SINGLE sentinel
// window, so the claim under test is not "each stage is clean in isolation"
// (already covered) but "the composed region FR-046a defines is clean as a
// whole" — no stage's cleanliness depends on another stage having already
// reset the counters.
//
// POSITIVE CONTROL (TASK-27): every existing no-allocation suite asserts
// allocations() == 0 and nothing ever asserted allocations() > 0, so a
// sentinel that silently stopped tripping (a link-order change dropping
// support/allocation-sentinel.cpp, a toolchain eliding the operator new
// override) would go unnoticed while every claim above kept reading GREEN
// vacuously. nucleo-audio-ring-no-allocation-test.cpp already closed this
// with one positive control; POSITIVE-CONTROL below adds a second,
// independent one scoped to this file, so this file's own claims do not rely
// on a control living elsewhere.
//
// NO-LOCK: this is an inspection, not a runtime assertion (a lock that is
// never contended does not "fire" the way a heap allocation does). All five
// headers were grepped for `<mutex>`, `std::mutex`, `std::lock_guard`,
// `<atomic>`, `<thread>`, and `<semaphore>` — audio-ring.h, sample-format.h,
// usb-out-path.h, parameter-shadow.h, and dsp-block-path.h — zero hits in
// all five. This is not incidental: it is what D26 (single main-loop
// execution context; the interrupt handler only enqueues, FR-046) makes
// possible. ParameterShadow in particular has no synchronization of any
// kind — set() and flush() are both called from the SAME execution context
// (FR-047 names the explicit trigger for revisiting this: sampling a
// peripheral from a timer interrupt would break the single-context
// assumption and this file's no-lock claim along with it).

using namespace acfx;
using namespace acfx::nucleo;
using acfx::test::AllocationSentinel;

namespace {

// ---------------------------------------------------------------------------
// Fakes shared by REGION-A/B below. Deliberately free of std::vector members
// so nothing here needs a "grow before the measured region" caveat the way
// DB8's ScalingEffect does.
// ---------------------------------------------------------------------------

// Stage 5's effect: a fixed per-channel scale with plain scalar bookkeeping,
// no allocation possible because there is nothing that could allocate.
struct FixedScaleEffect {
    int calls = 0;
    int lastNumSamples = 0;

    void process(acfx::AudioBlock& io) noexcept {
        ++calls;
        lastNumSamples = io.numSamples();
        for (int frame = 0; frame < io.numSamples(); ++frame) {
            io.channel(0)[frame] *= 2.0f;
            io.channel(1)[frame] *= -3.0f;
        }
    }

    // What the flushed parameter shadow (stage 4) applies to. Bounded array,
    // no heap: mirrors how a real effect's setParameter() would be backed by
    // fixed member storage, not a container.
    static constexpr int kMaxParams = 8;
    std::array<float, kMaxParams> paramValues{};
    std::array<bool, kMaxParams> paramTouched{};

    void setParameter(ParamId id, float value) noexcept {
        if (id.value >= kMaxParams) {
            return;
        }
        paramValues[id.value] = value;
        paramTouched[id.value] = true;
    }
};

struct StepClock {
    std::uint32_t value = 0;
    std::uint32_t now() noexcept {
        value += 100;
        return value;
    }
};

using BigRing = AudioRing<512, kChannels>;

// A simulated TinyUSB OUT-endpoint fifo: an unframed run of bytes, exactly
// the shape nucleo-usb-out-service-test.cpp's FakeOutFifo uses. push()
// happens OUTSIDE every sentinel window in this file; only available()/
// read() run inside one, and neither allocates.
class FakeOutFifo {
public:
    void push(const std::vector<std::int16_t>& samples, int byteCount) {
        const auto* raw = reinterpret_cast<const std::uint8_t*>(samples.data());
        bytes_.insert(bytes_.end(), raw, raw + byteCount);
    }

    int available() noexcept { return static_cast<int>(bytes_.size() - readIndex_); }

    int read(std::int16_t* dst, int maxBytes) noexcept {
        const int queued = available();
        const int count = (queued < maxBytes) ? queued : maxBytes;
        if (count > 0) {
            std::memcpy(dst, bytes_.data() + readIndex_, static_cast<std::size_t>(count));
            readIndex_ += static_cast<std::size_t>(count);
        }
        return count;
    }

private:
    std::vector<std::uint8_t> bytes_;
    std::size_t readIndex_ = 0;
};

using StagingBuffer =
    std::int16_t[UsbOutPath::maxPayloadBytes() / static_cast<int>(sizeof(std::int16_t))];

// Interleaved stereo payload built OUTSIDE any sentinel window.
std::vector<std::int16_t> makePayload(int frames, std::int16_t base = 0) {
    std::vector<std::int16_t> payload(static_cast<std::size_t>(kChannels * frames));
    for (int frame = 0; frame < frames; ++frame) {
        const auto left = static_cast<std::int16_t>(base + frame + 1);
        payload[static_cast<std::size_t>(kChannels * frame)] = left;
        payload[static_cast<std::size_t>(kChannels * frame + 1)] = static_cast<std::int16_t>(-left);
    }
    return payload;
}

int bytesFor(int frames) noexcept {
    return frames * kChannels * static_cast<int>(sizeof(std::int16_t));
}

}  // namespace

// ============================================================================
// REGION-A — the stage FR-046a names explicitly and that had no dynamic
// coverage: the parameter dirty-flag walk (parameter-shadow.h, FR-042).
// ============================================================================

TEST_CASE("FR-046a/4: ParameterShadow::set() allocates nothing") {
    ParameterShadow<8> shadow;

    AllocationSentinel::reset();
    for (int iter = 0; iter < 200; ++iter) {
        shadow.set(iter % 8, static_cast<float>(iter % 8) / 8.0f);
    }
    const std::size_t allocations = AllocationSentinel::allocations();

    CHECK_MESSAGE(allocations == 0, "ParameterShadow::set() allocated ", allocations);
}

TEST_CASE("FR-046a/4: ParameterShadow::flush() allocates nothing") {
    ParameterShadow<8> shadow;
    FixedScaleEffect effect;

    AllocationSentinel::reset();
    for (int iter = 0; iter < 200; ++iter) {
        for (int i = 0; i < 8; ++i) {
            shadow.set(i, static_cast<float>((iter + i) % 8) / 8.0f);
        }
        shadow.flush([&effect](ParamId id, float value) noexcept {
            effect.setParameter(id, value);
        });
    }
    const std::size_t allocations = AllocationSentinel::allocations();

    CHECK_MESSAGE(allocations == 0, "ParameterShadow::flush() allocated ", allocations);
    // Sanity: the walk actually ran and reached the effect, so a green
    // allocation count is not vacuous over an empty loop.
    bool anyTouched = false;
    for (bool touched : effect.paramTouched) {
        anyTouched = anyTouched || touched;
    }
    CHECK(anyTouched);
}

TEST_CASE("FR-046a/4: dirty() (the audio-side peek used around the walk) allocates nothing") {
    ParameterShadow<8> shadow;
    shadow.set(3, 0.5f);

    AllocationSentinel::reset();
    bool result = false;
    for (int iter = 0; iter < 1000; ++iter) {
        result = shadow.dirty(3) || result;
    }
    const std::size_t allocations = AllocationSentinel::allocations();

    CHECK(result);
    CHECK_MESSAGE(allocations == 0, "ParameterShadow::dirty() allocated ", allocations);
}

// ============================================================================
// REGION-B — the composed FR-046a region: all five named stages under ONE
// sentinel window, packet arrival through the block's reply, so the claim is
// about the REGION FR-046a defines, not five isolated stages.
// ============================================================================

TEST_CASE("FR-046a/FR-046b: one full arrival-to-reply pass allocates nothing across "
          "all five named stages") {
    UsbOutPath outPath;
    AudioTransportStats stats;
    BigRing inputRing(1);   // promoted to Running by the first write below
    BigRing outputRing(0);
    ParameterShadow<8> shadow;
    FixedScaleEffect effect;
    DspBlockPath blockPath;
    StepClock clock;

    // Prime the OUT fifo with several packets, INCLUDING a torn one (FR-028a
    // truncation) and an empty one, so stage 1 genuinely exercises the
    // truncation-counting branch during the measured region, not just the
    // happy path. All construction/push work happens OUTSIDE the sentinel
    // window.
    FakeOutFifo fifo;
    fifo.push(makePayload(kBlockFrames), bytesFor(kBlockFrames));
    fifo.push(makePayload(kBlockFrames, 100), bytesFor(kBlockFrames) + 2);  // torn: +2 bytes
    fifo.push(makePayload(0), 0);                                          // empty payload
    fifo.push(makePayload(kBlockFrames, 200), bytesFor(kBlockFrames));

    StagingBuffer buffer{};

    AllocationSentinel::reset();

    // Stage 1 (truncation) + stage 2 (conversion, OUT direction) + stage 3
    // (ring write): drain every packet currently queued.
    while (fifo.available() > 0) {
        (void)serviceOutFifo(fifo, outPath, buffer, inputRing, stats);
    }

    // Stage 4: a burst of CC-style updates collapsing via last-write-wins,
    // then the dirty-flag walk applying them to the effect.
    for (int i = 0; i < 8; ++i) {
        shadow.set(i, static_cast<float>(i) / 8.0f);
    }
    shadow.set(2, 0.9f);  // second write to slot 2 — last-write-wins, no eviction
    shadow.flush([&effect](ParamId id, float value) noexcept {
        effect.setParameter(id, value);
    });

    // Stage 5 (process()) + stage 3 again (ring read on the way in, ring
    // write on the way out): run every whole block the input ring now holds.
    int blocksRun = 0;
    while (inputRing.occupancy() >= kBlockFrames) {
        const BlockPassResult pass = blockPath.runOneBlock(inputRing, outputRing, effect, stats, clock);
        if (!pass.blockProcessed) {
            break;
        }
        ++blocksRun;
    }

    // Stage 2 again (conversion, IN direction): read back out of the output
    // ring and convert to int16 the way the IN path's producer would.
    std::vector<float> left(static_cast<std::size_t>(kBlockFrames));
    std::vector<float> right(static_cast<std::size_t>(kBlockFrames));
    std::vector<std::int16_t> interleaved(static_cast<std::size_t>(2 * kBlockFrames));
    while (outputRing.occupancy() >= kBlockFrames) {
        float* dst[kChannels] = {left.data(), right.data()};
        outputRing.read(dst, kBlockFrames);
        const float* src[kChannels] = {left.data(), right.data()};
        interleaveToInt16(src, interleaved.data(), kBlockFrames);
    }

    const std::size_t allocations = AllocationSentinel::allocations();

    CHECK_MESSAGE(allocations == 0,
                  "the composed FR-046a region allocated ", allocations,
                  " time(s) across arrival-to-reply");
    // Sanity: the region actually did the work it claims to have measured.
    CHECK(stats.malformedPayloads >= 1);  // the torn packet was counted (FR-028a)
    CHECK(blocksRun > 0);
    CHECK(effect.calls == blocksRun);
    bool anyTouched = false;
    for (bool touched : effect.paramTouched) {
        anyTouched = anyTouched || touched;
    }
    CHECK(anyTouched);
}

// ============================================================================
// POSITIVE CONTROL (TASK-27) — scoped to this file, independent of the
// control already living in nucleo-audio-ring-no-allocation-test.cpp. If the
// sentinel ever stops tripping (dropped link, elided override), THIS case
// fails loudly instead of every REGION-A/B assertion above passing
// vacuously.
// ============================================================================

TEST_CASE("FR-046a positive control: a deliberate heap allocation trips the sentinel") {
    AllocationSentinel::reset();
    // Captured into plain locals BEFORE the first CHECK_MESSAGE: doctest's
    // own DOCTEST_INFO/CHECK_MESSAGE machinery can itself allocate (e.g. to
    // build the logged-message context) as a side effect of the macro
    // expansion, ahead of evaluating its condition. Reading the counter
    // straight inside a CHECK_MESSAGE condition would then race that
    // bookkeeping allocation rather than measuring the deliberate `new`
    // below — exactly the mistake this control exists to avoid making
    // elsewhere in this file.
    const std::size_t before = AllocationSentinel::allocations();

    // volatile defeats allocation elision; the pointer is genuinely read back
    // so the compiler cannot prove the store is dead and drop the `new`.
    volatile std::int32_t* probe = new std::int32_t(0x5A5A5A5A);
    const std::size_t after = AllocationSentinel::allocations();
    const bool tripped = after > before;
    const std::int32_t probeValue = *probe;
    delete probe;

    CHECK_MESSAGE(before == 0, "counter must start at zero before the deliberate allocation");
    CHECK(probeValue == 0x5A5A5A5A);
    CHECK_MESSAGE(tripped,
                  "sentinel did not trip on a deliberate `new` — every "
                  "no-allocation claim in this file (and the FR-046a region "
                  "as a whole) would be vacuous if this ever fails");
}
