#pragma once

#include <cstdint>
#include <cstring>
#include <vector>

#include "audio-ring.h"
#include "sample-format.h"
#include "transport-stats.h"
#include "usb-out-path.h"

// Shared host-side fixtures for the polled OUT path's multi-packet suites:
// tests/core/nucleo-usb-out-service-test.cpp (the per-call read bound and the
// chunk loop) and tests/core/nucleo-usb-out-flush-test.cpp (clear-on-tear).
// Split out on the same rationale as opamp-stage-test-support.h — two suites
// need the identical simulated fifo and payload builders, and duplicating them
// would let the two copies drift apart while both kept passing.
//
// NO doctest dependency here: these helpers build data and report facts, and
// every assertion stays in the suite that owns the case.

namespace nucleo_out_test {

using acfx::nucleo::kChannels;
using acfx::nucleo::kInt16Scale;
using acfx::nucleo::kMaxPacketFrames;
using acfx::nucleo::UsbOutPath;

inline constexpr int kBytesPerFrame = kChannels * static_cast<int>(sizeof(std::int16_t));

// Ring big enough that several maximum packets fit without overflowing, so the
// alignment assertions in the suites are never confounded by AR3 drops.
using BigRing = acfx::nucleo::AudioRing<256, kChannels>;

// Interleaved stereo payload: left counts up from `base`, right is the left
// sample negated, so a de-interleave that crosses channels or slips by a sample
// shows up as a sign flip rather than only as a magnitude difference.
inline std::vector<std::int16_t> makePayload(int frames, std::int16_t base = 0) {
    std::vector<std::int16_t> payload(static_cast<std::size_t>(kChannels * frames));
    for (int frame = 0; frame < frames; ++frame) {
        const auto left = static_cast<std::int16_t>(base + frame + 1);
        payload[static_cast<std::size_t>(kChannels * frame)] = left;
        payload[static_cast<std::size_t>(kChannels * frame + 1)] =
            static_cast<std::int16_t>(-left);
    }
    return payload;
}

inline float expected(std::int16_t sample) noexcept {
    return static_cast<float>(sample) / kInt16Scale;
}

inline int bytesFor(int frames) noexcept { return frames * kBytesPerFrame; }

// A simulated TinyUSB OUT endpoint fifo: an UNFRAMED run of bytes. push()
// appends a payload exactly as the ISR appends one, leaving no record of where
// it began; read() hands back whatever the caller asks for, capped at what is
// queued, exactly as tu_fifo_read_n() does; clear() discards everything, as
// tu_fifo_clear() does behind tud_audio_clear_ep_out_ff().
//
// clear() is INSTRUMENTED — it counts its calls and records how many bytes each
// one threw away — because "the flush happened, exactly once, and discarded
// exactly this much" is the whole behaviour under test and is otherwise
// invisible from outside the fifo.
class FakeOutFifo {
public:
    void push(const std::vector<std::int16_t>& samples, int byteCount) {
        const auto* raw = reinterpret_cast<const std::uint8_t*>(samples.data());
        bytes_.insert(bytes_.end(), raw, raw + byteCount);
    }

    int available() noexcept {
        return static_cast<int>(bytes_.size() - readIndex_);
    }

    int read(std::int16_t* dst, int maxBytes) noexcept {
        const int queued = available();
        const int count = (queued < maxBytes) ? queued : maxBytes;
        if (count > 0) {
            std::memcpy(dst, bytes_.data() + readIndex_, static_cast<std::size_t>(count));
            readIndex_ += static_cast<std::size_t>(count);
        }
        // The ISR race, modelled where it actually happens: an OUT transfer can
        // complete and append a payload AFTER the read has returned and BEFORE
        // the caller has finished the pass. This is the ONLY way the fifo is
        // non-empty when a torn read is detected — a read that returns a
        // non-multiple-of-4 count is necessarily a read that drained the fifo
        // (tu_fifo_read_n returns min(queued, requested)) — and therefore the
        // only way a clear-on-tear flush discards anything at all.
        if (!injected_.empty()) {
            bytes_.insert(bytes_.end(), injected_.begin(), injected_.end());
            injected_.clear();
        }
        return count;
    }

    // Append `byteCount` bytes of `samples` immediately after the NEXT read
    // returns, as an OUT-transfer-complete ISR would.
    void injectAfterNextRead(const std::vector<std::int16_t>& samples, int byteCount) {
        const auto* raw = reinterpret_cast<const std::uint8_t*>(samples.data());
        injected_.assign(raw, raw + byteCount);
    }

    bool clear() noexcept {
        ++clearCalls_;
        if (refuseClear_) {
            // TinyUSB's tud_audio_clear_ep_out_ff() returns false without
            // touching the fifo when its TU_VERIFY fails (the audio function is
            // not configured). Model that faithfully: refusing must leave the
            // bytes in place, or the test could not tell a refusal apart from a
            // successful flush.
            return false;
        }
        lastClearDiscardedBytes_ = available();
        totalClearDiscardedBytes_ += lastClearDiscardedBytes_;
        bytes_.clear();
        readIndex_ = 0;
        return true;
    }

    // Make every subsequent clear() report failure, as an unconfigured audio
    // function does.
    void refuseClear() noexcept { refuseClear_ = true; }

    int clearCalls() const noexcept { return clearCalls_; }
    int lastClearDiscardedBytes() const noexcept { return lastClearDiscardedBytes_; }
    int totalClearDiscardedBytes() const noexcept { return totalClearDiscardedBytes_; }

private:
    std::vector<std::uint8_t> bytes_;
    std::vector<std::uint8_t> injected_;
    std::size_t readIndex_ = 0;
    int clearCalls_ = 0;
    int lastClearDiscardedBytes_ = 0;
    int totalClearDiscardedBytes_ = 0;
    bool refuseClear_ = false;
};

// Staging buffer with exactly the geometry the shim gives serviceOutFifo():
// one maximum packet, no more.
using StagingBuffer =
    std::int16_t[UsbOutPath::maxPayloadBytes() / static_cast<int>(sizeof(std::int16_t))];

// Drain everything the fifo currently holds, one service-loop pass per call,
// and return what each pass reported. A flush inside a pass empties the fifo
// and so ends the drain, which is exactly what the board does.
template <typename Ring>
std::vector<acfx::nucleo::OutServicePass> drainFifo(FakeOutFifo& fifo,
                                                    UsbOutPath& path,
                                                    StagingBuffer& buffer,
                                                    Ring& ring,
                                                    acfx::nucleo::AudioTransportStats& stats) {
    std::vector<acfx::nucleo::OutServicePass> passes;
    while (fifo.available() > 0) {
        passes.push_back(
            acfx::nucleo::serviceOutFifo(fifo, path, buffer, ring, stats));
    }
    return passes;
}

// Read `frames` frames out of `ring` into caller-owned buffers.
template <typename Ring>
inline int drainRing(Ring& ring, std::vector<float>& left, std::vector<float>& right, int frames) {
    left.assign(static_cast<std::size_t>(frames), 0.0f);
    right.assign(static_cast<std::size_t>(frames), 0.0f);
    float* dst[kChannels] = {left.data(), right.data()};
    return ring.read(dst, frames);
}

} // namespace nucleo_out_test
