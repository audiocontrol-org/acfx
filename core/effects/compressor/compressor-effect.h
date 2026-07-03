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

// CompressorEffect — the host-facing wrapper that adds the Effect contract on
// top of the CompressorCore composition kernel (US1..US4). Mirrors the shipped
// core/effects/saturation/saturation-effect.h idiom EXACTLY: no base class, no
// vtable in the hot path; one constexpr ParameterDescriptor table as the
// single source of parameter truth (FR-019); a lock-free atomic cross-thread
// parameter handoff (FR-020). The wrapper owns per-channel CompressorCore
// state and is allocation-free in process() (FR-022).
//
// This is a SKELETON (task T004): the parameter table + static_assert +
// Effect-contract method signatures are in place and the file compiles, but
// two things are deliberately left for later tasks:
//   - latencySamples() always reports 0 here; wiring it to the actual
//     lookahead (round(lookaheadSeconds * sampleRate)) is T011/FR-021.
//   - StereoLink cross-channel composition (FR-017, linked -> common gain
//     from the cross-channel max) is NOT implemented: this skeleton runs
//     every channel independently regardless of the stereoLink_ value. The
//     full cross-channel wiring is a later task (T011/T026).
// Per-parameter denormalize -> CompressorCore setter forwarding for the other
// 16 parameters IS implemented below (it is the same boilerplate shape as
// SaturationEffect::applyPending and does not need to wait for a later task).
//
// Thread-ownership boundary (identical to SaturationEffect/SvfEffect):
//   - setParameter() may be called from ANY thread (a UI loop, a MIDI
//     callback, an MCU main loop). It only publishes a lock-free atomic
//     pending value; the audio thread consumes pending values at the top of
//     process(). So parameter edits never race process() — the wrapper
//     encodes that handoff itself (FR-020).
//   - prepare()/reset() DO mutate core coefficients directly and are NOT
//     synchronized against process(). They must be called only while the
//     audio stream is stopped (before start, or during a device change with
//     audio paused) — the standard prepare/process lifecycle already implies
//     this, and that quiescence is the adapter's responsibility, not
//     something the wrapper can enforce (FR-021).
//
// PARAMETER RANGES ARE A TUNING-PASS OPEN QUESTION. The descriptor shapes
// below (kinds/units/skews/labels) are normative (contracts/compressor-
// effect-api.md, data-model.md); the exact numeric ranges are defensible
// placeholders, not yet validated against a reference measurement — mirroring
// SaturationEffect's kParams disclaimer.
//
// NOTE on time-valued units: ParamUnit (core/dsp/param-id.h) has no
// milliseconds enumerator, only `seconds`. data-model.md lists attack/
// release/lookahead ranges in ms as the tuning-pass placeholder; this table
// stores the equivalent values in SECONDS (the unit CompressorCore's
// setAttack/setRelease take) and tags them ParamUnit::seconds, so no ms<->s
// conversion is needed at apply time. lookahead is likewise stored/denormalized
// in seconds and converted to samples (round(seconds * sampleRate)) only when
// forwarded to CompressorCore::setLookahead / prepare()'s buffer sizing.

namespace acfx {

// StereoLink (data-model.md "StereoLink") — CompressorEffect-level only; not
// a CompressorCore concern. perChannel: independent per-channel detection.
// linked: one detector value (cross-channel max) drives a common gain
// (FR-017) — see the class-level skeleton note above.
enum class StereoLink : std::uint8_t { perChannel, linked };

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

    // Option labels for the discrete parameters (single source of truth).
    static constexpr std::array<std::string_view, 4> kModeLabels = {
        {"compress", "limit", "expand", "gate"}};
    static constexpr std::array<std::string_view, 2> kDetectionLabels = {
        {"feedForward", "feedBack"}};
    static constexpr std::array<std::string_view, 2> kDetectorLabels = {{"peak", "rms"}};
    static constexpr std::array<std::string_view, 2> kBallisticsSiteLabels = {{"level", "gain"}};
    static constexpr std::array<std::string_view, 2> kAutoMakeupLabels = {{"off", "on"}};
    static constexpr std::array<std::string_view, 2> kStereoLinkLabels = {
        {"perChannel", "linked"}};

