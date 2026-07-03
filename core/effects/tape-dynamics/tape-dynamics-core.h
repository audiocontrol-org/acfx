#pragma once

#include <array>
#include <cmath>
#include <cstdint>

#include "primitives/nonlinear/hysteresis.h"
#include "primitives/oversampling/oversampler.h"

// TapeDynamicsCore — the RT-safe per-channel composition kernel (FR-008/009).
//
// Composes the shipped primitives (Hysteresis, Oversampler) into the full
// per-sample tape dynamics signal chain with optional trim: input gain →
// Oversampler<Factor> (JA/hysteresis step) → optional trim → dry/wet mix →
// output trim. Platform-independent (Constitution IV): no host-framework or
// embedded-vendor headers. RT-safe by construction (Constitution VI): every
// member is a value (no heap), all coefficient work lives in prepare()/setters,
// and process() is allocation-free, lock-free, and bounded.
//
// Composition (data-model.md "Entity — TapeDynamicsCore"):
//   oversampler   Oversampler<Factor>  runs Hysteresis at the oversampled rate
//   hysteresis    Hysteresis           the Jiles-Atherton magnetics core
//   (optional)    EnvelopeFollower     trim ballistics (if trim.enabled)
//   (optional)    GainComputer         trim gain mapping (if trim.enabled)
//
// The template Factor parameter (2/4/8/16) is chosen at effect level based on
// the oversampling parameter; TapeDynamicsEffect instantiates all four cores
// and dispatches at process() time (data-model.md).

namespace acfx {

template <int Factor>
class TapeDynamicsCore {
public:
    // Prepare for a sample rate and channel count; sizes/configures all
    // composed primitives and caches macro→physics coefficients (drive gain,
    // Ms, k, mix, output). No audio-path work (Constitution VI).
    void prepare(double sampleRate, int channels) noexcept {
        sampleRate_ = sampleRate;
        numChannels_ = channels;
        if (numChannels_ < 1)
            numChannels_ = 1;
        if (numChannels_ > kMaxChannels)
            numChannels_ = kMaxChannels;

        // The oversampler runs at the BASE rate (it does the up/down itself);
        // the JA hysteresis step runs INSIDE the oversampler at the high rate,
        // so its integrator step size dt = 1/fs must be configured for the
        // OVERSAMPLED rate (sampleRate * Factor) — otherwise the JA physics
        // would be integrated with the wrong dt. This is the crux of composing
        // Hysteresis under Oversampler<Factor> (FR-009, contract E4).
        const double osRate = sampleRate_ * static_cast<double>(Factor);
        for (int ch = 0; ch < numChannels_; ++ch) {
            oversampler_[static_cast<std::size_t>(ch)].init(
                static_cast<float>(sampleRate_));
            hysteresis_[static_cast<std::size_t>(ch)].prepare(osRate);
        }

        // Re-push the cached macro→physics coefficients into every channel's
        // Hysteresis so prepare() re-establishes exactly the applied parameter
        // state (setters may have run before prepare()). No per-sample work.
        applyToHysteresis();
    }

    // Advance one audio sample on a channel; signal flow:
    // x · drive → Oversampler<Factor>::process(·, JA step) → [trim?] →
    // mix(dry, wet) · output. Returns the processed output sample.
    [[nodiscard]] float processSample(float x, int ch) noexcept {
        if (ch < 0 || ch >= numChannels_)
            return x;

        const std::size_t c = static_cast<std::size_t>(ch);
        const float dry = x;

        // 1. Input drive: scale the sample into the applied field H. drive=0 dB
        //    -> driveLinear_ == 1 -> unity into the magnetics.
        const float driven = x * driveLinear_;

        // 2. Oversampled JA step: the Oversampler upsamples `driven` to Factor
        //    high-rate samples, runs the Jiles-Atherton hysteresis integration
        //    on EACH (the evalAtHighRate callable: float -> float, noexcept),
        //    then decimates back to one base-rate sample. The oversampler is
        //    reused VERBATIM — the JA physics is the caller's nonlinearity.
        Hysteresis& h = hysteresis_[c];
        float wet = oversampler_[c].process(
            driven, [&h](float s) noexcept { return h.process(s); });

        // 3. Optional explicit trim (EnvelopeFollower + GainComputer) — T026.
        //    A clearly-marked no-op seam today: when trim is disabled the path
        //    is exactly drive -> OS(JA) -> mix -> output. T026 fills applyTrim().
        wet = applyTrim(wet, dry, ch);

        // 4. Dry/wet mix (mix=1 -> fully wet; mix=0 -> dry passthrough) then
        //    post output trim gain.
        float y = mix_ * wet + (1.0f - mix_) * dry;
        y *= outputGain_;
        return y;
    }

    // -----------------------------------------------------------------------
    // Parameter push API (called by the effect wrapper — T016 — on parameter
    // changes, off the audio path). Each setter caches the macro→physics
    // coefficient and/or forwards it into the composed Hysteresis units. NONE
    // of this runs per sample (Constitution VI).
    // -----------------------------------------------------------------------

