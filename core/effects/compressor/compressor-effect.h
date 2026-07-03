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
#include "effects/compressor/compressor-core.h"
#include "effects/compressor/compressor-parameters.h"

// CompressorEffect — the host-facing wrapper adding the Effect contract on top
// of the CompressorCore composition kernel (US1..US4). Mirrors the shipped
// saturation-effect.h idiom EXACTLY: no base class, no hot-path vtable; one
// constexpr ParameterDescriptor table as the single source of parameter truth
// (FR-019); a lock-free atomic cross-thread parameter handoff (FR-020). The
// wrapper owns per-channel CompressorCore state and is allocation-free in
// process() (FR-022). All 17 parameters denormalize -> CompressorCore setter
// (same boilerplate shape as SaturationEffect::applyPending).
//
// EXTERNAL KEY (FR-014): process(io) is keyless (key = x per channel);
// process(io, sidechain) routes the matching sidechain channel sample as the
// key. Channel-mapping + sample-count rules are documented at keyAt().
//
// STEREO LINKING (FR-017): perChannel detects+applies each channel
// independently; linked shares ONE detection — per sample the largest-magnitude
// key drives the designated detector core (channel 0) once via detectGainLin(),
// and that SAME gain is applied to every channel via applyGain(), so a
// transient in one channel ducks all channels equally and the image stays put.
// The linked detection state (detector/HPF/ballistics + feedback tap) lives in
// core 0; in feedback topology it taps channel 0's previous output. Linking one
// channel degenerates to per-channel. (See the F7 limitation note in
// processBlock() on the idle non-primary detectors.)
//
// Thread-ownership boundary (identical to SaturationEffect/SvfEffect):
//   - setParameter() is callable from ANY thread: it only publishes a lock-free
//     atomic pending value, consumed by the audio thread at the top of
//     process(), so edits never race process() (FR-020).
//   - prepare()/reset() mutate core coefficients directly and are NOT
//     synchronized against process(); they must be called only while the audio
//     stream is stopped — the adapter's responsibility, not enforced here
//     (FR-021).
//
// The descriptor table, option labels, StereoLink enum, and the dense-id
// static_assert live in compressor-parameters.h (split out per FR-028 to keep
// this wrapper within budget). This file keeps the `Param` index enum and
// exposes the table via the `kParams` alias.

namespace acfx {

class CompressorEffect {
public:
    // Stable parameter ids — the dense index into kParams, in data-model.md's
    // table order (contracts/compressor-effect-api.md "CompressorEffect").
    enum Param : std::uint8_t {
        kThreshold = 0,
        kRatio = 1,
        kKnee = 2,
        kAttack = 3,
        kRelease = 4,
        kMode = 5,
        kDetection = 6,
        kDetector = 7,
        kBallisticsSite = 8,
        kRange = 9,
        kScHpf = 10,
        kLookahead = 11,
        kMakeup = 12,
        kAutoMakeup = 13,
        kStereoLink = 14,
        kMix = 15,
        kOutput = 16
    };

    // The single source of parameter truth (FR-019), defined in
    // compressor-parameters.h and aliased here so every in-class reference
    // (kParams[kThreshold], the static_assert, parameters()) reads naturally.
    // Row order matches the Param enum above (see that header for shapes,
    // ranges, and the discrete-option label arrays).
    static constexpr const std::array<ParameterDescriptor, 17>& kParams = kCompressorParams;

    CompressorEffect() noexcept {
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
        "CompressorEffect parameter table violates a descriptor invariant "
        "(max>min; logarithmic => min>0; discrete => count>=2 and choices.size()==count)");

    static constexpr span<const ParameterDescriptor> parameters() noexcept { return kParams; }

    // Audio stream must be stopped — see the thread-ownership note above.
    void prepare(const ProcessContext& ctx) noexcept {
        sampleRate_ = static_cast<float>(ctx.sampleRate);
        numChannels_ = ctx.numChannels < kMaxChannels ? ctx.numChannels : kMaxChannels;
        const int maxLookaheadSamples =
            static_cast<int>(std::lround(kParams[kLookahead].max * sampleRate_));
        for (int ch = 0; ch < numChannels_; ++ch)
            cores_[static_cast<std::size_t>(ch)].prepare(sampleRate_, maxLookaheadSamples);
        applyAll();
    }

