#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string_view>

#include "dsp/audio-block.h"
#include "dsp/param-id.h"
#include "dsp/parameter.h"
#include "dsp/process-context.h"
#include "dsp/span.h"
#include "primitives/svf-primitive.h"

// The State-Variable Filter effect — the one effect proving the cross-platform
// spine. Satisfies the Effect contract (no base class, no vtable in the hot
// path). One constexpr parameter table (cutoff/resonance/mode) drives every
// adapter (FR-003). Per-channel DaisySP Svf state, allocation-free process.
//
// Thread-ownership boundary:
//   - setParameter() may be called from ANY thread (a UI loop, a MIDI callback,
//     an MCU main loop). It only publishes a lock-free atomic pending value; the
//     audio thread consumes pending values at the top of process(). So parameter
//     edits never race process() — the core encodes that handoff itself.
//   - prepare()/reset() DO mutate filter coefficients directly and are NOT
//     synchronized against process(). They must be called only while the audio
//     stream is stopped (before start, or during a device change with audio
//     paused) — the standard prepare/process lifecycle already implies this, and
//     that quiescence is the adapter's responsibility, not something the core can
//     enforce.

namespace acfx {

class SvfEffect {
public:
    // Stable parameter ids — the dense index into kParams.
    enum Param : std::uint8_t { kCutoff = 0, kResonance = 1, kMode = 2 };

    // Option labels for the mode discrete parameter (single source of truth).
    static constexpr std::array<std::string_view, 3> kModeLabels = {{"lowpass", "highpass", "bandpass"}};

    // The single source of parameter truth (SC-006). cutoff: log Hz; resonance:
    // linear 0..1; mode: discrete {lowpass, highpass, bandpass}.
    static constexpr std::array<ParameterDescriptor, 3> kParams = {{
        {ParamId{kCutoff}, "cutoff", ParamUnit::hz, 20.0f, 20000.0f, 1000.0f,
         ParamSkew::logarithmic, ParamKind::continuous, 0},
        {ParamId{kResonance}, "resonance", ParamUnit::none, 0.0f, 1.0f, 0.0f,
         ParamSkew::linear, ParamKind::continuous, 0},
        {ParamId{kMode}, "mode", ParamUnit::none, 0.0f, 2.0f, 0.0f,
         ParamSkew::linear, ParamKind::discrete, 3, kModeLabels},
    }};

    SvfEffect() noexcept {
        for (std::size_t i = 0; i < kNumParams; ++i) {
            pendingBits_[i].store(0u, std::memory_order_relaxed);
            pendingDirty_[i].store(0u, std::memory_order_relaxed);
        }
    }

    // Build-time guard: every descriptor in the table is valid (so a malformed
    // entry — e.g. a log param with min<=0 — fails compilation, not the audio path).
    static_assert(
        [] {
            for (const ParameterDescriptor& d : kParams)
                if (!isValidDescriptor(d))
                    return false;
            return true;
        }(),
        "SvfEffect parameter table violates a descriptor invariant "
        "(max>min; logarithmic => min>0; discrete => count>=2 and choices.size()==count)");

    static constexpr span<const ParameterDescriptor> parameters() noexcept { return kParams; }

    // Audio stream must be stopped — see the thread-ownership note above.
    void prepare(const ProcessContext& ctx) noexcept {
        sampleRate_ = static_cast<float>(ctx.sampleRate);
        numChannels_ = ctx.numChannels < kMaxChannels ? ctx.numChannels : kMaxChannels;
        for (int ch = 0; ch < numChannels_; ++ch)
            filters_[static_cast<std::size_t>(ch)].init(sampleRate_);
        applyAll();
    }

    // Audio stream must be stopped — see the thread-ownership note above.
    void reset() noexcept {
        for (int ch = 0; ch < numChannels_; ++ch)
            filters_[static_cast<std::size_t>(ch)].reset();
        applyAll();
    }

    void process(AudioBlock& io) noexcept {
        applyPending(); // consume cross-thread parameter edits on the audio thread
        const int channels = io.numChannels() < numChannels_ ? io.numChannels() : numChannels_;
        const int samples = io.numSamples();
        for (int ch = 0; ch < channels; ++ch) {
            float* x = io.channel(ch);
            SvfPrimitive& f = filters_[static_cast<std::size_t>(ch)];
            for (int n = 0; n < samples; ++n)
                x[n] = f.process(x[n]);
        }
    }

