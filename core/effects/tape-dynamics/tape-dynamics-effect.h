#pragma once

#include <array>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>

#include "dsp/audio-block.h"
#include "dsp/effect.h"
#include "dsp/param-id.h"
#include "dsp/parameter.h"
#include "dsp/process-context.h"
#include "dsp/span.h"
#include "effects/tape-dynamics/tape-dynamics-core.h"
#include "effects/tape-dynamics/tape-dynamics-parameters.h"
#include "primitives/nonlinear/hysteresis.h" // acfx::Solver

// TapeDynamicsEffect — the host-facing wrapper adding the Effect contract on
// top of the TapeDynamicsCore composition kernel. Mirrors the shipped
// saturation-effect.h / compressor-effect.h idiom EXACTLY: no base class, no
// hot-path vtable; one constexpr ParameterDescriptor table as the single source
// of parameter truth (FR-019); a lock-free atomic cross-thread parameter handoff
// (FR-020). The wrapper owns per-channel TapeDynamicsCore instances for each
// supported oversampling factor (2x/4x/8x) and dispatches at process() time.
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

        // Prepare ALL THREE cores (2x/4x/8x) — cheap; only the one selected by
        // the oversampling parameter is exercised per block (dispatched in
        // processBlock() via oversamplingIndex_). Keeping every factor
        // continuously prepared means a mid-session oversampling switch never
        // hits an unprepared core.
        core2x_.prepare(static_cast<double>(sampleRate_), numChannels_);
        core4x_.prepare(static_cast<double>(sampleRate_), numChannels_);
        core8x_.prepare(static_cast<double>(sampleRate_), numChannels_);

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
    // ALL THREE stay prepared and carry the same applied parameter state at all
    // times (applyPending()/applyAll() push every setter to every core); only
    // oversamplingIndex_ selects which one processBlock() actually runs, so a
    // mid-session oversampling switch never hits a stale or unprepared core.
    TapeDynamicsCore<2> core2x_;
    TapeDynamicsCore<4> core4x_;
    TapeDynamicsCore<8> core8x_;

    float sampleRate_ = 48000.0f;
    int numChannels_ = 1;
    int oversamplingIndex_ = 2; // 0=2x, 1=4x, 2=8x (kTapeDynamicsOversamplingLabels order); default 8x

    // Pending parameter state (lock-free, atomic handoff from setParameter()).
    std::array<std::atomic<std::uint32_t>, kNumParams> pendingBits_;
    std::array<std::atomic<std::uint32_t>, kNumParams> pendingDirty_;

    // Applied parameter state — mutated only in prepare/reset/applyPending (the
    // first two require a stopped stream; the third runs on the audio thread).
    // Defaults are the denormalized kParams defaults (mirrors CompressorEffect/
    // SaturationEffect).
    float driveDb_ = kParams[kDrive].defaultValue;
    float saturation_ = kParams[kSaturation].defaultValue;
    float width_ = kParams[kWidth].defaultValue;
    Solver solver_ = Solver::rk4;
    bool trimEnabled_ = false;
    float trimAttack_ = kParams[kTrimAttack].defaultValue;
    float trimRelease_ = kParams[kTrimRelease].defaultValue;
    float trimAmount_ = kParams[kTrimAmount].defaultValue;
    float mix_ = kParams[kMix].defaultValue;
    float outputDb_ = kParams[kOutput].defaultValue;

    // float <-> uint32 bit reinterpretation (allocation-free; a 4-byte memcpy is
    // a register move) so the cross-thread atomics are provably lock-free
    // (mirrors CompressorEffect/SaturationEffect's floatBits/bitsFloat).
    static float bitsFloat(std::uint32_t u) noexcept {
        float f = 0.0f;
        std::memcpy(&f, &u, sizeof(f));
        return f;
    }

    float pendingValue(Param p) const noexcept {
        return bitsFloat(pendingBits_[p].load(std::memory_order_relaxed));
    }

    // Discrete solver bucket index -> Solver enum (kTapeDynamicsSolverLabels
    // order: {"rk2", "rk4", "newtonRaphson"}).
    static Solver toSolver(float index) noexcept {
        switch (static_cast<int>(index)) {
        case 0:
            return Solver::rk2;
        case 2:
            return Solver::newtonRaphson;
        case 1:
        default:
            return Solver::rk4;
        }
    }

    // Discrete oversampling bucket index -> which prepared core processBlock()
    // dispatches to (kTapeDynamicsOversamplingLabels order: {"2x","4x","8x"}).
    // Clamped defensively — denormalize() already rounds into [0, count-1] for a
    // well-formed descriptor, but an out-of-range bucket must never index past
    // the three cores.
    static int toOversamplingIndex(float index) noexcept {
        int i = static_cast<int>(index);
        if (i < 0)
            i = 0;
        if (i > 2)
            i = 2;
        return i;
    }

    // Consume pending parameter edits and denormalize into cached values, then
    // dispatch to every core's matching setter (kOversampling instead selects
    // WHICH core processBlock() dispatches to).
    void applyPending() noexcept {
        if (pendingDirty_[kDrive].exchange(0u, std::memory_order_acquire)) {
            driveDb_ = denormalize(kParams[kDrive], pendingValue(kDrive));
            applyDrive();
        }
        if (pendingDirty_[kSaturation].exchange(0u, std::memory_order_acquire)) {
            saturation_ = denormalize(kParams[kSaturation], pendingValue(kSaturation));
            applySaturation();
        }
        if (pendingDirty_[kWidth].exchange(0u, std::memory_order_acquire)) {
            width_ = denormalize(kParams[kWidth], pendingValue(kWidth));
            applyWidth();
        }
        if (pendingDirty_[kSolver].exchange(0u, std::memory_order_acquire)) {
            solver_ = toSolver(denormalize(kParams[kSolver], pendingValue(kSolver)));
            applySolver();
        }
        if (pendingDirty_[kOversampling].exchange(0u, std::memory_order_acquire)) {
            oversamplingIndex_ =
                toOversamplingIndex(denormalize(kParams[kOversampling], pendingValue(kOversampling)));
            // Not a per-core setter: selects which already-prepared core
            // processBlock() runs (see processBlock()).
        }
        if (pendingDirty_[kTrimEnabled].exchange(0u, std::memory_order_acquire)) {
            trimEnabled_ = denormalize(kParams[kTrimEnabled], pendingValue(kTrimEnabled)) >= 0.5f;
            applyTrimEnabled();
        }
        if (pendingDirty_[kTrimAttack].exchange(0u, std::memory_order_acquire)) {
            trimAttack_ = denormalize(kParams[kTrimAttack], pendingValue(kTrimAttack));
            applyTrimAttack();
        }
        if (pendingDirty_[kTrimRelease].exchange(0u, std::memory_order_acquire)) {
            trimRelease_ = denormalize(kParams[kTrimRelease], pendingValue(kTrimRelease));
            applyTrimRelease();
        }
        if (pendingDirty_[kTrimAmount].exchange(0u, std::memory_order_acquire)) {
            trimAmount_ = denormalize(kParams[kTrimAmount], pendingValue(kTrimAmount));
            applyTrimAmount();
        }
        if (pendingDirty_[kMix].exchange(0u, std::memory_order_acquire)) {
            mix_ = denormalize(kParams[kMix], pendingValue(kMix));
            applyMix();
        }
        if (pendingDirty_[kOutput].exchange(0u, std::memory_order_acquire)) {
            outputDb_ = denormalize(kParams[kOutput], pendingValue(kOutput));
            applyOutput();
        }
    }

    // Each apply* pushes the cached value into ALL THREE cores (not just the
    // active one) so switching oversamplingIndex_ mid-session lands on a core
    // that already carries the full current parameter state.
    void applyDrive() noexcept {
        core2x_.setDrive(driveDb_);
        core4x_.setDrive(driveDb_);
        core8x_.setDrive(driveDb_);
    }
    void applySaturation() noexcept {
        core2x_.setSaturation(saturation_);
        core4x_.setSaturation(saturation_);
        core8x_.setSaturation(saturation_);
    }
    void applyWidth() noexcept {
        core2x_.setWidth(width_);
        core4x_.setWidth(width_);
        core8x_.setWidth(width_);
    }
    void applySolver() noexcept {
        core2x_.setSolver(solver_);
        core4x_.setSolver(solver_);
        core8x_.setSolver(solver_);
    }
    void applyTrimEnabled() noexcept {
        core2x_.setTrimEnabled(trimEnabled_);
        core4x_.setTrimEnabled(trimEnabled_);
        core8x_.setTrimEnabled(trimEnabled_);
    }
    void applyTrimAttack() noexcept {
        core2x_.setTrimAttack(trimAttack_);
        core4x_.setTrimAttack(trimAttack_);
        core8x_.setTrimAttack(trimAttack_);
    }
    void applyTrimRelease() noexcept {
        core2x_.setTrimRelease(trimRelease_);
        core4x_.setTrimRelease(trimRelease_);
        core8x_.setTrimRelease(trimRelease_);
    }
    void applyTrimAmount() noexcept {
        core2x_.setTrimAmount(trimAmount_);
        core4x_.setTrimAmount(trimAmount_);
        core8x_.setTrimAmount(trimAmount_);
    }
    void applyMix() noexcept {
        core2x_.setMix(mix_);
        core4x_.setMix(mix_);
        core8x_.setMix(mix_);
    }
    void applyOutput() noexcept {
        core2x_.setOutput(outputDb_);
        core4x_.setOutput(outputDb_);
        core8x_.setOutput(outputDb_);
    }

    // Apply all cached parameter values to every core (called by prepare()/
    // reset(), which re-prepare the composed primitives and so must
    // re-establish the full applied parameter state). oversamplingIndex_
    // needs no core push — it only selects which already-prepared core
    // processBlock() runs (see processBlock()).
    void applyAll() noexcept {
        applyDrive();
        applySaturation();
        applyWidth();
        applySolver();
        applyTrimEnabled();
        applyTrimAttack();
        applyTrimRelease();
        applyTrimAmount();
        applyMix();
        applyOutput();
    }

    // Dispatch the block to the core matching oversamplingIndex_ (2x/4x/8x)
    // and process every channel in place. RT-safe: no heap allocation, no
    // locks — just the bounded per-sample TapeDynamicsCore::processSample()
    // call already proven RT-safe in tape-dynamics-core.h.
    void processBlock(AudioBlock& io) noexcept {
        const int channels = io.numChannels() < numChannels_ ? io.numChannels() : numChannels_;
        const int samples = io.numSamples();
        switch (oversamplingIndex_) {
        case 0:
            runCore(core2x_, io, channels, samples);
            break;
        case 1:
            runCore(core4x_, io, channels, samples);
            break;
        case 2:
        default:
            runCore(core8x_, io, channels, samples);
            break;
        }
    }

    template <typename Core>
    static void runCore(Core& core, AudioBlock& io, int channels, int samples) noexcept {
        for (int ch = 0; ch < channels; ++ch) {
            float* x = io.channel(ch);
            for (int n = 0; n < samples; ++n)
                x[n] = core.processSample(x[n], ch);
        }
    }
};

#if defined(__cpp_concepts) && __cpp_concepts >= 201907L
static_assert(Effect<TapeDynamicsEffect>,
              "TapeDynamicsEffect must satisfy the Effect contract (dsp/effect.h)");
#endif

} // namespace acfx