    // Audio stream must be stopped — see the thread-ownership note above.
    void reset() noexcept {
        for (int ch = 0; ch < numChannels_; ++ch)
            cores_[static_cast<std::size_t>(ch)].reset();
        applyAll();
    }

    // In-place, keyless Effect-concept process() (main input doubles as the
    // key); StereoLink is honored (FR-017); see processBlock().
    void process(AudioBlock& io) noexcept {
        applyPending(); // consume cross-thread parameter edits on the audio thread
        processBlock(io, nullptr);
    }

    // In-place with an EXTERNAL sidechain key (FR-014): detection reads the
    // matching sidechain channel, the gain still applies to `io`. Both overloads
    // consume pending params at the top and are allocation-free / lock-free.
    // Channel-mapping + linking rules are documented at processBlock()/keyAt().
    void process(AudioBlock& io, const AudioBlock& sidechain) noexcept {
        applyPending();
        processBlock(io, &sidechain);
    }

    // Publish a normalized 0..1 value for a parameter. Callable from any thread;
    // the audio thread applies it at the next process() (no immediate core
    // mutation — keeps coefficient updates single-threaded) (FR-020).
    void setParameter(ParamId id, float normalized) noexcept {
        const std::uint8_t i = id.value;
        if (i >= kNumParams)
            return; // out-of-range id: a programming error; no silent state change
        pendingBits_[i].store(floatBits(normalized), std::memory_order_relaxed);
        pendingDirty_[i].store(1u, std::memory_order_release);
    }

    // Reported latency = round(lookaheadSeconds * sampleRate), clamped to the
    // same budget ceiling the core applies to the actual delay (F6), so the
    // reported PDC always matches the real delay (FR-021). Set in prepare() (via
    // applyLookahead) and kept current on each lookahead apply. The lookahead
    // DelayLine is the only latency source; every other stage is zero-latency.
    int latencySamples() const noexcept { return latencySamples_; }

private:
    static constexpr int kMaxChannels = 8;
    static constexpr std::size_t kNumParams = 17;

    // The key sample for main channel `ch` at sample `n`. No sidechain block
    // (sc == nullptr / empty) => the channel's own input (internal key, key=x).
    // With a sidechain: use its channel `ch`, falling back to channel 0 when it
    // has fewer channels (a mono key feeds every channel). Read-only, so the
    // detect pass sees pre-write values.
    //
    // SAMPLE-COUNT rule (F5): a sidechain may carry FEWER samples than the main
    // block. Never read past it — with no sidechain samples fall back to the
    // main input at `n`; otherwise clamp the read to the last valid sample
    // (scSamples - 1) so a shorter sidechain holds its final value.
    static float keyAt(const std::array<float*, kMaxChannels>& mainCh,
                       const AudioBlock* sc, int scChannels, int scSamples,
                       int ch, int n) noexcept {
        if (sc == nullptr || scChannels <= 0 || scSamples <= 0)
            return mainCh[static_cast<std::size_t>(ch)][n];
        const int scCh = (ch < scChannels) ? ch : 0;
        const int scN  = (n < scSamples) ? n : scSamples - 1;
        return sc->channel(scCh)[scN];
    }

