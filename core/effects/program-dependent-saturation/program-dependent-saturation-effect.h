#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>

#include "dsp/audio-block.h"
#include "dsp/effect.h"
#include "dsp/param-id.h"
#include "dsp/parameter.h"
#include "dsp/process-context.h"
#include "dsp/span.h"
#include "effects/program-dependent-saturation/program-dependent-saturation-core.h"

// ProgramDependentSaturationEffect — the host-facing wrapper SKELETON (T004):
// the Effect-contract surface + the lock-free atomic parameter-handoff
// scaffold. Mirrors the shipped core/effects/saturation/saturation-effect.h /
// core/effects/compressor/compressor-effect.h idiom EXACTLY: no base class,
// no vtable in the audio path; one constexpr ParameterDescriptor table as the
// (eventual) single source of parameter truth; per-channel
// ProgramDependentSaturationCore state; setParameter() publishes a lock-free
// atomic pending value from ANY thread, consumed on the audio thread at the
// top of process().
//
// DEFERRED TO LATER TASKS (contracts/program-dependent-saturation-effect-api.md
// "ProgramDependentSaturationEffect"):
//   - T011: the real ~24-row constexpr ParameterDescriptor table (data-model.md
//     "Parameter table") replacing the empty kParams placeholder below, plus
//     the full applyPending()/apply*() denormalize -> ProgramDependentSaturationCore-
//     setter dispatch (same boilerplate shape as SaturationEffect::applyPending).
//     If the full table pushes this file past the ~300-500 line budget, the
//     table + denormalize logic split into program-dependent-saturation-parameters.h
//     (FR-025/FR-028, mirroring compressor-parameters.h).
//   - T030: the DynamicPreset apply (a documented table of matrix configurations).
//   - T036: StereoLink (linked = common modulation from the max detector value
//     across channels; perChannel = independent).
//
// Thread-ownership boundary (identical to SaturationEffect/CompressorEffect):
//   - setParameter() is callable from ANY thread: it only publishes a
//     lock-free atomic pending value, consumed by the audio thread at the top
//     of process() (FR-017).
//   - prepare()/reset() mutate core state directly and are NOT synchronized
//     against process(); they must be called only while the audio stream is
//     stopped — the adapter's responsibility, not enforced here (FR-018).
//
// SCOPE NOTE: DynamicPreset and StereoLink are nested inside the class
// (rather than acfx-namespace-scope, as CompressorEffect's StereoLink is
// defined in compressor-parameters.h) so that a translation unit including
// both this header and compressor-effect.h never sees two competing
// `acfx::StereoLink` definitions — an enum redefinition is a hard compile
// error, not merely an ODR concern at link time. Nesting these two
// effect-local enums avoids that latent collision between two independently-
// evolving, same-named, same-shaped enums.

namespace acfx {

class ProgramDependentSaturationEffect {
public:
    // DynamicPreset (data-model.md "DynamicPreset") — an apply-once
    // convenience that writes the modulation matrix; `none` is the neutral /
    // orthogonality-baseline preset (all target depths 0, US3). Applied in T030.
    enum class DynamicPreset : std::uint8_t { none, opto, variMu, tapeComp };

    // StereoLink (data-model.md "StereoLink") — perChannel: independent
    // detection/modulation per channel; linked: one detector value (max
    // across channels) drives common modulation. Applied in T036.
    enum class StereoLink : std::uint8_t { perChannel, linked };

    // Stable parameter ids — the dense index into kParams, in data-model.md's
    // parameter-table order (contracts/program-dependent-saturation-effect-api.md
    // "ProgramDependentSaturationEffect").
    enum Param : std::uint8_t {
        kDrive = 0,
        kVoicing = 1,
        kTone = 2,
        kMix = 3,
        kOutput = 4,
        kBias = 5,
        kQuality = 6,
        kDetector = 7,
        kBallistics = 8,
        kAttack = 9,
        kRelease = 10,
        kDetection = 11,
        kDriveDepth = 12,
        kDriveCurve = 13,
        kBiasDepth = 14,
        kBiasCurve = 15,
        kToneDepth = 16,
        kToneCurve = 17,
        kMixDepth = 18,
        kMixCurve = 19,
        kDynamicPreset = 20,
        kExternalSidechain = 21,
        kScHpf = 22,
        kStereoLink = 23
    };

    ProgramDependentSaturationEffect() noexcept {
        for (std::size_t i = 0; i < kNumParams; ++i) {
            pendingBits_[i].store(0u, std::memory_order_relaxed);
            pendingDirty_[i].store(0u, std::memory_order_relaxed);
        }
    }

    // T011-deferred placeholder: the real ~24-row descriptor table (shapes +
    // ranges per data-model.md "Parameter table") is NOT duplicated here so
    // T011 stays the single place that owns it (see the header note above).
    // Declared here (ahead of the static_assert/parameters() below, mirroring
    // SaturationEffect's kParams placement) because a static_assert's
    // expression is evaluated at its point in the class body, unlike a member
    // function body, which gets complete-class context.
    static constexpr std::array<ParameterDescriptor, 0> kParams{};

