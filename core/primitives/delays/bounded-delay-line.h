#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <type_traits>

#include "primitives/lofi/int16-quant.h"

// Statically-sized delay line whose storage is an in-object
// std::array<Sample, MaxSamples> (no heap, no caller-owned pointer). It
// replicates acfx::DelayLine's write/readFractional/reset math exactly
// (core/primitives/delays/delay-line.h) so that, for Sample = float, it is
// bit-identical to DelayLine. For Sample = std::int16_t, values are routed
// through store()/load() to quantize on write and dequantize on read
// (core int16 convention, primitives/lofi/int16-quant.h) -- see
// quantizeInt16()/dequantizeInt16() for the exact scale/round/clamp rule.
//
// Platform independence (Constitution IV): standard library only.

namespace acfx {

template <typename Sample, std::size_t MaxSamples>
class BoundedDelayLine {
public:
    // capacity must be <= MaxSamples; a violation is a programming error, so
    // it is clamped rather than allowed to index out of the owned storage.
    void prepare(int capacity, float sampleRate) noexcept {
        capacity_   = std::clamp(capacity, 0, static_cast<int>(MaxSamples));
        sampleRate_ = sampleRate;
        reset();
    }

    // Zero the buffer and reset the write position.
    void reset() noexcept {
        buffer_.fill(Sample{});
        writePos_ = 0;
    }

    // Push one sample and advance the write position (mod capacity).
    void write(float x) noexcept {
        store(buffer_[static_cast<std::size_t>(writePos_)], x);
        writePos_ = (writePos_ + 1) % capacity_;
    }

    // Read `delaySamples` in the past via linear interpolation. Identical
    // clamp/index math to acfx::DelayLine::readFractional.
    float readFractional(float delaySamples) const noexcept {
        float clamped = std::clamp(delaySamples,
                                    0.0f,
                                    static_cast<float>(capacity_ - 1));
        int   i       = static_cast<int>(clamped);
        float f       = clamped - static_cast<float>(i);

        int newer_idx = (writePos_ - 1 - i + 2 * capacity_) % capacity_;
        int older_idx = (writePos_ - 2 - i + 2 * capacity_) % capacity_;

        const float newer = load(buffer_[static_cast<std::size_t>(newer_idx)]);
        const float older = load(buffer_[static_cast<std::size_t>(older_idx)]);

        return (1.0f - f) * newer + f * older;
    }

    int   capacity()        const noexcept { return capacity_; }
    float sampleRate()      const noexcept { return sampleRate_; }

    // Maximum valid delaySamples argument: capacity - 1.
    float maxDelaySamples() const noexcept {
        return static_cast<float>(capacity_ - 1);
    }

private:
    // Identity store/load for float; quantize/dequantize for int16.
    static void store(Sample& slot, float x) noexcept {
        if constexpr (std::is_same_v<Sample, std::int16_t>) {
            slot = quantizeInt16(x);
        } else {
            slot = x;
        }
    }

    static float load(const Sample& slot) noexcept {
        if constexpr (std::is_same_v<Sample, std::int16_t>) {
            return dequantizeInt16(slot);
        } else {
            return slot;
        }
    }

    std::array<Sample, MaxSamples> buffer_{};
    int   capacity_   = 0;
    int   writePos_   = 0;
    float sampleRate_ = 48000.0f;
};

} // namespace acfx
