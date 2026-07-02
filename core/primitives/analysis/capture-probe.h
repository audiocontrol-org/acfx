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
// than Capacity outstanding, skips forward to the most-recent Capacity samples.
//
// Coherent-window-or-dropped-frame (drain): because the producer may overwrite
// slots the consumer is mid-copy, drain() validates every window it copies. It
// snapshots the write index, copies the chosen window, then RE-READS the write
// index; if the producer advanced far enough during the copy to overwrite the
// oldest slot copied, the window is TORN and is NOT returned. drain() retries a
// bounded number of times and, failing that, DROPS the frame (returns 0 and
// counts it in droppedFrameCount()) rather than hand a spliced old/new mix to
// the FFT. A dropped-torn-frame is the honest outcome for a live analyzer: the
// caller skips one update. This is best-effort by construction -- push() writes
// its buffer slots before publishing a single release store, so a torn read of
// the boundary slot by an *in-flight* (not-yet-published) push is inherent to
// overwrite-on-overrun SPSC and cannot be fully eliminated without cooperation
// from push(); the re-read reliably rejects the dominant case (a lapping push
// that completed during the copy). See contracts/capture-probe-api.md.
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

    // Constitution VI (RT safety): push() must never take a lock on the audio
    // path. The index/counter atomics are 64-bit; on a target where 64-bit
    // atomics are not always lock-free (e.g. 32-bit Cortex-M / Daisy / Teensy),
    // push() would silently lower to a locked libatomic call, violating the
    // no-lock audio-path contract. Fail loud at compile time instead. This holds
    // on desktop x86-64 (64-bit atomics are always lock-free) and only fires if
    // the probe is ever instantiated on a target that lacks them.
    static_assert(
        std::atomic<std::uint64_t>::is_always_lock_free,
        "CaptureProbeRing requires lock-free atomics on the audio path "
        "(Constitution VI); this target lacks lock-free 64-bit atomics -- use a "
        "lock-free-width index before embedding.");

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
    // Returns the number actually copied on a COHERENT window (< out.size() on
    // underrun), or 0 for a dropped-torn-frame (see the header banner and
    // droppedFrameCount()). Never blocks, never allocates, never fabricates
    // samples.
    std::size_t drain(span<float> out) noexcept {
        const std::size_t want = out.size();

        // Coherent-window-or-dropped-frame. Each attempt: snapshot the write
        // index, choose + copy a window, then re-read the write index. The
        // window is coherent iff the producer did not overwrite the oldest slot
        // we copied while we were copying, i.e. finalWrite - r <= Capacity (a
        // slot at logical index p is overwritten once the producer reaches
        // logical index p + Capacity, and r is the oldest slot we read).
        for (int attempt = 0; attempt < kMaxDrainAttempts; ++attempt) {
            // Consumer owns readIndex_ (relaxed self-read); observes
            // writeIndex_ (acquire) for freshly published data.
            std::uint64_t r = readIndex_.load(std::memory_order_relaxed);
            const std::uint64_t w = writeIndex_.load(std::memory_order_acquire);

            std::uint64_t unread = w - r;
            if (unread > Capacity) {
                // The producer lapped us: keep only the most-recent Capacity
                // samples so we serve recent (not stale) data.
                r = w - Capacity;
                unread = Capacity;
            }

            const std::size_t take =
                (want < static_cast<std::size_t>(unread))
                    ? want
                    : static_cast<std::size_t>(unread);

            // Nothing copied (underrun / zero-size request) -> nothing can be
            // torn. Preserve the overrun-skip of readIndex_ and return.
            if (take == 0) {
                readIndex_.store(r, std::memory_order_release);
                return 0;
            }

            for (std::size_t i = 0; i < take; ++i) {
                out[i] = buffer_[static_cast<std::size_t>((r + i) % Capacity)];
            }

            // Re-read the write index: did the producer overwrite the oldest
            // slot (logical index r) we just copied while we were copying?
            const std::uint64_t finalWrite =
                writeIndex_.load(std::memory_order_acquire);
            if (finalWrite - r <= Capacity) {
                // Coherent window: commit the read and return it.
                readIndex_.store(r + take, std::memory_order_release);
                return take;
            }
            // Torn window: retry with a fresh, more-recent window.
        }

        // Retry budget exhausted -> drop the torn frame. Resynchronise the
        // reader to the freshest coherent boundary so subsequent drains stay
        // consistent, count the drop, and return 0 (caller skips this update).
        const std::uint64_t w = writeIndex_.load(std::memory_order_acquire);
        readIndex_.store(w > Capacity ? (w - Capacity) : 0,
                         std::memory_order_release);
        droppedFrameCount_.fetch_add(1, std::memory_order_relaxed);
        return 0;
    }

    // Observable count of samples dropped because the producer lapped the
    // consumer (FR-013). Producer-owned. Monotonic until reset().
    std::uint64_t overrunCount() const noexcept {
        return overrunCount_.load(std::memory_order_relaxed);
    }

    // Observable count of drain() calls that returned 0 because the copied
    // window was torn (producer overwrote it mid-copy) and could not be
    // re-copied coherently within the retry budget. Consumer-owned. Monotonic
    // until reset().
    std::uint64_t droppedFrameCount() const noexcept {
        return droppedFrameCount_.load(std::memory_order_relaxed);
    }

    // Clear all indices and counters. Not called concurrently with
    // push()/drain() (contract).
    void reset() noexcept {
        writeIndex_.store(0, std::memory_order_relaxed);
        readIndex_.store(0, std::memory_order_relaxed);
        overrunCount_.store(0, std::memory_order_relaxed);
        droppedFrameCount_.store(0, std::memory_order_relaxed);
    }

    // Compile-time capacity (statically-known embedded RAM).
    static constexpr std::size_t capacity() noexcept { return Capacity; }

private:
    // Bounded retries let a transient lap resolve to a coherent window before
    // we give up and drop the frame. drain() runs on the analysis thread (not
    // the audio path), so a small bounded loop here is not an RT concern.
    static constexpr int kMaxDrainAttempts = 4;

    // Monotonically increasing 64-bit counters; the physical slot is index %
    // Capacity. 64-bit width makes wraparound of the counters themselves a
    // non-concern for any realistic runtime.
    std::array<float, Capacity> buffer_{};
    std::atomic<std::uint64_t> writeIndex_{0};
    std::atomic<std::uint64_t> readIndex_{0};
    std::atomic<std::uint64_t> overrunCount_{0};    // producer-owned
    std::atomic<std::uint64_t> droppedFrameCount_{0}; // consumer-owned
};

} // namespace acfx
