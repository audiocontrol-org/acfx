#pragma once

// RT capture probe -- the audio -> analysis bridge and the ONLY audio-path unit
// in the harmonic-analysis feature (contracts/capture-probe-api.md,
// data-model.md "CaptureProbeRing", research.md Decision 7, FR-011/012/013).
//
// A single-producer/single-consumer (SPSC), lock-free ring over a fixed
// compile-time-sized value buffer. Portable core: only stdlib is included
// (<atomic>/<array>/<cstddef>/<cstdint>) plus the repo's span vocabulary type;
// NO host, JUCE, platform, or analysis headers reach this file (gate C-AN-PRIM).
//
// Concurrency contract (Constitution VI -- RT safety):
//   - The audio (producer) thread calls push() only. It performs a bounded copy
//     and exactly one release store of the write index -- NO heap allocation, NO
//     lock, NO analysis math.
//   - The analysis (consumer) thread calls available()/drain() only. It advances
//     the read index with acquire/release.
//   - Single writer per atomic: the producer owns writeIndex_ and overrunCount_;
//     the consumer owns readIndex_. Each side reads the other's index with an
//     acquire load and never writes it -- so this stays truly SPSC lock-free.
//   - reset() is not called concurrently with push()/drain() (contract).
//
// Overrun (producer laps the unread consumer): the producer NEVER blocks or
// allocates. It writes anyway (overwriting the oldest unread sample) and counts
// each lapped sample in the observable overrunCount(). The consumer, seeing more
// than Capacity outstanding, skips forward to the most-recent Capacity samples so
// every drain returns coherent recent data rather than a stale/torn window.
//
// Underrun (fewer than requested ready): drain() copies only what is available
// and returns that count -- it never blocks and never fabricates samples.

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>

#include "dsp/span.h"

namespace acfx {

template <std::size_t Capacity>
class CaptureProbeRing {
public:
    static_assert(Capacity > 0, "CaptureProbeRing capacity must be positive");

    CaptureProbeRing() noexcept = default;

    // --- Audio-thread surface (RT-safe, noexcept) --------------------------
    // Bounded copy of up to in.size() samples into the ring + one release store.
    // No alloc, no lock, no math. On overrun the oldest unread samples are
    // overwritten and counted; the call always completes without blocking.
    void push(span<const float> in) noexcept {
        // Producer owns writeIndex_ (relaxed self-read); it only observes the
        // consumer's readIndex_ (acquire) to detect lapping -- never writes it.
        std::uint64_t w = writeIndex_.load(std::memory_order_relaxed);
        const std::uint64_t r = readIndex_.load(std::memory_order_acquire);

        const std::size_t n = in.size();
        for (std::size_t i = 0; i < n; ++i) {
            buffer_[static_cast<std::size_t>(w % Capacity)] = in[i];
            // Writing this slot laps the consumer iff at least Capacity unread
            // samples already stand between the reader's position (r) and w.
            if (w - r >= Capacity) {
                overrunCount_.fetch_add(1, std::memory_order_relaxed);
            }
            ++w;
        }

        // Single release store publishes all copied samples to the consumer.
        writeIndex_.store(w, std::memory_order_release);
    }

    // --- Analysis-thread surface (lock-free) -------------------------------
    // Samples ready to drain, clamped to Capacity (an overrun cannot make more
    // than Capacity samples readable).
    std::size_t available() const noexcept {
        const std::uint64_t r = readIndex_.load(std::memory_order_acquire);
        const std::uint64_t w = writeIndex_.load(std::memory_order_acquire);
        const std::uint64_t unread = w - r;
        return unread > Capacity ? Capacity : static_cast<std::size_t>(unread);
    }

    // Copy up to out.size() ready samples into out, advancing the read index
    // (acquire load of the write index, release store of the read index).
    // Returns the number actually copied (< out.size() on underrun). Never
    // blocks, never allocates, never fabricates samples.
    std::size_t drain(span<float> out) noexcept {
        // Consumer owns readIndex_ (relaxed self-read); observes writeIndex_
        // (acquire) for freshly published data.
        std::uint64_t r = readIndex_.load(std::memory_order_relaxed);
        const std::uint64_t w = writeIndex_.load(std::memory_order_acquire);

        std::uint64_t unread = w - r;
        if (unread > Capacity) {
            // The producer lapped us: discard everything but the most-recent
            // Capacity samples so we return coherent recent data, not a stale or
            // partially-overwritten window.
            r = w - Capacity;
            unread = Capacity;
        }

        const std::size_t want = out.size();
        const std::size_t take =
            (want < static_cast<std::size_t>(unread)) ? want
                                                      : static_cast<std::size_t>(unread);
        for (std::size_t i = 0; i < take; ++i) {
            out[i] = buffer_[static_cast<std::size_t>((r + i) % Capacity)];
        }

        readIndex_.store(r + take, std::memory_order_release);
        return take;
    }

    // Observable count of samples dropped because the producer lapped the
    // consumer (FR-013). Monotonic until reset().
    std::uint64_t overrunCount() const noexcept {
        return overrunCount_.load(std::memory_order_relaxed);
    }

    // Clear all indices and the overrun counter. Not called concurrently with
    // push()/drain() (contract).
    void reset() noexcept {
        writeIndex_.store(0, std::memory_order_relaxed);
        readIndex_.store(0, std::memory_order_relaxed);
        overrunCount_.store(0, std::memory_order_relaxed);
    }

    // Compile-time capacity (statically-known embedded RAM).
    static constexpr std::size_t capacity() noexcept { return Capacity; }

private:
    // Monotonically increasing 64-bit counters; the physical slot is index %
    // Capacity. 64-bit width makes wraparound of the counters themselves a
    // non-concern for any realistic runtime.
    std::array<float, Capacity> buffer_{};
    std::atomic<std::uint64_t> writeIndex_{0};
    std::atomic<std::uint64_t> readIndex_{0};
    std::atomic<std::uint64_t> overrunCount_{0};
};

} // namespace acfx
