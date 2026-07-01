#pragma once

#include <array>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string_view>

#include "dsp/audio-block.h"
#include "dsp/param-id.h"
#include "dsp/parameter.h"
#include "dsp/process-context.h"
#include "dsp/span.h"
#include "labs/saturation/saturation-core.h"
#include "labs/saturation/saturation-voicings.h"

// SaturationEffect — the host-facing wrapper that adds the Effect contract on top
// of the shipped SaturationCore kernel (US3). Mirrors the shipped
// core/effects/svf/svf-effect.h idiom EXACTLY: no base class, no vtable in the hot
// path; one constexpr ParameterDescriptor table as the single source of parameter
// truth (SC-006 / FR-003); a lock-free atomic cross-thread parameter handoff. The
// wrapper owns per-channel SaturationCore state and is allocation-free in process().
//
// Thread-ownership boundary (identical to SvfEffect):
//   - setParameter() may be called from ANY thread (a UI loop, a MIDI callback,
//     an MCU main loop). It only publishes a lock-free atomic pending value; the
//     audio thread consumes pending values at the top of process(). So parameter
//     edits never race process() — the wrapper encodes that handoff itself.
//   - prepare()/reset() DO mutate core coefficients directly and are NOT
//     synchronized against process(). They must be called only while the audio
//     stream is stopped (before start, or during a device change with audio
//     paused) — the standard prepare/process lifecycle already implies this, and
//     that quiescence is the adapter's responsibility, not something the wrapper
//     can enforce.
//
// PARAMETER RANGES ARE A TUNING-PASS OPEN QUESTION. The descriptor shapes below
// (kinds/units/skews/labels) are normative (saturation-api.md "SaturationEffect");
// the exact numeric ranges (drive 0..+48 dB, output -24..+24 dB, etc.) are
// defensible placeholders, not yet validated against a reference measurement —
// they are the tuning-pass open question carried in data-model.md, mirroring the
// per-voicing numbers in saturation-voicings.h.

namespace acfx {

class SaturationEffect {
public:
    // Stable parameter ids — the dense index into kParams
    // (saturation-api.md "SaturationEffect").
    enum Param : std::uint8_t {
        kDrive = 0,
        kVoicing = 1,
        kTone = 2,
        kMix = 3,
        kOutput = 4,
        kBias = 5,
        kQuality = 6
    };

    // Option labels for the discrete voicing parameter (single source of truth).
    static constexpr std::array<std::string_view, 4> kVoicingLabels = {
        {"softClip", "tape", "console", "tubePreamp"}};

    // Option labels for the discrete quality parameter. 'oversampled' is
    // intentionally NOT user-selectable yet (FR-015) — the reserved seam is absent
    // here even though SaturationQuality declares it.
    static constexpr std::array<std::string_view, 2> kQualityLabels = {{"naive", "adaa"}};

    // The single source of parameter truth (SC-006). Shapes are normative; ranges
    // are the tuning-pass OPEN QUESTION (see the header note above):
    //   drive:   dB pre-gain into the nonlinearity, 0..+48 dB (0 dB = unity)
    //   voicing: discrete {softClip, tape, console, tubePreamp}
    //   tone:    linear post tilt, -1..+1 (0 = transparent)
    //   mix:     linear dry/wet, 0..1 (1 = fully wet)
    //   output:  dB makeup trim, -24..+24 dB (0 dB = unity)
    //   bias:    linear asymmetry, -1..+1 (0 = symmetric)
    //   quality: discrete {naive, adaa}
    static constexpr std::array<ParameterDescriptor, 7> kParams = {{
        {ParamId{kDrive}, "drive", ParamUnit::decibels, 0.0f, 48.0f, 0.0f,
         ParamSkew::linear, ParamKind::continuous, 0},
        {ParamId{kVoicing}, "voicing", ParamUnit::none, 0.0f, 3.0f, 0.0f,
         ParamSkew::linear, ParamKind::discrete, 4, kVoicingLabels},
        {ParamId{kTone}, "tone", ParamUnit::none, -1.0f, 1.0f, 0.0f,
         ParamSkew::linear, ParamKind::continuous, 0},
        {ParamId{kMix}, "mix", ParamUnit::none, 0.0f, 1.0f, 1.0f,
         ParamSkew::linear, ParamKind::continuous, 0},
        {ParamId{kOutput}, "output", ParamUnit::decibels, -24.0f, 24.0f, 0.0f,
         ParamSkew::linear, ParamKind::continuous, 0},
        {ParamId{kBias}, "bias", ParamUnit::none, -1.0f, 1.0f, 0.0f,
         ParamSkew::linear, ParamKind::continuous, 0},
        {ParamId{kQuality}, "quality", ParamUnit::none, 0.0f, 1.0f, 1.0f,
         ParamSkew::linear, ParamKind::discrete, 2, kQualityLabels},
    }};

