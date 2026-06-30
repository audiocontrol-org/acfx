#pragma once

#include <algorithm>
#include <cstring>

// Allocation-free circular delay buffer with fractional (linear-interpolated)
// reads. The caller (effect) owns the backing storage and binds it via
// prepare(). No heap allocation occurs inside this class (Constitution VI).
//
// Read convention (backward from write position):
//   write(x) stores x at buffer_[writePos_] and advances writePos_ forward
//   (post-increment, mod capacity).  "d samples ago" means:
//     - newer sample (i = floor(d)): buffer_[(writePos_ - 1 - i + 2*capacity_) % capacity_]
//     - older sample (i+1 steps):    buffer_[(writePos_ - 2 - i + 2*capacity_) % capacity_]
//   readFractional(d) = (1-f)*newer + f*older, where f = d - floor(d).
//
// In-range guarantee (FR-007): for ANY delaySamples value — negative, 0, huge,
// beyond capacity — readFractional accesses ONLY indices in [0, capacity_).
// delaySamples is clamped to [0, capacity_-1] before the fetch.
//
// Platform independence (Constitution IV): standard library only; no desktop
// or MCU platform-specific headers.

namespace acfx {

class DelayLine {
public:
    // Bind preallocated caller-owned storage. No allocation here.
    // `storage` must point to at least `capacity` floats.
    // `capacity` should be >= maxDelaySamples + 1 guard sample.
    // Calls reset() to zero the buffer and place the write head at 0.
    void prepare(float* storage, int capacity, float sampleRate) noexcept {
        buffer_     = storage;
        capacity_   = capacity;
        sampleRate_ = sampleRate;
        reset();
    }

    // Zero the buffer and reset the write position.
    // Intended for use when the audio stream is stopped or on first use.
    void reset() noexcept {
        if (buffer_ != nullptr && capacity_ > 0) {
            std::memset(buffer_, 0,
                        static_cast<std::size_t>(capacity_) * sizeof(float));
        }
        writePos_ = 0;
    }

    // Push one sample and advance the write position (mod capacity).
    // Allocation-free, bounded work (Constitution VI).
    void write(float x) noexcept {
        buffer_[writePos_] = x;
        writePos_          = (writePos_ + 1) % capacity_;
    }

    // Read `delaySamples` in the past via linear interpolation.
    // delaySamples is clamped to [0, capacity-1]; the read is ALWAYS in range.
    //
    // Let clamped d have integer part i and fraction f (d = i + f, 0 <= f < 1).
    // Result = (1-f) * sample[i steps ago] + f * sample[(i+1) steps ago].
    //
    // Index arithmetic: adding 2*capacity_ before the modulo guarantees a
    // non-negative dividend regardless of writePos_ or i value.
    // Safety proof: writePos_ in [0,C), i in [0,C-1], so the smallest
    //   (writePos_ - 1 - i + 2C) = (0 - 1 - (C-1) + 2C) = C > 0.  QED.
    float readFractional(float delaySamples) const noexcept {
        float clamped = std::clamp(delaySamples,
                                   0.0f,
                                   static_cast<float>(capacity_ - 1));
        int   i       = static_cast<int>(clamped);
        float f       = clamped - static_cast<float>(i);

        int newer_idx = (writePos_ - 1 - i + 2 * capacity_) % capacity_;
        int older_idx = (writePos_ - 2 - i + 2 * capacity_) % capacity_;

        return (1.0f - f) * buffer_[newer_idx] + f * buffer_[older_idx];
    }

    int   capacity()        const noexcept { return capacity_; }
    float sampleRate()      const noexcept { return sampleRate_; }

    // Maximum valid delaySamples argument: capacity - 1.
    float maxDelaySamples() const noexcept {
        return static_cast<float>(capacity_ - 1);
    }

private:
    float* buffer_     = nullptr;
    int    capacity_   = 0;
    int    writePos_   = 0;
    float  sampleRate_ = 48000.0f;
};

} // namespace acfx
