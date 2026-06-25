#pragma once

#include <cstdint>

#include "Filters/svf.h" // DaisySP — pinned via CPM (cmake/dependencies.cmake)

// A thin, allocation-free, mode-selectable wrapper over DaisySP's proven Svf
// (research.md decision 1). DaisySP is a platform-independent pure-DSP math
// library, so wrapping it here keeps core/ free of any platform headers
// (Constitution IV). The wrapper owns mode selection + reset; the per-sample math
// is DaisySP's.

namespace acfx {

enum class SvfMode : std::uint8_t { lowpass, highpass, bandpass };

class SvfPrimitive {
public:
    // Prepare the filter for a sample rate. Clears internal state.
    void init(float sampleRate) noexcept {
        sampleRate_ = sampleRate;
        svf_.Init(sampleRate);
    }

    // f in Hz. DaisySP requires 0 < f < sampleRate/3; the caller (SvfEffect)
    // clamps cutoff into that range before calling.
    void setFreq(float hz) noexcept { svf_.SetFreq(hz); }

    // r in [0, 1] (DaisySP stability bound).
    void setRes(float r) noexcept { svf_.SetRes(r); }

    void setMode(SvfMode mode) noexcept { mode_ = mode; }
    SvfMode mode() const noexcept { return mode_; }

    // Re-initialize to a cleared-but-prepared state (DaisySP's Init clears state).
    void reset() noexcept { svf_.Init(sampleRate_); }

    // Process one sample, returning the currently-selected mode's output.
    // Allocation-free, bounded work (Constitution VI).
    float process(float in) noexcept {
        svf_.Process(in);
        switch (mode_) {
        case SvfMode::highpass:
            return svf_.High();
        case SvfMode::bandpass:
            return svf_.Band();
        case SvfMode::lowpass:
        default:
            return svf_.Low();
        }
    }

private:
    daisysp::Svf svf_{};
    float sampleRate_ = 48000.0f;
    SvfMode mode_ = SvfMode::lowpass;
};

} // namespace acfx