    // The single source of parameter truth (FR-019). Shapes are normative
    // (data-model.md); ranges are the tuning-pass OPEN QUESTION (see the
    // header note above):
    //   threshold:      dB, -60..0, default -18
    //   ratio:          ratio, 1..20, default 4 (limit mode ignores it)
    //   knee:           dB, 0..24, default 6
    //   attack:         seconds, 0.0001..0.2 (0.1..200 ms), default 0.01
    //   release:        seconds, 0.001..2.0 (1..2000 ms), default 0.1
    //   mode:           discrete {compress, limit, expand, gate}
    //   detection:      discrete {feedForward, feedBack}
    //   detector:       discrete {peak, rms}
    //   ballisticsSite: discrete {level, gain}
    //   range:          dB, -80..0, default -40
    //   scHpf:          Hz, 0..500, default 0 (0 = bypass)
    //   lookahead:      seconds, 0..0.02 (0..20 ms), default 0
    //   makeup:         dB, -24..24, default 0
    //   autoMakeup:     discrete {off, on}
    //   stereoLink:     discrete {perChannel, linked}, default linked
    //   mix:            linear 0..1, default 1
    //   output:         dB, -24..24, default 0
    static constexpr std::array<ParameterDescriptor, 17> kParams = {{
        {ParamId{kThreshold}, "threshold", ParamUnit::decibels, -60.0f, 0.0f, -18.0f,
         ParamSkew::linear, ParamKind::continuous, 0},
        {ParamId{kRatio}, "ratio", ParamUnit::ratio, 1.0f, 20.0f, 4.0f, ParamSkew::linear,
         ParamKind::continuous, 0},
        {ParamId{kKnee}, "knee", ParamUnit::decibels, 0.0f, 24.0f, 6.0f, ParamSkew::linear,
         ParamKind::continuous, 0},
        {ParamId{kAttack}, "attack", ParamUnit::seconds, 0.0001f, 0.2f, 0.01f, ParamSkew::linear,
         ParamKind::continuous, 0},
        {ParamId{kRelease}, "release", ParamUnit::seconds, 0.001f, 2.0f, 0.1f, ParamSkew::linear,
         ParamKind::continuous, 0},
        {ParamId{kMode}, "mode", ParamUnit::none, 0.0f, 3.0f, 0.0f, ParamSkew::linear,
         ParamKind::discrete, 4, kModeLabels},
        {ParamId{kDetection}, "detection", ParamUnit::none, 0.0f, 1.0f, 0.0f, ParamSkew::linear,
         ParamKind::discrete, 2, kDetectionLabels},
        {ParamId{kDetector}, "detector", ParamUnit::none, 0.0f, 1.0f, 1.0f, ParamSkew::linear,
         ParamKind::discrete, 2, kDetectorLabels},
        {ParamId{kBallisticsSite}, "ballisticsSite", ParamUnit::none, 0.0f, 1.0f, 0.0f,
         ParamSkew::linear, ParamKind::discrete, 2, kBallisticsSiteLabels},
        {ParamId{kRange}, "range", ParamUnit::decibels, -80.0f, 0.0f, -40.0f, ParamSkew::linear,
         ParamKind::continuous, 0},
        {ParamId{kScHpf}, "scHpf", ParamUnit::hz, 0.0f, 500.0f, 0.0f, ParamSkew::linear,
         ParamKind::continuous, 0},
        {ParamId{kLookahead}, "lookahead", ParamUnit::seconds, 0.0f, 0.02f, 0.0f,
         ParamSkew::linear, ParamKind::continuous, 0},
        {ParamId{kMakeup}, "makeup", ParamUnit::decibels, -24.0f, 24.0f, 0.0f, ParamSkew::linear,
         ParamKind::continuous, 0},
        {ParamId{kAutoMakeup}, "autoMakeup", ParamUnit::none, 0.0f, 1.0f, 0.0f, ParamSkew::linear,
         ParamKind::discrete, 2, kAutoMakeupLabels},
        {ParamId{kStereoLink}, "stereoLink", ParamUnit::none, 0.0f, 1.0f, 1.0f, ParamSkew::linear,
         ParamKind::discrete, 2, kStereoLinkLabels},
        {ParamId{kMix}, "mix", ParamUnit::none, 0.0f, 1.0f, 1.0f, ParamSkew::linear,
         ParamKind::continuous, 0},
        {ParamId{kOutput}, "output", ParamUnit::decibels, -24.0f, 24.0f, 0.0f, ParamSkew::linear,
         ParamKind::continuous, 0},
    }};

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