    // Input drive in DECIBELS (parameter "drive", 0..24 dB; 0 dB == unity).
    // Cached as a linear gain applied ahead of the magnetics.
    void setDrive(float driveDb) noexcept {
        driveDb_ = driveDb;
        driveLinear_ = dbToGain(driveDb);
    }

    // "saturation" macro -> JAParams.Ms (saturation magnetization / ceiling),
    // pushed into every channel's Hysteresis. Direct mapping (tuning-pass
    // open question, per tape-dynamics-parameters.h): the setter is the
    // contract; the exact law is a later tuning concern. Ms must stay > 0
    // (Hysteresis::setMs guards this).
    void setSaturation(float saturation) noexcept {
        Ms_ = saturation;
        for (int ch = 0; ch < numChannels_; ++ch)
            hysteresis_[static_cast<std::size_t>(ch)].setMs(
                static_cast<double>(Ms_));
    }

    // "width" macro -> JAParams.k (coercivity / loop width / memory). Direct
    // mapping (tuning-pass open question); k must stay > 0 (Hysteresis::setK
    // guards this).
    void setWidth(float width) noexcept {
        k_ = width;
        for (int ch = 0; ch < numChannels_; ++ch)
            hysteresis_[static_cast<std::size_t>(ch)].setK(
                static_cast<double>(k_));
    }

    // Integration method for the JA ODE step (rk2/rk4/newtonRaphson).
    void setSolver(Solver solver) noexcept {
        solver_ = solver;
        for (int ch = 0; ch < numChannels_; ++ch)
            hysteresis_[static_cast<std::size_t>(ch)].setSolver(solver_);
    }

    // Dry/wet blend, 0..1 (1 == fully wet).
    void setMix(float mix) noexcept { mix_ = mix; }

    // Post makeup gain in DECIBELS (parameter "output", -24..24 dB). Cached as
    // a linear gain applied after the mix stage.
    void setOutput(float outputDb) noexcept {
        outputDb_ = outputDb;
        outputGain_ = dbToGain(outputDb);
    }

    // T026 seam: enable/disable the optional explicit trim stage. Today only
    // toggles the flag; applyTrim() stays a no-op until T026 wires the
    // EnvelopeFollower + GainComputer.
    void setTrimEnabled(bool enabled) noexcept { trimEnabled_ = enabled; }

    // Clear all composed state (filter memories, magnetization, etc.)
    // without discarding applied parameter state.
    void reset() noexcept {
        for (int ch = 0; ch < numChannels_; ++ch) {
            oversampler_[ch].reset();
            hysteresis_[ch].reset();
        }
    }

private:
    static constexpr int kMaxChannels = 32;

    // dB -> linear gain (mirrors SaturationEffect/PDS dbToGain exactly). Pure,
    // RT-safe; only ever called from the off-audio-path setters, never per
    // sample.
    [[nodiscard]] static float dbToGain(float db) noexcept {
        return std::pow(10.0f, db / 20.0f);
    }

    // Push the cached Ms/k/solver into every ACTIVE channel's Hysteresis. Used
    // by prepare() to (re)establish applied parameter state after the units are
    // (re)prepared. Off the audio path.
    void applyToHysteresis() noexcept {
        for (int ch = 0; ch < numChannels_; ++ch) {
            Hysteresis& h = hysteresis_[static_cast<std::size_t>(ch)];
            h.setMs(static_cast<double>(Ms_));
            h.setK(static_cast<double>(k_));
            h.setSolver(solver_);
        }
    }

    // T026 SEAM — optional explicit trim (EnvelopeFollower + GainComputer).
    // Deliberately a no-op passthrough today so the MVP path is EXACTLY
    // drive -> OS(JA) -> mix -> output. T026 slots the trim ballistics + gain
    // mapping in here (using `dry` as the detector source and `ch` to select
    // the per-channel follower state) without restructuring processSample().
    [[nodiscard]] float applyTrim(float wet, float /*dry*/, int /*ch*/) const
        noexcept {
        if (!trimEnabled_)
            return wet;
        // T026: run the per-channel EnvelopeFollower on the detector signal and
        // apply GainComputer output here. Until then, trimEnabled_ has no audible
        // effect (defined no-op, not a silent fallback — the stage is not yet
        // implemented, so it contributes unity gain).
        return wet;
    }

    double sampleRate_ = 48000.0;
    int numChannels_ = 1;

    std::array<Oversampler<Factor>, kMaxChannels> oversampler_;
    std::array<Hysteresis, kMaxChannels> hysteresis_;

    // Cached macro→physics parameter state (set off the audio path via the
    // setters / applyToHysteresis()). Defaults: 0 dB drive (unity into the
    // magnetics), Ms=k=1 (Hysteresis defaults), rk4 solver, fully wet, 0 dB
    // output, trim disabled.
    float driveDb_ = 0.0f;
    float driveLinear_ = 1.0f;   // dbToGain(0) == 1
    float Ms_ = 1.0f;
    float k_ = 1.0f;
    Solver solver_ = Solver::rk4;
    float mix_ = 1.0f;
    float outputDb_ = 0.0f;
    float outputGain_ = 1.0f;    // dbToGain(0) == 1
    bool trimEnabled_ = false;
};

} // namespace acfx
