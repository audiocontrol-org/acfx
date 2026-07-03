#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>

#include "dsp/audio-block.h"
#include "dsp/effect.h"
#include "dsp/param-id.h"
#include "dsp/parameter.h"
#include "dsp/process-context.h"
#include "dsp/span.h"
#include "effects/program-dependent-saturation/program-dependent-saturation-core.h"
#include "effects/program-dependent-saturation/program-dependent-saturation-parameters.h"
#include "effects/program-dependent-saturation/program-dependent-saturation-presets.h"
#include "effects/program-dependent-saturation/program-dependent-saturation-routing.h"

// ProgramDependentSaturationEffect — the host-facing wrapper adding the Effect
// contract on top of the ProgramDependentSaturationCore composition kernel.
// Mirrors the shipped saturation-effect.h / compressor-effect.h idiom EXACTLY:
// no base class, no hot-path vtable; one constexpr ParameterDescriptor table as
// the single source of parameter truth (FR-003); a lock-free atomic cross-thread
// parameter handoff (FR-017). The wrapper owns per-channel
// ProgramDependentSaturationCore state and is allocation-free in process().
// All 24 parameters denormalize -> ProgramDependentSaturationCore setter (same
// boilerplate shape as SaturationEffect::applyPending).
//
// TONE PER-BLOCK (Decision 4): tone is modulated once per block via
// core.newBlock(), fed the core's OWN most-recent normalized envelope from the
// previous block (core.lastNorm()) — a deliberate one-block (control-rate) lag
// (block N's tilt is driven by block N-1's final level), inaudible for a slow
// spectral tilt. First block: lastNorm() is 0 (silence).
//
// EXTERNAL KEY (FR-012, T034) + STEREO LINKING (FR-013, T036): process(io) is
// keyless; process(io, sidechain) routes the matching sidechain channel as the
// key (gated by the externalSidechain flag). perChannel detects+modulates each
// channel independently; linked shares ONE detection driving a common modulation
// across channels (image-stable). Both live in the routing free functions in
// program-dependent-saturation-routing.h (FR-025 split); processBlock() forwards.
//
// DYNAMIC PRESETS (FR-014, T030): applyDynamicPreset() writes the documented
// modulation matrix (program-dependent-saturation-presets.h) to every core;
// `none` is the neutral baseline no-op (equal to the constructor defaults, so it
// never clobbers a hand-dialed matrix). presetConfig() exposes the same table so
// a test can assert the realized matrix equals the documentation.
//
// Thread-ownership (identical to SaturationEffect/CompressorEffect):
// setParameter() is callable from ANY thread (publishes a lock-free atomic
// pending value, consumed on the audio thread at the top of process(), FR-017);
// prepare()/reset() mutate core state directly and must be called only while
// the audio stream is stopped — the adapter's responsibility (FR-018).
//
// SPLIT ACROSS FILES (FR-025, budget): the descriptor table, option-label
// arrays, dense-id static_assert, and the stateless scalar/enum-converter
// helpers (floatBits/bitsFloat/dbToGain/toVoicing/toQuality/toDetectMode/
// toBallistics/toDetection/toModCurve) live in program-dependent-saturation-
// parameters.h. This file keeps the `Param` enum, the two effect-local enums
// (DynamicPreset/StereoLink) and their converters, `kParams`, and every method
// touching per-channel core state.

namespace acfx {

class ProgramDependentSaturationEffect {
public:
    // DynamicPreset (data-model.md "DynamicPreset") — an apply-once convenience
    // that writes the modulation matrix (applyDynamicPreset); `none` is the
    // neutral / orthogonality-baseline preset (all target depths 0).
    enum class DynamicPreset : std::uint8_t { none, opto, variMu, tapeComp };

    // StereoLink (data-model.md "StereoLink") — perChannel: independent
    // detection/modulation per channel; linked: one shared detection (max across
    // channels) drives common modulation (see the routing header).
    enum class StereoLink : std::uint8_t { perChannel, linked };

    // Stable parameter ids — the dense index into kParams, in data-model.md's
    // parameter-table order.
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

    // The single source of parameter truth (FR-003), defined in
    // program-dependent-saturation-parameters.h and aliased here so every
    // in-class reference reads naturally. Row order matches the Param enum above.
    static constexpr const std::array<ParameterDescriptor, 24>& kParams = kPdsParams;

