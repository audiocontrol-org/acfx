#pragma once

#include <array>
#include <vector>

#include "primitives/delays/delay-line.h"
#include "primitives/modulation/lfo.h"

// WowFlutterStage — input-path tape-instability processor (US3, FR-017..FR-021).
// Owns its own short per-channel DelayLine plus two independent LFOs:
//   wow_     (slow, default ~0.5 Hz)  — tape wow component
//   flutter_ (fast, default ~8 Hz)    — tape flutter component
//
// Signal flow per sample (see ModulatedDelayEffect contract):
//   1. Call tickModulation() ONCE PER SAMPLE (shared across all channels) to
//      advance both LFOs and receive the combined displacement in samples.
//   2. Call processSample(x, ch, displacement) ONCE PER CHANNEL for each audio channel.
//
// LFOs tick even when depths are zero so that phase advances consistently
// regardless of bypass state — the depth scalar multiplies the output to zero
// before it can influence the signal path.
//
// Depth-0 passthrough (FR-019): when BOTH wowDepth_ and flutterDepth_ are 0,
// processSample() uses a guarded bypass and returns x unchanged — exact
// passthrough, zero pitch modulation, output == input sample-for-sample.
// The delay line is still written (keeps the buffer current so there is no
// click if depths are raised later mid-stream) but the read is skipped.
//
// Buffer sizing: with kNominalSecs = 10 ms and kModRangeSecs = 5 ms (the
// per-LFO range at depth=1), the worst-case combined displacement is ±10 ms
// (both LFOs bipolar +1, both depths = 1.0 → +5+5 = +10 ms; or −10 ms).
// Read range: [0 ms, 20 ms] — strictly within the ~21 ms preallocated buffer.
// DelayLine also clamps delaySamples to [0, capacity-1] as a hard invariant
// (FR-021).
//
// RT-safety (FR-021, Constitution VI): all buffers preallocated in prepare();
// process path zero-heap, lock-free, O(1) bounded work.
//
// Platform independence (Constitution IV): standard library only; no desktop
// or MCU platform-specific headers.

namespace acfx {

class WowFlutterStage {
public:
    // Per-LFO displacement at depth=1 (±5 ms).  Combined worst case: ±10 ms.
    // Nominal center tap at 10 ms keeps reads in [0 ms, 20 ms] at any depth.
    static constexpr float kNominalSecs  = 0.010f;   // 10 ms center tap
    static constexpr float kModRangeSecs = 0.005f;   // 5 ms displacement per LFO @ depth=1

    // Audio stream must be stopped.  Allocates per-channel delay buffers and
    // initialises both LFOs.  numChannels is clamped to kMaxChannels.
    void prepare(float sampleRate, int numChannels) noexcept {
        sampleRate_  = sampleRate;
        numChannels_ = numChannels < kMaxChannels ? numChannels : kMaxChannels;

        nominalSamples_   = kNominalSecs  * sampleRate_;
        modRangeSamples_  = kModRangeSecs * sampleRate_;

        // Capacity: nominal + 2*range (max combined displacement) + 4 guard samples.
        // At 48 kHz: nominal=480, 2*range=480, capacity≈964 samples (~20 ms).
        const int capacity =
            static_cast<int>(nominalSamples_ + modRangeSamples_ * 2.0f) + 4;

        for (int ch = 0; ch < numChannels_; ++ch) {
            const std::size_t idx = static_cast<std::size_t>(ch);
            buffers_[idx].assign(static_cast<std::size_t>(capacity), 0.0f);
            lines_[idx].prepare(buffers_[idx].data(), capacity, sampleRate_);
        }

        wow_.prepare(sampleRate_);
        flutter_.prepare(sampleRate_);
        wow_.setRate(0.5f);    // default: 0.5 Hz wow
        flutter_.setRate(8.0f); // default: 8 Hz flutter
    }

    // Zero all delay buffers and reset LFO phases.  Audio stream must be stopped.
    void reset() noexcept {
        for (int ch = 0; ch < numChannels_; ++ch)
            lines_[static_cast<std::size_t>(ch)].reset();
        wow_.reset();
        flutter_.reset();
    }

    // Parameter setters — callable from the audio thread (RT-safe, no heap/locks).
    void setWowRate    (float hz)    noexcept { wow_.setRate(hz);   }
    void setWowDepth   (float depth) noexcept { wowDepth_     = depth; }
    void setFlutterRate(float hz)    noexcept { flutter_.setRate(hz); }
    void setFlutterDepth(float depth) noexcept { flutterDepth_ = depth; }

    // Tick both LFOs ONCE PER SAMPLE (shared across channels; modulation is
    // correlated / identical per channel, per spec Assumptions).  Returns the
    // combined displacement in samples for use in processSample() this period.
    // LFOs tick regardless of depth to maintain consistent phase.
    float tickModulation() noexcept {
        const float wowOut     = wow_.tick();
        const float flutterOut = flutter_.tick();
        // depth=0: term multiplies to zero, no contribution (FR-019 depth scalar).
        return (wowOut * wowDepth_ + flutterOut * flutterDepth_) * modRangeSamples_;
    }

    // Apply tape instability to one sample for one channel.
    // Always writes x to the delay line to keep the buffer current.
    // Guarded bypass (FR-019): if both depths are 0, returns x unchanged —
    // exact passthrough, no pitch modulation, output == input.
    // displacement: value returned by tickModulation() for this sample period.
    float processSample(float x, int ch, float displacement) noexcept {
        const std::size_t idx = static_cast<std::size_t>(ch);
        // Always write so the buffer stays current; enabling later does not read
        // stale/silent data.  A one-time latency-transition step on first enable
        // (bypass returns x directly; active returns a ~10 ms-old sample) is
        // inherent to the guarded-bypass design.
        lines_[idx].write(x);

        // Guarded bypass: both depths at zero → return input unchanged.
        if (wowDepth_ == 0.0f && flutterDepth_ == 0.0f)
            return x;

        // Read the modulated tap.  DelayLine clamps to [0, capacity-1] so the
        // read is always in range regardless of displacement magnitude (FR-021).
        return lines_[idx].readFractional(nominalSamples_ + displacement);
    }

private:
    static constexpr int kMaxChannels = 8;

    std::array<std::vector<float>, kMaxChannels> buffers_{};
    std::array<DelayLine,          kMaxChannels> lines_{};

    Lfo   wow_{};
    Lfo   flutter_{};

    float sampleRate_      = 48000.0f;
    int   numChannels_     = 0;
    float nominalSamples_  = 0.0f;
    float modRangeSamples_ = 0.0f;
    float wowDepth_        = 0.0f;   // default: bypass (FR-019)
    float flutterDepth_    = 0.0f;   // default: bypass (FR-019)
};

} // namespace acfx