    // Publish a normalized 0..1 value for a parameter. Callable from any thread;
    // the audio thread applies it at the next process() (no immediate filter
    // mutation here — that keeps coefficient updates single-threaded).
    void setParameter(ParamId id, float normalized) noexcept {
        const std::uint8_t i = id.value;
        if (i >= kNumParams)
            return; // out-of-range id: a programming error; no silent state change
        pendingBits_[i].store(floatBits(normalized), std::memory_order_relaxed);
        pendingDirty_[i].store(1u, std::memory_order_release);
    }

private:
    static constexpr int kMaxChannels = 8;
    static constexpr std::size_t kNumParams = 3;

    // float <-> uint32 bit reinterpretation (allocation-free; a 4-byte memcpy is a
    // register move). Lets the cross-thread atomics be provably lock-free.
    static std::uint32_t floatBits(float f) noexcept {
        std::uint32_t u = 0;
        std::memcpy(&u, &f, sizeof(u));
        return u;
    }
    static float bitsFloat(std::uint32_t u) noexcept {
        float f = 0.0f;
        std::memcpy(&f, &u, sizeof(f));
        return f;
    }

    static SvfMode toMode(float index) noexcept {
        switch (static_cast<int>(index)) {
        case 1:
            return SvfMode::highpass;
        case 2:
            return SvfMode::bandpass;
        case 0:
        default:
            return SvfMode::lowpass;
        }
    }

    float pendingValue(Param p) const noexcept {
        return bitsFloat(pendingBits_[p].load(std::memory_order_relaxed));
    }

    // Apply any parameter values published since the last block (audio thread).
    void applyPending() noexcept {
        if (pendingDirty_[kCutoff].exchange(0u, std::memory_order_acquire)) {
            cutoffHz_ = denormalize(kParams[kCutoff], pendingValue(kCutoff));
            applyCutoff();
        }
        if (pendingDirty_[kResonance].exchange(0u, std::memory_order_acquire)) {
            resonance_ = denormalize(kParams[kResonance], pendingValue(kResonance));
            applyResonance();
        }
        if (pendingDirty_[kMode].exchange(0u, std::memory_order_acquire)) {
            mode_ = toMode(denormalize(kParams[kMode], pendingValue(kMode)));
            applyMode();
        }
    }

    // DaisySP's Svf requires cutoff strictly below sampleRate/3; clamp into a safe
    // band just under that bound (and never below the descriptor minimum).
    float clampedCutoff() const noexcept {
        const float maxFreq = sampleRate_ * 0.32f;
        float f = cutoffHz_;
        if (f > maxFreq)
            f = maxFreq;
        if (f < 20.0f)
            f = 20.0f;
        return f;
    }

    void applyCutoff() noexcept {
        const float f = clampedCutoff();
        for (int ch = 0; ch < numChannels_; ++ch)
            filters_[static_cast<std::size_t>(ch)].setFreq(f);
    }
    void applyResonance() noexcept {
        for (int ch = 0; ch < numChannels_; ++ch)
            filters_[static_cast<std::size_t>(ch)].setRes(resonance_);
    }
    void applyMode() noexcept {
        for (int ch = 0; ch < numChannels_; ++ch)
            filters_[static_cast<std::size_t>(ch)].setMode(mode_);
    }
    void applyAll() noexcept {
        applyCutoff();
        applyResonance();
        applyMode();
    }

    std::array<SvfPrimitive, kMaxChannels> filters_{};
    float sampleRate_ = 48000.0f;
    int numChannels_ = 0;

    // Applied parameter state — mutated only in prepare/reset/applyPending (the
    // first two require a stopped stream; the third runs on the audio thread).
    float cutoffHz_ = kParams[kCutoff].defaultValue;
    float resonance_ = kParams[kResonance].defaultValue;
    SvfMode mode_ = SvfMode::lowpass;

    // Cross-thread pending edits: any thread publishes, the audio thread consumes.
    // Stored as the float's bit pattern in a uint32 so the atomic is provably
    // lock-free on every target (a bare std::atomic<float> can degrade to a libcall
    // on some embedded runtimes).
    std::array<std::atomic<std::uint32_t>, kNumParams> pendingBits_;
    std::array<std::atomic<std::uint32_t>, kNumParams> pendingDirty_;
    static_assert(std::atomic<std::uint32_t>::is_always_lock_free,
                  "pending-parameter atomics must be lock-free for RT safety");
};

} // namespace acfx
