#pragma once

#include <array>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>

#include "dsp/audio-block.h"
#include "dsp/param-id.h"
#include "dsp/parameter.h"
#include "dsp/process-context.h"
#include "dsp/span.h"
#include "effects/tape-dynamics/tape-dynamics-core.h"
#include "effects/tape-dynamics/tape-dynamics-parameters.h"

// TapeDynamicsEffect — the host-facing wrapper adding the Effect contract on
// top of the TapeDynamicsCore composition kernel. Mirrors the shipped
// saturation-effect.h / compressor-effect.h idiom EXACTLY: no base class, no
// hot-path vtable; one constexpr ParameterDescriptor table as the single source
// of parameter truth (FR-019); a lock-free atomic cross-thread parameter handoff
// (FR-020). The wrapper owns per-channel TapeDynamicsCore instances for each
// supported oversampling factor (2x/4x/8x/16x) and dispatches at process() time.
// All parameters denormalize → TapeDynamicsCore setters (same boilerplate shape
// as CompressorEffect/SaturationEffect::applyPending).
//
// Thread-ownership boundary (identical to CompressorEffect/SaturationEffect):
//   - setParameter() is callable from ANY thread: it only publishes a lock-free
//     atomic pending value, consumed by the audio thread at the top of
//     process(), so edits never race process() (FR-020).
//   - prepare()/reset() mutate core coefficients directly and are NOT
//     synchronized against process(); they must be called only while the audio
//     stream is stopped — the adapter's responsibility, not enforced here
//     (FR-021).
//
// The descriptor table, option labels, and the dense-id static_assert live in
// tape-dynamics-parameters.h (split out per FR-028 to keep this wrapper within
// the ~300–500 line budget). This file keeps the Param index enum and exposes
// the table via the kParams alias.

namespace acfx {

class TapeDynamicsEffect {
public:
    // Stable parameter ids — the dense index into kParams, in data-model.md's
    // table order (specs/tape-dynamics/data-model.md "Entity — TapeDynamicsParameters").
    enum Param : std::uint8_t {
        kDrive = 0,
        kSaturation = 1,
        kWidth = 2,
        kSolver = 3,
        kOversampling = 4,
        kTrimEnabled = 5,
        kTrimAttack = 6,
        kTrimRelease = 7,
        kTrimAmount = 8,
        kMix = 9,
        kOutput = 10
    };

    static constexpr int kNumParams = 11;

    // The single source of parameter truth (FR-019), defined in
    // tape-dynamics-parameters.h and aliased here so every in-class reference
    // (kParams[kDrive], the static_assert, parameters()) reads naturally.
    // Row order matches the Param enum above.
    static constexpr const std::array<ParameterDescriptor, kNumParams>& kParams =
        kTapeDynamicsParams;

    TapeDynamicsEffect() noexcept {
        for (std::size_t i = 0; i < kNumParams; ++i) {
            pendingBits_[i].store(0u, std::memory_order_relaxed);
            pendingDirty_[i].store(0u, std::memory_order_relaxed);
        }
    }

    // Build-time guard: every descriptor in the table is valid (so a
    // malformed entry — e.g. a discrete param with count != choices.size() —
    // fails compilation, not the audio path).
    static_assert(
        [] {
            for (const ParameterDescriptor& d : kParams)
                if (!isValidDescriptor(d))
                    return false;
            return true;
        }(),
        "TapeDynamicsEffect parameter table violates a descriptor invariant "
        "(max>min; logarithmic => min>0; discrete => count>=2 and choices.size()==count)");

    static constexpr span<const ParameterDescriptor> parameters() noexcept { return kParams; }

    // Audio stream must be stopped — see the thread-ownership note above.
    void prepare(const ProcessContext& ctx) noexcept {
        sampleRate_ = static_cast<float>(ctx.sampleRate);
        numChannels_ = ctx.numChannels < kMaxChannels ? ctx.numChannels : kMaxChannels;

        // Prepare all three cores (dispatched at process() time via oversamplingIndex_).
        // Stub implementation: later tasks fill in oversampler-factor setup.
        core2x_.prepare(sampleRate_, numChannels_);
        core4x_.prepare(sampleRate_, numChannels_);
        core8x_.prepare(sampleRate_, numChannels_);

        applyAll();
    }

    // Audio stream must be stopped — see the thread-ownership note above.
    void reset() noexcept {
        core2x_.reset();
        core4x_.reset();
        core8x_.reset();
        applyAll();
    }

    // In-place Effect-concept process().
    void process(AudioBlock& io) noexcept {
        applyPending(); // consume cross-thread parameter edits on the audio thread
        processBlock(io);
    }

    // Publish a normalized 0..1 value for a parameter. Callable from any thread;
    // the audio thread applies it at the next process() (FR-020).
    void setParameter(ParamId id, float normalized) noexcept {
        const std::uint8_t i = id.value;
        if (i >= kNumParams)
            return;

        std::uint32_t bits = 0u;
        std::memcpy(&bits, &normalized, sizeof(normalized));

        pendingBits_[i].store(bits, std::memory_order_relaxed);
        pendingDirty_[i].store(1u, std::memory_order_release);
    }

private:
    static constexpr int kMaxChannels = 32;

    // Cores for each oversampling factor (Oversampler supports 2, 4, 8 only).
    TapeDynamicsCore<2> core2x_;
    TapeDynamicsCore<4> core4x_;
    TapeDynamicsCore<8> core8x_;

    float sampleRate_ = 48000.0f;
    int numChannels_ = 1;
    int oversamplingIndex_ = 2; // 0=2x, 1=4x, 2=8x; default 8x

    // Pending parameter state (lock-free, atomic handoff from setParameter()).
    std::array<std::atomic<std::uint32_t>, kNumParams> pendingBits_;
    std::array<std::atomic<std::uint32_t>, kNumParams> pendingDirty_;

    // Cached denormalized parameter values.
    float drive_ = 0.0f;
    float saturation_ = 1.0f;
    float width_ = 1.0f;
    std::uint8_t solver_ = 1; // default rk4
    std::uint8_t oversampling_ = 2; // default 8x (index 2)
    bool trimEnabled_ = false;
    float trimAttack_ = 0.01f;
    float trimRelease_ = 0.1f;
    float trimAmount_ = 0.5f;
    float mix_ = 1.0f;
    float output_ = 0.0f;

    // Consume pending parameter edits and denormalize into cached values,
    // then dispatch to the active core's setters (stub for now).
    void applyPending() noexcept {
        // Stub: later tasks implement parameter denormalization and core setter dispatch.
    }

    // Apply all cached parameter values to the active core (called by prepare/reset).
    void applyAll() noexcept {
        // Stub: later tasks apply all parameters to the active core.
    }

    // Process the audio block through the active core.
    void processBlock(AudioBlock& io) noexcept {
        // Stub: dispatch to the active core (oversamplingIndex_) and process
        // the audio block in place.
    }
};

} // namespace acfx