    SaturationEffect() noexcept {
        for (std::size_t i = 0; i < kNumParams; ++i) {
            pendingBits_[i].store(0u, std::memory_order_relaxed);
            pendingDirty_[i].store(0u, std::memory_order_relaxed);
        }
    }

    // Build-time guard: every descriptor in the table is valid (so a malformed
    // entry — e.g. a discrete param with count != choices.size() — fails
    // compilation, not the audio path).
    static_assert(
        [] {
            for (const ParameterDescriptor& d : kParams)
                if (!isValidDescriptor(d))
                    return false;
            return true;
        }(),
        "SaturationEffect parameter table violates a descriptor invariant "
        "(max>min; logarithmic => min>0; discrete => count>=2 and choices.size()==count)");

    static constexpr span<const ParameterDescriptor> parameters() noexcept { return kParams; }

    // Audio stream must be stopped — see the thread-ownership note above.
    void prepare(const ProcessContext& ctx) noexcept {
        sampleRate_ = static_cast<float>(ctx.sampleRate);
        numChannels_ = ctx.numChannels < kMaxChannels ? ctx.numChannels : kMaxChannels;
        for (int ch = 0; ch < numChannels_; ++ch)
            cores_[static_cast<std::size_t>(ch)].prepare(sampleRate_);
        applyAll();
    }

    // Audio stream must be stopped — see the thread-ownership note above.
    void reset() noexcept {
        for (int ch = 0; ch < numChannels_; ++ch)
            cores_[static_cast<std::size_t>(ch)].reset();
        applyAll();
    }

    void process(AudioBlock& io) noexcept {
        applyPending(); // consume cross-thread parameter edits on the audio thread
        const int channels = io.numChannels() < numChannels_ ? io.numChannels() : numChannels_;
        const int samples = io.numSamples();
        for (int ch = 0; ch < channels; ++ch) {
            float* x = io.channel(ch);
            SaturationCore& core = cores_[static_cast<std::size_t>(ch)];
            for (int n = 0; n < samples; ++n)
                x[n] = core.process(x[n]);
        }
    }

    // Publish a normalized 0..1 value for a parameter. Callable from any thread;
    // the audio thread applies it at the next process() (no immediate core
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
    static constexpr std::size_t kNumParams = 7;

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

    // dB -> linear gain. SaturationCore::setDrive/setOutput take a LINEAR gain
    // (saturation-core.h), so the dB-valued drive/output descriptors are converted
    // here before the setter call.
    static float dbToGain(float db) noexcept { return std::pow(10.0f, db / 20.0f); }

    // Discrete bucket index -> voicing enum (kVoicingLabels order).
    static SaturationVoicing toVoicing(float index) noexcept {
        switch (static_cast<int>(index)) {
        case 1:
            return SaturationVoicing::tape;
        case 2:
            return SaturationVoicing::console;
        case 3:
            return SaturationVoicing::tubePreamp;
        case 0:
        default:
            return SaturationVoicing::softClip;
        }
    }

    // Discrete bucket index -> quality enum (kQualityLabels order; 'oversampled'
    // is not user-selectable, so it is never produced here).
    static SaturationQuality toQuality(float index) noexcept {
        switch (static_cast<int>(index)) {
        case 1:
            return SaturationQuality::adaa;
        case 0:
        default:
            return SaturationQuality::naive;
        }
    }

    float pendingValue(Param p) const noexcept {
        return bitsFloat(pendingBits_[p].load(std::memory_order_relaxed));
    }