    // In-place, per-channel, keyless (main input doubles as the sidechain
    // key). SKELETON: no cross-channel StereoLink composition yet — see the
    // header note above (FR-017 deferred).
    void process(AudioBlock& io) noexcept {
        applyPending(); // consume cross-thread parameter edits on the audio thread
        const int channels = io.numChannels() < numChannels_ ? io.numChannels() : numChannels_;
        const int samples = io.numSamples();
        for (int ch = 0; ch < channels; ++ch) {
            float* x = io.channel(ch);
            CompressorCore& core = cores_[static_cast<std::size_t>(ch)];
            for (int n = 0; n < samples; ++n)
                x[n] = core.process(x[n], x[n]);
        }
    }

    // Publish a normalized 0..1 value for a parameter. Callable from any
    // thread; the audio thread applies it at the next process() (no
    // immediate core mutation here — that keeps coefficient updates
    // single-threaded) (FR-020).
    void setParameter(ParamId id, float normalized) noexcept {
        const std::uint8_t i = id.value;
        if (i >= kNumParams)
            return; // out-of-range id: a programming error; no silent state change
        pendingBits_[i].store(floatBits(normalized), std::memory_order_relaxed);
        pendingDirty_[i].store(1u, std::memory_order_release);
    }

    // Reported latency = round(lookaheadSeconds * sampleRate) (FR-021).
    // SKELETON: not yet wired to the applied lookahead value — always 0 here;
    // a later task (T011) establishes it in prepare()/applyLookahead().
    int latencySamples() const noexcept { return 0; }

private:
    static constexpr int kMaxChannels = 8;
    static constexpr std::size_t kNumParams = 17;

    // float <-> uint32 bit reinterpretation (allocation-free; a 4-byte memcpy
    // is a register move). Lets the cross-thread atomics be provably
    // lock-free on every target.
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

    // Apply any parameter values published since the last block (audio
    // thread). Each dirty param is denormalized into its REAL value before
    // the matching CompressorCore setter is pushed to every channel.
    // stereoLink_ is stored but not (yet) composed across channels — see the
    // header note above.
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
            // Not forwarded to CompressorCore: cross-channel composition is a
            // later task (see the header note above; FR-017).
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

    // Applied parameter state — mutated only in prepare/reset/applyPending
    // (the first two require a stopped stream; the third runs on the audio
    // thread). Defaults are the denormalized kParams defaults.
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
    // consumes. Stored as the float's bit pattern in a uint32 so the atomic
    // is provably lock-free on every target (a bare std::atomic<float> can
    // degrade to a libcall on some embedded runtimes).
    std::array<std::atomic<std::uint32_t>, kNumParams> pendingBits_;
    std::array<std::atomic<std::uint32_t>, kNumParams> pendingDirty_;
    static_assert(std::atomic<std::uint32_t>::is_always_lock_free,
                  "pending-parameter atomics must be lock-free for RT safety");
};

} // namespace acfx
