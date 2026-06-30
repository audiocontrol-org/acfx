#pragma once

#include <cmath>
#include <cstdint>

// Platform-independent, allocation-free LFO primitive (acfx modulated-delay).
// Shapes: sine, triangle, saw, smoothed-random (xorshift PRNG, seedable).
// All methods that run in the audio path are RT-safe: no heap, no locks, bounded work.
// See: specs/modulated-delay/contracts/lfo.md  (Constitution IV, V, VI)

namespace acfx {

enum class LfoShape : std::uint8_t { sine, triangle, saw, random };

class Lfo {
public:
    // Record sample rate and reset phase + random state deterministically.
    void prepare(float sampleRate) noexcept {
        sampleRate_ = sampleRate;
        reset();
    }

    // Phase -> 0; re-seed random state to a fixed deterministic value.
    // Safe to call from any thread while the stream is stopped.
    void reset() noexcept {
        phase_      = 0.0f;
        rngState_   = kSeed;
        randCurr_   = generateFloat();
        randTarget_ = generateFloat();
    }

    // inc_ = hz / sampleRate. RT-safe, no allocation.
    void setRate(float hz) noexcept {
        inc_ = hz / sampleRate_;
    }

    void setShape(LfoShape shape) noexcept {
        shape_ = shape;
    }

    // Advance one sample; return bipolar value in [-1, 1].
    // Allocation-free, lock-free, bounded work (Constitution VI).
    float tick() noexcept {
        float out = computeOutput();  // output at current phase, then advance
        phase_ += inc_;
        if (phase_ >= 1.0f) {
            phase_      -= 1.0f;
            randCurr_    = randTarget_;
            randTarget_  = generateFloat();
        }
        return out;
    }

private:
    static constexpr uint32_t kSeed  = 0x12345678u;
    // Literal 2*pi (not std::numbers, which is C++20-only) so this core header also
    // compiles on the C++17 Teensy toolchain (plan Technical Context; Constitution IV/VII).
    static constexpr float    kTwoPi = 6.28318530717958647692f;

    float computeOutput() const noexcept {
        switch (shape_) {
        case LfoShape::sine:
            return std::sin(kTwoPi * phase_);
        case LfoShape::triangle:
            // Phase [0, 0.5) -> rise from -1 to +1; [0.5, 1) -> fall from +1 to -1.
            return (phase_ < 0.5f)
                ? (4.0f * phase_ - 1.0f)
                : (3.0f - 4.0f * phase_);
        case LfoShape::saw:
            // Rising ramp: phase 0 -> -1, phase 1 -> +1.
            return 2.0f * phase_ - 1.0f;
        case LfoShape::random:
            // Smoothed sample-and-hold: linearly interpolate randCurr_ -> randTarget_
            // across the period. Max delta per sample = 2 * inc_ (click-free by design).
            return randCurr_ + phase_ * (randTarget_ - randCurr_);
        }
        return 0.0f; // unreachable; silences exhaustiveness warning
    }

    // xorshift32 — seedable, no heap, no locks. Used only when phase wraps
    // (once per LFO period), so RT cost is amortised over the whole period.
    uint32_t xorshift32() noexcept {
        rngState_ ^= rngState_ << 13;
        rngState_ ^= rngState_ >> 17;
        rngState_ ^= rngState_ << 5;
        return rngState_;
    }

    // Map a uint32 uniformly to [-1, 1].
    float generateFloat() noexcept {
        return static_cast<float>(xorshift32()) /
               static_cast<float>(0xFFFFFFFFu) * 2.0f - 1.0f;
    }

    float    sampleRate_ = 48000.0f;
    float    phase_      = 0.0f;
    float    inc_        = 0.0f;
    LfoShape shape_      = LfoShape::sine;
    uint32_t rngState_   = kSeed;
    float    randCurr_   = 0.0f;
    float    randTarget_ = 0.0f;
};

} // namespace acfx