    // Build-time guard: every descriptor in the table is valid (so a
    // malformed entry — e.g. a discrete param with count != choices.size() —
    // fails compilation, not the audio path). Trivially satisfied while
    // kParams is the T011-deferred empty placeholder (see the header note
    // above); stays meaningful once the real table lands.
    static_assert(
        [] {
            for (const ParameterDescriptor& d : kParams)
                if (!isValidDescriptor(d))
                    return false;
            return true;
        }(),
        "ProgramDependentSaturationEffect parameter table violates a descriptor invariant "
        "(max>min; logarithmic => min>0; discrete => count>=2 and choices.size()==count)");

    static constexpr span<const ParameterDescriptor> parameters() noexcept { return kParams; }

    // Audio stream must be stopped — see the thread-ownership note above.
    // Full body (table-driven applyAll()) deferred to T011; this stub already
    // prepares every per-channel core so prepare() is callable against a real
    // ProcessContext today.
    void prepare(const ProcessContext& ctx) noexcept {
        sampleRate_ = static_cast<float>(ctx.sampleRate);
        numChannels_ = ctx.numChannels < kMaxChannels ? ctx.numChannels : kMaxChannels;
        for (int ch = 0; ch < numChannels_; ++ch)
            cores_[static_cast<std::size_t>(ch)].prepare(sampleRate_);
    }

    // Audio stream must be stopped — see the thread-ownership note above.
    void reset() noexcept {
        for (int ch = 0; ch < numChannels_; ++ch)
            cores_[static_cast<std::size_t>(ch)].reset();
    }

    // Body deferred to T011 (data-model.md "Per-sample chain"): the real
    // per-target base+offset dispatch lives in
    // ProgramDependentSaturationCore::process(), still a T010 stub itself
    // (identity pass-through), so this wrapper loop is already the right
    // shape — it just has nothing but pass-through DSP behind it yet. Calls
    // newBlock() once per block per the contract (FR-010a); the argument
    // (block-representative normalized envelope) is a T011 concern.
    void process(AudioBlock& io) noexcept {
        applyPending(); // consume cross-thread parameter edits on the audio thread
        const int channels = io.numChannels() < numChannels_ ? io.numChannels() : numChannels_;
        const int samples = io.numSamples();
        for (int ch = 0; ch < channels; ++ch) {
            float* x = io.channel(ch);
            ProgramDependentSaturationCore& core = cores_[static_cast<std::size_t>(ch)];
            core.newBlock(0.0f);
            for (int n = 0; n < samples; ++n)
                x[n] = core.process(x[n], x[n]); // no external sidechain wiring yet
        }
    }

    // Publish a normalized 0..1 value for a parameter. Callable from any
    // thread; the audio thread applies it at the next process() (no
    // immediate core mutation here — keeps coefficient updates
    // single-threaded), mirroring SaturationEffect/CompressorEffect exactly.
    void setParameter(ParamId id, float normalized) noexcept {
        const std::uint8_t i = id.value;
        if (i >= kNumParams)
            return; // out-of-range id: a programming error; no silent state change
        pendingBits_[i].store(floatBits(normalized), std::memory_order_relaxed);
        pendingDirty_[i].store(1u, std::memory_order_release);
    }

private:
    static constexpr int kMaxChannels = 8;
    static constexpr std::size_t kNumParams = 24; // dense ids 0..23 (data-model.md)

    // float <-> uint32 bit reinterpretation (allocation-free; a 4-byte memcpy
    // is a register move) so the cross-thread atomics are provably lock-free.
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

    // Consume any parameter values published since the last block (audio
    // thread). The real denormalize -> ProgramDependentSaturationCore-setter
    // dispatch (mirroring SaturationEffect::applyPending, one branch per
    // Param) is T011 work; this stub only clears the dirty flags (reading
    // them via bitsFloat/pendingBits_ once T011 lands) so pending state never
    // grows stale in the meantime.
    void applyPending() noexcept {
        for (std::size_t i = 0; i < kNumParams; ++i) {
            if (pendingDirty_[i].exchange(0u, std::memory_order_acquire)) {
                (void)bitsFloat(pendingBits_[i].load(std::memory_order_relaxed));
            }
        }
    }

    std::array<ProgramDependentSaturationCore, kMaxChannels> cores_{};
    float sampleRate_ = 48000.0f;
    int numChannels_ = 0;

    // Cross-thread pending edits: any thread publishes, the audio thread
    // consumes. Stored as the float's bit pattern in a uint32 so the atomic
    // is provably lock-free (a bare std::atomic<float> can degrade to a
    // libcall on some embedded runtimes).
    std::array<std::atomic<std::uint32_t>, kNumParams> pendingBits_;
    std::array<std::atomic<std::uint32_t>, kNumParams> pendingDirty_;
    static_assert(std::atomic<std::uint32_t>::is_always_lock_free,
                  "pending-parameter atomics must be lock-free for RT safety");
};

#if defined(__cpp_concepts) && __cpp_concepts >= 201907L
static_assert(Effect<ProgramDependentSaturationEffect>,
              "ProgramDependentSaturationEffect must satisfy the Effect contract (dsp/effect.h)");
#endif

} // namespace acfx