    ProgramDependentSaturationEffect() noexcept {
        for (std::size_t i = 0; i < kNumParams; ++i) {
            pendingBits_[i].store(0u, std::memory_order_relaxed);
            pendingDirty_[i].store(0u, std::memory_order_relaxed);
        }
    }

    // Build-time guard: every descriptor in the table is valid (so a malformed
    // entry fails compilation, not the audio path).
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

    // The documented modulation-matrix configuration a DynamicPreset writes
    // (program-dependent-saturation-presets.h). Exposed so a test (T029) can
    // assert the realized matrix equals the documented preset definition (SC-008)
    // and so the preset table is a single source of truth shared with
    // writePreset(). Row order == the DynamicPreset enum, so the cast indexes it.
    static constexpr const PdsPresetConfig& presetConfig(DynamicPreset preset) noexcept {
        return kPdsPresetConfigs[static_cast<std::size_t>(preset)];
    }

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

    // In-place, keyless Effect-concept process() (each channel's main input
    // doubles as the key); StereoLink is honored (FR-013); see processBlock().
    void process(AudioBlock& io) noexcept {
        applyPending(); // consume cross-thread parameter edits on the audio thread
        processBlock(io, nullptr);
    }

    // In-place with an EXTERNAL sidechain key (FR-012): when externalSidechain is
    // ON the detector reads the matching sidechain channel, while the saturation
    // still applies to `io`. Both overloads consume pending params at the top and
    // are allocation-free / lock-free. Mapping + linking rules: keyAt()/processBlock().
    void process(AudioBlock& io, const AudioBlock& sidechain) noexcept {
        applyPending();
        processBlock(io, &sidechain);
    }

    // Publish a normalized 0..1 value for a parameter. Callable from any thread;
    // the audio thread applies it at the next process() (no immediate core
    // mutation — keeps coefficient updates single-threaded) (FR-017).
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

    // Shared per-block routing for both process() overloads — external-key
    // mapping (T034) + StereoLink perChannel/linked dispatch (T036). The routing
    // logic (keyAt + the linked/perChannel passes) is a free function over the
    // core array in program-dependent-saturation-routing.h (FR-025 split); this
    // just forwards the effect's channel count + sidechain/link flags.
    void processBlock(AudioBlock& io, const AudioBlock* sc) noexcept {
        pds::processBlock(cores_, numChannels_, externalKey_,
                          stereoLink_ == StereoLink::linked, io, sc);
    }

    // floatBits/bitsFloat/dbToGain and the other index->enum converters live in
    // program-dependent-saturation-parameters.h as free acfx functions (moved
    // out per FR-025/FR-028); unqualified calls below resolve to those via
    // namespace-scope lookup. toDynamicPreset/toStereoLink stay here because
    // they return enums NESTED in this class (see that header's SCOPE NOTE).
    static DynamicPreset toDynamicPreset(float index) noexcept {
        switch (static_cast<int>(index)) {
        case 1: return DynamicPreset::opto;
        case 2: return DynamicPreset::variMu;
        case 3: return DynamicPreset::tapeComp;
        default: return DynamicPreset::none;
        }
    }
    static StereoLink toStereoLink(float index) noexcept {
        return static_cast<int>(index) == 1 ? StereoLink::linked : StereoLink::perChannel;
    }

    float pendingValue(Param p) const noexcept {
        return bitsFloat(pendingBits_[p].load(std::memory_order_relaxed));
    }