    // Shared core of both process() overloads. `sc == nullptr` => internal key.
    // perChannel: each channel detects + applies independently. linked (FR-017):
    // per sample take the largest-magnitude key, run the designated core
    // (channel 0) ONCE for a common gain, then apply that SAME gain to every
    // channel. The detect pass reads all keys before the apply pass writes any
    // output, so an internal key is never read after overwrite. Allocation-free:
    // the only scratch is a bounded stack array of channel pointers.
    void processBlock(AudioBlock& io, const AudioBlock* sc) noexcept {
        const int channels = io.numChannels() < numChannels_ ? io.numChannels() : numChannels_;
        const int samples = io.numSamples();
        const int scChannels = (sc != nullptr) ? sc->numChannels() : 0;
        const int scSamples  = (sc != nullptr) ? sc->numSamples() : 0;

        std::array<float*, kMaxChannels> mainCh{};
        for (int ch = 0; ch < channels; ++ch)
            mainCh[static_cast<std::size_t>(ch)] = io.channel(ch);

        // KNOWN LIMITATION (F7): in linked mode only the designated core
        // (channel 0) advances its detector; the others sit idle. A linked ->
        // perChannel switch therefore re-converges those idle detectors over the
        // attack/release time — a brief transient, NOT a wrong steady state.
        // Warming every detector was rejected: in feedback topology it would tap
        // each channel's own previous output, reintroducing the per-channel
        // image drift linked mode exists to remove. Out of scope for this pass.
        if (stereoLink_ == StereoLink::linked && channels > 0) {
            for (int n = 0; n < samples; ++n) {
                // Detect pass: cross-channel-max key (by magnitude).
                float maxKey = 0.0f;
                float maxMag = -1.0f;
                for (int ch = 0; ch < channels; ++ch) {
                    const float key = keyAt(mainCh, sc, scChannels, scSamples, ch, n);
                    const float mag = std::fabs(key);
                    if (mag > maxMag) {
                        maxMag = mag;
                        maxKey = key;
                    }
                }
                // One shared detection on the designated core, one common gain.
                const float gLin = cores_[0].detectGainLin(maxKey);
                // Apply pass: same gain to every channel (image-stable).
                for (int ch = 0; ch < channels; ++ch) {
                    float* x = mainCh[static_cast<std::size_t>(ch)];
                    x[n] = cores_[static_cast<std::size_t>(ch)].applyGain(x[n], gLin);
                }
            }
            return;
        }

        // perChannel: independent detection + application per channel.
        for (int ch = 0; ch < channels; ++ch) {
            float* x = mainCh[static_cast<std::size_t>(ch)];
            CompressorCore& core = cores_[static_cast<std::size_t>(ch)];
            for (int n = 0; n < samples; ++n) {
                const float key = keyAt(mainCh, sc, scChannels, scSamples, ch, n);
                x[n] = core.process(x[n], key);
            }
        }
    }

    // float <-> uint32 bit reinterpretation (allocation-free; a 4-byte memcpy is
    // a register move) so the cross-thread atomics are provably lock-free.
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

    // Discrete bucket index -> enum (label-array order).
    static GainMode toMode(float index) noexcept {
        switch (static_cast<int>(index)) {
        case 1:
            return GainMode::limit;
        case 2:
            return GainMode::expand;
        case 3:
            return GainMode::gate;
        case 0:
        default:
            return GainMode::compress;
        }
    }
    static Detection toDetection(float index) noexcept {
        return static_cast<int>(index) == 1 ? Detection::feedBack : Detection::feedForward;
    }
    static DetectMode toDetector(float index) noexcept {
        return static_cast<int>(index) == 1 ? DetectMode::rms : DetectMode::peak;
    }
    static BallisticsSite toBallisticsSite(float index) noexcept {
        return static_cast<int>(index) == 1 ? BallisticsSite::gain : BallisticsSite::level;
    }
    static StereoLink toStereoLink(float index) noexcept {
        return static_cast<int>(index) == 1 ? StereoLink::linked : StereoLink::perChannel;
    }

    float pendingValue(Param p) const noexcept {
        return bitsFloat(pendingBits_[p].load(std::memory_order_relaxed));
    }