    // Apply any parameter values published since the last block (audio thread).
    // Each dirty param is denormalized into its REAL value (dB->gain for
    // drive/output, index->enum for voicing/quality, direct for the linear params)
    // before the matching SaturationCore setter is pushed to every channel.
    void applyPending() noexcept {
        if (pendingDirty_[kDrive].exchange(0u, std::memory_order_acquire)) {
            driveGain_ = dbToGain(denormalize(kParams[kDrive], pendingValue(kDrive)));
            applyDrive();
        }
        if (pendingDirty_[kVoicing].exchange(0u, std::memory_order_acquire)) {
            // T016: this runs SaturationCore::setVoicing() -> configureShapers()
            // -> ADAAWaveshaper::setShape() ON THE AUDIO THREAD (setShape is not
            // noexcept -- it throws for a shape with no antiderivative). This is
            // safe ONLY because every SaturationVoicing's shape is ADAA-safe, an
            // invariant locked by tests/core/saturation-voicings-test.cpp and
            // documented at SaturationCore::configureShapers() (saturation-core.h).
            voicing_ = toVoicing(denormalize(kParams[kVoicing], pendingValue(kVoicing)));
            applyVoicing();
        }
        if (pendingDirty_[kTone].exchange(0u, std::memory_order_acquire)) {
            tone_ = denormalize(kParams[kTone], pendingValue(kTone));
            applyTone();
        }
        if (pendingDirty_[kMix].exchange(0u, std::memory_order_acquire)) {
            mix_ = denormalize(kParams[kMix], pendingValue(kMix));
            applyMix();
        }
        if (pendingDirty_[kOutput].exchange(0u, std::memory_order_acquire)) {
            outputGain_ = dbToGain(denormalize(kParams[kOutput], pendingValue(kOutput)));
            applyOutput();
        }
        if (pendingDirty_[kBias].exchange(0u, std::memory_order_acquire)) {
            bias_ = denormalize(kParams[kBias], pendingValue(kBias));
            applyBias();
        }
        if (pendingDirty_[kQuality].exchange(0u, std::memory_order_acquire)) {
            quality_ = toQuality(denormalize(kParams[kQuality], pendingValue(kQuality)));
            applyQuality();
        }
    }

    void applyDrive() noexcept {
        for (int ch = 0; ch < numChannels_; ++ch)
            cores_[static_cast<std::size_t>(ch)].setDrive(driveGain_);
    }
    void applyVoicing() noexcept {
        for (int ch = 0; ch < numChannels_; ++ch)
            cores_[static_cast<std::size_t>(ch)].setVoicing(voicing_);
    }
    void applyTone() noexcept {
        for (int ch = 0; ch < numChannels_; ++ch)
            cores_[static_cast<std::size_t>(ch)].setTone(tone_);
    }
    void applyMix() noexcept {
        for (int ch = 0; ch < numChannels_; ++ch)
            cores_[static_cast<std::size_t>(ch)].setMix(mix_);
    }
    void applyOutput() noexcept {
        for (int ch = 0; ch < numChannels_; ++ch)
            cores_[static_cast<std::size_t>(ch)].setOutput(outputGain_);
    }
    void applyBias() noexcept {
        for (int ch = 0; ch < numChannels_; ++ch)
            cores_[static_cast<std::size_t>(ch)].setBias(bias_);
    }
    void applyQuality() noexcept {
        for (int ch = 0; ch < numChannels_; ++ch)
            cores_[static_cast<std::size_t>(ch)].setQuality(quality_);
    }
    void applyAll() noexcept {
        applyDrive();
        applyVoicing();
        applyTone();
        applyMix();
        applyOutput();
        applyBias();
        applyQuality();
    }

    std::array<SaturationCore, kMaxChannels> cores_{};
    float sampleRate_ = 48000.0f;
    int numChannels_ = 0;

    // Applied parameter state — mutated only in prepare/reset/applyPending (the
    // first two require a stopped stream; the third runs on the audio thread).
    // Defaults are the denormalized kParams defaults and match SaturationCore's
    // own defaults (unity drive/output, symmetric bias, softClip, adaa, flat tone,
    // fully wet mix).
    float driveGain_ = dbToGain(kParams[kDrive].defaultValue);
    SaturationVoicing voicing_ = SaturationVoicing::softClip;
    float tone_ = kParams[kTone].defaultValue;
    float mix_ = kParams[kMix].defaultValue;
    float outputGain_ = dbToGain(kParams[kOutput].defaultValue);
    float bias_ = kParams[kBias].defaultValue;
    SaturationQuality quality_ = SaturationQuality::adaa;

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