    // Apply any parameter values published since the last block (audio thread).
    // Each dirty param is denormalized into its REAL value before the matching
    // ProgramDependentSaturationCore setter is pushed to every channel. Mirrors
    // SaturationEffect/CompressorEffect: one branch per Param.
    void applyPending() noexcept {
        // Preset FIRST so a same-block individual edit (applied below) OVERRIDES it
        // (US9 — a starting point, not a lock); writePreset() bakes the cached fields.
        if (pendingDirty_[kDynamicPreset].exchange(0u, std::memory_order_acquire)) {
            dynamicPreset_ =
                toDynamicPreset(denormalize(kParams[kDynamicPreset], pendingValue(kDynamicPreset)));
            applyDynamicPreset();
        }
        if (pendingDirty_[kDrive].exchange(0u, std::memory_order_acquire)) {
            driveDb_ = denormalize(kParams[kDrive], pendingValue(kDrive));
            applyDrive();
        }
        if (pendingDirty_[kVoicing].exchange(0u, std::memory_order_acquire)) {
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
        if (pendingDirty_[kDetector].exchange(0u, std::memory_order_acquire)) {
            detectMode_ = toDetectMode(denormalize(kParams[kDetector], pendingValue(kDetector)));
            applyDetector();
        }
        if (pendingDirty_[kBallistics].exchange(0u, std::memory_order_acquire)) {
            ballistics_ = toBallistics(denormalize(kParams[kBallistics], pendingValue(kBallistics)));
            applyBallistics();
        }
        if (pendingDirty_[kAttack].exchange(0u, std::memory_order_acquire)) {
            attackSeconds_ = denormalize(kParams[kAttack], pendingValue(kAttack)) * kMsToSec;
            applyAttack();
        }
        if (pendingDirty_[kRelease].exchange(0u, std::memory_order_acquire)) {
            releaseSeconds_ = denormalize(kParams[kRelease], pendingValue(kRelease)) * kMsToSec;
            applyRelease();
        }
        if (pendingDirty_[kDetection].exchange(0u, std::memory_order_acquire)) {
            detection_ = toDetection(denormalize(kParams[kDetection], pendingValue(kDetection)));
            applyDetection();
        }
        if (pendingDirty_[kDriveDepth].exchange(0u, std::memory_order_acquire)) {
            driveDepth_ = denormalize(kParams[kDriveDepth], pendingValue(kDriveDepth));
            applyDepth(ModTarget::drive, driveDepth_);
        }
        if (pendingDirty_[kDriveCurve].exchange(0u, std::memory_order_acquire)) {
            driveCurve_ = toModCurve(denormalize(kParams[kDriveCurve], pendingValue(kDriveCurve)));
            applyCurve(ModTarget::drive, driveCurve_);
        }
        if (pendingDirty_[kBiasDepth].exchange(0u, std::memory_order_acquire)) {
            biasDepth_ = denormalize(kParams[kBiasDepth], pendingValue(kBiasDepth));
            applyDepth(ModTarget::bias, biasDepth_);
        }
        if (pendingDirty_[kBiasCurve].exchange(0u, std::memory_order_acquire)) {
            biasCurve_ = toModCurve(denormalize(kParams[kBiasCurve], pendingValue(kBiasCurve)));
            applyCurve(ModTarget::bias, biasCurve_);
        }
        if (pendingDirty_[kToneDepth].exchange(0u, std::memory_order_acquire)) {
            toneDepth_ = denormalize(kParams[kToneDepth], pendingValue(kToneDepth));
            applyDepth(ModTarget::tone, toneDepth_);
        }
        if (pendingDirty_[kToneCurve].exchange(0u, std::memory_order_acquire)) {
            toneCurve_ = toModCurve(denormalize(kParams[kToneCurve], pendingValue(kToneCurve)));
            applyCurve(ModTarget::tone, toneCurve_);
        }
        if (pendingDirty_[kMixDepth].exchange(0u, std::memory_order_acquire)) {
            mixDepth_ = denormalize(kParams[kMixDepth], pendingValue(kMixDepth));
            applyDepth(ModTarget::mix, mixDepth_);
        }
        if (pendingDirty_[kMixCurve].exchange(0u, std::memory_order_acquire)) {
            mixCurve_ = toModCurve(denormalize(kParams[kMixCurve], pendingValue(kMixCurve)));
            applyCurve(ModTarget::mix, mixCurve_);
        }
        if (pendingDirty_[kExternalSidechain].exchange(0u, std::memory_order_acquire)) {
            externalKey_ =
                denormalize(kParams[kExternalSidechain], pendingValue(kExternalSidechain)) >= 0.5f;
            applyExternalKey();
        }
        if (pendingDirty_[kScHpf].exchange(0u, std::memory_order_acquire)) {
            scHpfHz_ = denormalize(kParams[kScHpf], pendingValue(kScHpf));
            applyScHpf();
        }
        if (pendingDirty_[kStereoLink].exchange(0u, std::memory_order_acquire)) {
            stereoLink_ = toStereoLink(denormalize(kParams[kStereoLink], pendingValue(kStereoLink)));
            // Stored only; the linked-detection composition across channels is T036.
        }
    }

    // Per-parameter apply — push the denormalized value to every channel core.
    void applyDrive() noexcept {
        for (int ch = 0; ch < numChannels_; ++ch)
            cores_[static_cast<std::size_t>(ch)].setStaticDrive(driveDb_); // core holds dB
    }
    void applyVoicing() noexcept {
        for (int ch = 0; ch < numChannels_; ++ch)
            cores_[static_cast<std::size_t>(ch)].setVoicing(voicing_);
    }
    void applyTone() noexcept {
        for (int ch = 0; ch < numChannels_; ++ch)
            cores_[static_cast<std::size_t>(ch)].setStaticTone(tone_);
    }
    void applyMix() noexcept {
        for (int ch = 0; ch < numChannels_; ++ch)
            cores_[static_cast<std::size_t>(ch)].setStaticMix(mix_);
    }
    void applyOutput() noexcept {
        for (int ch = 0; ch < numChannels_; ++ch)
            cores_[static_cast<std::size_t>(ch)].setOutput(outputGain_); // linear gain
    }
    void applyBias() noexcept {
        for (int ch = 0; ch < numChannels_; ++ch)
            cores_[static_cast<std::size_t>(ch)].setStaticBias(bias_);
    }
    void applyQuality() noexcept {
        for (int ch = 0; ch < numChannels_; ++ch)
            cores_[static_cast<std::size_t>(ch)].setQuality(quality_);
    }
    void applyDetector() noexcept {
        for (int ch = 0; ch < numChannels_; ++ch)
            cores_[static_cast<std::size_t>(ch)].setDetectorMode(detectMode_);
    }
    void applyBallistics() noexcept {
        for (int ch = 0; ch < numChannels_; ++ch)
            cores_[static_cast<std::size_t>(ch)].setBallistics(ballistics_);
    }
    void applyAttack() noexcept {
        for (int ch = 0; ch < numChannels_; ++ch)
            cores_[static_cast<std::size_t>(ch)].setAttack(attackSeconds_);
    }
    void applyRelease() noexcept {
        for (int ch = 0; ch < numChannels_; ++ch)
            cores_[static_cast<std::size_t>(ch)].setRelease(releaseSeconds_);
    }
    void applyDetection() noexcept {
        for (int ch = 0; ch < numChannels_; ++ch)
            cores_[static_cast<std::size_t>(ch)].setDetection(detection_);
    }
    void applyDepth(ModTarget target, float depth) noexcept {
        for (int ch = 0; ch < numChannels_; ++ch)
            cores_[static_cast<std::size_t>(ch)].setDepth(target, depth);
    }
    void applyCurve(ModTarget target, ModCurve curve) noexcept {
        for (int ch = 0; ch < numChannels_; ++ch)
            cores_[static_cast<std::size_t>(ch)].setCurve(target, curve);
    }
    // DynamicPreset apply (T030 / FR-014). `none` is a no-op (byte-identical to
    // the constructor defaults, so it never clobbers a hand-dialed matrix);
    // opto/variMu/tapeComp write their documented matrix (program-dependent-
    // saturation-presets.h) to every core — a starting point, not a lock.
    void applyDynamicPreset() noexcept {
        if (dynamicPreset_ == DynamicPreset::none)
            return; // neutral baseline — see the header note
        writePreset(kPdsPresetConfigs[static_cast<std::size_t>(dynamicPreset_)]);
    }

    // Write a preset's documented modulation matrix into cached member state AND
    // push it to every channel core via the individual apply* helpers (audio
    // thread only). attack/release convert ms->s here, matching applyPending().
    void writePreset(const PdsPresetConfig& cfg) noexcept {
        driveDepth_ = cfg.drive.depth; driveCurve_ = cfg.drive.curve;
        biasDepth_  = cfg.bias.depth;  biasCurve_  = cfg.bias.curve;
        toneDepth_  = cfg.tone.depth;  toneCurve_  = cfg.tone.curve;
        mixDepth_   = cfg.mix.depth;   mixCurve_   = cfg.mix.curve;
        detection_  = cfg.detection;
        detectMode_ = cfg.detector;
        ballistics_ = cfg.ballistics;
        attackSeconds_  = cfg.attackMs * kMsToSec;
        releaseSeconds_ = cfg.releaseMs * kMsToSec;
        applyDepth(ModTarget::drive, driveDepth_); applyCurve(ModTarget::drive, driveCurve_);
        applyDepth(ModTarget::bias, biasDepth_);   applyCurve(ModTarget::bias, biasCurve_);
        applyDepth(ModTarget::tone, toneDepth_);   applyCurve(ModTarget::tone, toneCurve_);
        applyDepth(ModTarget::mix, mixDepth_);     applyCurve(ModTarget::mix, mixCurve_);
        applyDetection();
        applyDetector();
        applyBallistics();
        applyAttack();
        applyRelease();
    }
    void applyExternalKey() noexcept {
        for (int ch = 0; ch < numChannels_; ++ch)
            cores_[static_cast<std::size_t>(ch)].setExternalKey(externalKey_);
    }
    void applyScHpf() noexcept {
        for (int ch = 0; ch < numChannels_; ++ch)
            cores_[static_cast<std::size_t>(ch)].setScHpf(scHpfHz_);
    }

    void applyAll() noexcept {
        applyDrive();
        applyVoicing();
        applyTone();
        applyMix();
        applyOutput();
        applyBias();
        applyQuality();
        applyDetector();
        applyBallistics();
        applyAttack();
        applyRelease();
        applyDetection();
        applyDepth(ModTarget::drive, driveDepth_);
        applyCurve(ModTarget::drive, driveCurve_);
        applyDepth(ModTarget::bias, biasDepth_);
        applyCurve(ModTarget::bias, biasCurve_);
        applyDepth(ModTarget::tone, toneDepth_);
        applyCurve(ModTarget::tone, toneCurve_);
        applyDepth(ModTarget::mix, mixDepth_);
        applyCurve(ModTarget::mix, mixCurve_);
        // No applyDynamicPreset() here: writePreset() already baked the preset into
        // the cached fields at selection time, so re-pushing them faithfully carries
        // preset+overrides on prepare/reset without reverting a manual override.
        applyExternalKey();
        applyScHpf();
        // stereoLink_ has no per-core setter (composed across channels in T036).
    }

    static constexpr float kMsToSec = 0.001f; // attack/release descriptors hold ms; cores take seconds

    std::array<ProgramDependentSaturationCore, kMaxChannels> cores_{};
    float sampleRate_ = 48000.0f;
    int numChannels_ = 0;

    // Applied parameter state — mutated only in prepare/reset/applyPending (the
    // first two require a stopped stream; the third runs on the audio thread).
    // Defaults match the core's own defaults (all modulation depths 0).
    float driveDb_ = kParams[kDrive].defaultValue; // dB (the core converts to gain)
    SaturationVoicing voicing_ = SaturationVoicing::softClip;
    float tone_ = kParams[kTone].defaultValue;
    float mix_ = kParams[kMix].defaultValue;
    float outputGain_ = dbToGain(kParams[kOutput].defaultValue);
    float bias_ = kParams[kBias].defaultValue;
    SaturationQuality quality_ = SaturationQuality::adaa;
    DetectMode detectMode_ = DetectMode::rms;
    Ballistics ballistics_ = Ballistics::branching;
    float attackSeconds_ = kParams[kAttack].defaultValue * kMsToSec;
    float releaseSeconds_ = kParams[kRelease].defaultValue * kMsToSec;
    Detection detection_ = Detection::feedForward;
    float driveDepth_ = kParams[kDriveDepth].defaultValue;
    ModCurve driveCurve_ = ModCurve::linear;
    float biasDepth_ = kParams[kBiasDepth].defaultValue;
    ModCurve biasCurve_ = ModCurve::linear;
    float toneDepth_ = kParams[kToneDepth].defaultValue;
    ModCurve toneCurve_ = ModCurve::linear;
    float mixDepth_ = kParams[kMixDepth].defaultValue;
    ModCurve mixCurve_ = ModCurve::linear;
    DynamicPreset dynamicPreset_ = DynamicPreset::none;
    bool externalKey_ = false;
    float scHpfHz_ = kParams[kScHpf].defaultValue;
    StereoLink stereoLink_ = StereoLink::perChannel;

    // Cross-thread pending edits: any thread publishes, the audio thread
    // consumes (bit-pattern storage keeps the atomic provably lock-free).
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