    // Apply any parameter values published since the last block (audio thread).
    // Each dirty param is denormalized into its REAL value before the matching
    // CompressorCore setter is pushed to every channel. stereoLink_ is stored
    // here and composed across channels in processBlock() (FR-017), not pushed
    // to any single core.
    void applyPending() noexcept {
        if (pendingDirty_[kThreshold].exchange(0u, std::memory_order_acquire)) {
            thresholdDb_ = denormalize(kParams[kThreshold], pendingValue(kThreshold));
            applyThreshold();
        }
        if (pendingDirty_[kRatio].exchange(0u, std::memory_order_acquire)) {
            ratio_ = denormalize(kParams[kRatio], pendingValue(kRatio));
            applyRatio();
        }
        if (pendingDirty_[kKnee].exchange(0u, std::memory_order_acquire)) {
            kneeDb_ = denormalize(kParams[kKnee], pendingValue(kKnee));
            applyKnee();
        }
        if (pendingDirty_[kAttack].exchange(0u, std::memory_order_acquire)) {
            attackSeconds_ = denormalize(kParams[kAttack], pendingValue(kAttack));
            applyAttack();
        }
        if (pendingDirty_[kRelease].exchange(0u, std::memory_order_acquire)) {
            releaseSeconds_ = denormalize(kParams[kRelease], pendingValue(kRelease));
            applyRelease();
        }
        if (pendingDirty_[kMode].exchange(0u, std::memory_order_acquire)) {
            mode_ = toMode(denormalize(kParams[kMode], pendingValue(kMode)));
            applyMode();
        }
        if (pendingDirty_[kDetection].exchange(0u, std::memory_order_acquire)) {
            detection_ = toDetection(denormalize(kParams[kDetection], pendingValue(kDetection)));
            applyDetection();
        }
        if (pendingDirty_[kDetector].exchange(0u, std::memory_order_acquire)) {
            detector_ = toDetector(denormalize(kParams[kDetector], pendingValue(kDetector)));
            applyDetector();
        }
        if (pendingDirty_[kBallisticsSite].exchange(0u, std::memory_order_acquire)) {
            ballisticsSite_ = toBallisticsSite(
                denormalize(kParams[kBallisticsSite], pendingValue(kBallisticsSite)));
            applyBallisticsSite();
        }
        if (pendingDirty_[kRange].exchange(0u, std::memory_order_acquire)) {
            rangeDb_ = denormalize(kParams[kRange], pendingValue(kRange));
            applyRange();
        }
        if (pendingDirty_[kScHpf].exchange(0u, std::memory_order_acquire)) {
            scHpfHz_ = denormalize(kParams[kScHpf], pendingValue(kScHpf));
            applyScHpf();
        }
        if (pendingDirty_[kLookahead].exchange(0u, std::memory_order_acquire)) {
            lookaheadSeconds_ = denormalize(kParams[kLookahead], pendingValue(kLookahead));
            applyLookahead();
        }
        if (pendingDirty_[kMakeup].exchange(0u, std::memory_order_acquire)) {
            makeupDb_ = denormalize(kParams[kMakeup], pendingValue(kMakeup));
            applyMakeup();
        }
        if (pendingDirty_[kAutoMakeup].exchange(0u, std::memory_order_acquire)) {
            autoMakeup_ =
                denormalize(kParams[kAutoMakeup], pendingValue(kAutoMakeup)) >= 0.5f;
            applyAutoMakeup();
        }
        if (pendingDirty_[kStereoLink].exchange(0u, std::memory_order_acquire)) {
            stereoLink_ =
                toStereoLink(denormalize(kParams[kStereoLink], pendingValue(kStereoLink)));
            // Not a per-core setter: composed across channels in processBlock().
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

    void applyThreshold() noexcept {
        for (int ch = 0; ch < numChannels_; ++ch)
            cores_[static_cast<std::size_t>(ch)].setThreshold(thresholdDb_);
    }
    void applyRatio() noexcept {
        for (int ch = 0; ch < numChannels_; ++ch)
            cores_[static_cast<std::size_t>(ch)].setRatio(ratio_);
    }
    void applyKnee() noexcept {
        for (int ch = 0; ch < numChannels_; ++ch)
            cores_[static_cast<std::size_t>(ch)].setKnee(kneeDb_);
    }
    void applyAttack() noexcept {
        for (int ch = 0; ch < numChannels_; ++ch)
            cores_[static_cast<std::size_t>(ch)].setAttack(attackSeconds_);
    }
    void applyRelease() noexcept {
        for (int ch = 0; ch < numChannels_; ++ch)
            cores_[static_cast<std::size_t>(ch)].setRelease(releaseSeconds_);
    }
    void applyMode() noexcept {
        for (int ch = 0; ch < numChannels_; ++ch)
            cores_[static_cast<std::size_t>(ch)].setMode(mode_);
    }
    void applyDetection() noexcept {
        for (int ch = 0; ch < numChannels_; ++ch)
            cores_[static_cast<std::size_t>(ch)].setDetection(detection_);
    }
    void applyDetector() noexcept {
        for (int ch = 0; ch < numChannels_; ++ch)
            cores_[static_cast<std::size_t>(ch)].setDetector(detector_);
    }
    void applyBallisticsSite() noexcept {
        for (int ch = 0; ch < numChannels_; ++ch)
            cores_[static_cast<std::size_t>(ch)].setBallisticsSite(ballisticsSite_);
    }
    void applyRange() noexcept {
        for (int ch = 0; ch < numChannels_; ++ch)
            cores_[static_cast<std::size_t>(ch)].setRange(rangeDb_);
    }
    void applyScHpf() noexcept {
        for (int ch = 0; ch < numChannels_; ++ch)
            cores_[static_cast<std::size_t>(ch)].setSidechainHpf(scHpfHz_);
    }
    void applyLookahead() noexcept {
        const int samples = static_cast<int>(std::lround(lookaheadSeconds_ * sampleRate_));
        // F6: report the ACTUAL delay, not the raw request. The core clamps the
        // delay to the lesser of round(kParams[kLookahead].max * sampleRate)
        // (prepare()'s ceiling) and the compiled buffer
        // CompressorCore::kMaxLookaheadSamples (matching setLookahead's clamp);
        // clamping the reported latency the same way keeps host PDC == real delay.
        const int budgetCeiling =
            static_cast<int>(std::lround(kParams[kLookahead].max * sampleRate_));
        int ceiling = budgetCeiling < CompressorCore::kMaxLookaheadSamples
                          ? budgetCeiling
                          : CompressorCore::kMaxLookaheadSamples;
        if (ceiling < 0) ceiling = 0;
        latencySamples_ = samples > ceiling ? ceiling : samples; // reported host latency (FR-021)
        for (int ch = 0; ch < numChannels_; ++ch)
            cores_[static_cast<std::size_t>(ch)].setLookahead(samples);
    }
    void applyMakeup() noexcept {
        for (int ch = 0; ch < numChannels_; ++ch)
            cores_[static_cast<std::size_t>(ch)].setMakeup(makeupDb_);
    }
    void applyAutoMakeup() noexcept {
        for (int ch = 0; ch < numChannels_; ++ch)
            cores_[static_cast<std::size_t>(ch)].setAutoMakeup(autoMakeup_);
    }
    void applyMix() noexcept {
        for (int ch = 0; ch < numChannels_; ++ch)
            cores_[static_cast<std::size_t>(ch)].setMix(mix_);
    }
    void applyOutput() noexcept {
        for (int ch = 0; ch < numChannels_; ++ch)
            cores_[static_cast<std::size_t>(ch)].setOutput(outputDb_);
    }
    void applyAll() noexcept {
        applyThreshold();
        applyRatio();
        applyKnee();
        applyAttack();
        applyRelease();
        applyMode();
        applyDetection();
        applyDetector();
        applyBallisticsSite();
        applyRange();
        applyScHpf();
        applyLookahead();
        applyMakeup();
        applyAutoMakeup();
        applyMix();
        applyOutput();
    }

    std::array<CompressorCore, kMaxChannels> cores_{};
    float sampleRate_ = 48000.0f;
    int numChannels_ = 0;
    int latencySamples_ = 0; // round(lookaheadSeconds_ * sampleRate_); see applyLookahead()

    // Applied parameter state — mutated only in prepare/reset/applyPending (the
    // first two require a stopped stream; the third runs on the audio thread).
    // Defaults are the denormalized kParams defaults.
    float thresholdDb_ = kParams[kThreshold].defaultValue;
    float ratio_ = kParams[kRatio].defaultValue;
    float kneeDb_ = kParams[kKnee].defaultValue;
    float attackSeconds_ = kParams[kAttack].defaultValue;
    float releaseSeconds_ = kParams[kRelease].defaultValue;
    GainMode mode_ = GainMode::compress;
    Detection detection_ = Detection::feedForward;
    DetectMode detector_ = DetectMode::rms;
    BallisticsSite ballisticsSite_ = BallisticsSite::level;
    float rangeDb_ = kParams[kRange].defaultValue;
    float scHpfHz_ = kParams[kScHpf].defaultValue;
    float lookaheadSeconds_ = kParams[kLookahead].defaultValue;
    float makeupDb_ = kParams[kMakeup].defaultValue;
    bool autoMakeup_ = false;
    StereoLink stereoLink_ = StereoLink::linked;
    float mix_ = kParams[kMix].defaultValue;
    float outputDb_ = kParams[kOutput].defaultValue;

    // Cross-thread pending edits: any thread publishes, the audio thread
    // consumes. Stored as the float's bit pattern in a uint32 so the atomic is
    // provably lock-free (a bare std::atomic<float> can degrade to a libcall).
    std::array<std::atomic<std::uint32_t>, kNumParams> pendingBits_;
    std::array<std::atomic<std::uint32_t>, kNumParams> pendingDirty_;
    static_assert(std::atomic<std::uint32_t>::is_always_lock_free,
                  "pending-parameter atomics must be lock-free for RT safety");
};

} // namespace acfx
