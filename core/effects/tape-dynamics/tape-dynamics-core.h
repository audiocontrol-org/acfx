#pragma once

#include <array>
#include <cmath>
#include <cstdint>

#include "primitives/dynamics/envelope-follower.h"
#include "primitives/dynamics/gain-computer.h"
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
//   oversampler    Oversampler<Factor>  runs Hysteresis at the oversampled rate
//   hysteresis     Hysteresis           the Jiles-Atherton magnetics core
//   trimFollower   EnvelopeFollower     trim ballistics (per channel; only
//                                        ADVANCED when trim.enabled — T026)
//   trimGain       GainComputer         trim gain mapping (stateless; shared
//                                        across channels — its curve is fixed,
//                                        only the depth is user-controlled via
//                                        trim.amount)
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
            // The trim EnvelopeFollower runs at the BASE rate (it detects the
            // post-magnetics wet signal once per base-rate sample, same as
            // the mix/output stages) — unlike Hysteresis it is NOT inside the
            // oversampler.
            trimFollower_[static_cast<std::size_t>(ch)].init(
                static_cast<float>(sampleRate_));
        }

        // Re-push the cached macro→physics coefficients into every channel's
        // Hysteresis so prepare() re-establishes exactly the applied parameter
        // state (setters may have run before prepare()). No per-sample work.
        applyToHysteresis();
        applyToTrim();
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
        //    When trim is disabled the path is EXACTLY drive -> OS(JA) -> mix
        //    -> output (bit-exact, contract E7): applyTrim() early-returns
        //    before touching any trim state. When enabled it applies an
        //    envelope-driven gain reduction to the wet signal.
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

    // Enable/disable the optional explicit trim stage (FR-011). When disabled
    // applyTrim() early-returns without touching any trim state — the
    // bit-exact no-op guarantee (contract E7).
    void setTrimEnabled(bool enabled) noexcept { trimEnabled_ = enabled; }

    // Trim ballistics — EnvelopeFollower attack/release, in SECONDS, pushed
    // into every active channel's trim follower. Off the audio path.
    void setTrimAttack(float seconds) noexcept {
        trimAttackSeconds_ = seconds;
        for (int ch = 0; ch < numChannels_; ++ch)
            trimFollower_[static_cast<std::size_t>(ch)].setAttack(seconds);
    }
    void setTrimRelease(float seconds) noexcept {
        trimReleaseSeconds_ = seconds;
        for (int ch = 0; ch < numChannels_; ++ch)
            trimFollower_[static_cast<std::size_t>(ch)].setRelease(seconds);
    }

    // Trim depth, 0..1: scales the GainComputer's gain-reduction curve (in
    // dB) before it is applied to the wet signal. 0 == no reduction (unity);
    // 1 == the full curve. Cached only — GainComputer is stateless, so there
    // is nothing to push per-channel.
    void setTrimAmount(float amount) noexcept { trimAmount_ = amount; }

    // Clear all composed state (filter memories, magnetization, trim envelope
    // follower state, etc.) without discarding applied parameter state.
    void reset() noexcept {
        for (int ch = 0; ch < numChannels_; ++ch) {
            oversampler_[ch].reset();
            hysteresis_[ch].reset();
            trimFollower_[ch].reset();
        }
    }

private:
    static constexpr int kMaxChannels = 32;

    // Fixed trim GainComputer curve (not exposed as top-level trim macros —
    // only trim.attack/release/amount are user-controlled; see
    // tape-dynamics-parameters.h's header comment on JAParams.a/alpha/c for
    // the same "fixed, advanced" precedent). Values mirror GainComputer's own
    // defaults; named here for documentation.
    static constexpr float kTrimThresholdDb = -18.0f;
    static constexpr float kTrimRatio = 4.0f;
    static constexpr float kTrimKneeDb = 6.0f;

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

    // Optional explicit trim (EnvelopeFollower + GainComputer; T026, FR-011,
    // contract E7). When trim is DISABLED this is a true identity: it returns
    // `wet` untouched and — critically — never calls into trimFollower_, so
    // the signal path is bit-exact the magnetics-only core (E7's invariant).
    //
    // When ENABLED: the per-channel EnvelopeFollower tracks the wet (post-
    // magnetics) signal's envelope in the DECIBEL domain (attack/release are
    // the user's trim.attack/trim.release ballistics); the shared
    // (stateless) GainComputer maps that level to a gain-reduction curve in
    // dB (a fixed compress curve — threshold/ratio/knee are not exposed as
    // top-level trim macros, only the ballistics and the applied depth are);
    // trim.amount scales the reduction's DEPTH (0 == no reduction, 1 == the
    // full curve) before it is converted back to a linear gain and applied to
    // the wet signal. Composes the shipped primitives — no hand-rolled
    // detector.
    [[nodiscard]] float applyTrim(float wet, float /*dry*/, int ch) noexcept {
        if (!trimEnabled_)
            return wet;
        if (ch < 0 || ch >= numChannels_)
            return wet;

        const std::size_t c = static_cast<std::size_t>(ch);
        EnvelopeFollower& follower = trimFollower_[c];

        // Detector: envelope of the wet signal, in dB (GainComputer's native
        // input domain).
        const float levelDb = follower.process(wet);

        // Static curve -> gain reduction (dB, <= 0), scaled by the user's
        // trim.amount depth.
        const float grDb = trimGain_.computeGainDb(levelDb) * trimAmount_;

        return wet * dbToGain(grDb);
    }

    // Push the cached trim ballistics into every ACTIVE channel's
    // EnvelopeFollower and (re)configure the shared GainComputer curve.
    // Used by prepare() to (re)establish applied parameter state after the
    // trim followers are (re)prepared. Off the audio path (mirrors
    // applyToHysteresis()).
    void applyToTrim() noexcept {
        // The trim curve itself (threshold/ratio/knee) is a fixed, non-user-
        // facing compress shape — trim.amount (applied in applyTrim()) is
        // the only user-controlled depth knob. GainComputer's own defaults
        // already match these values; set them explicitly for clarity.
        trimGain_.setMode(GainMode::compress);
        trimGain_.setThreshold(kTrimThresholdDb);
        trimGain_.setRatio(kTrimRatio);
        trimGain_.setKnee(kTrimKneeDb);

        for (int ch = 0; ch < numChannels_; ++ch) {
            EnvelopeFollower& follower = trimFollower_[static_cast<std::size_t>(ch)];
            follower.setDomain(DetectDomain::decibel);
            follower.setAttack(trimAttackSeconds_);
            follower.setRelease(trimReleaseSeconds_);
        }
    }

    double sampleRate_ = 48000.0;
    int numChannels_ = 1;

    std::array<Oversampler<Factor>, kMaxChannels> oversampler_;
    std::array<Hysteresis, kMaxChannels> hysteresis_;
    std::array<EnvelopeFollower, kMaxChannels> trimFollower_;
    GainComputer trimGain_; // stateless; one shared instance (fixed curve, see above)

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
    float trimAttackSeconds_ = 0.01f;  // matches kTapeDynamicsParams[kTrimAttack] default (10 ms)
    float trimReleaseSeconds_ = 0.1f;  // matches kTapeDynamicsParams[kTrimRelease] default (100 ms)
    float trimAmount_ = 0.5f;          // matches kTapeDynamicsParams[kTrimAmount] default
};

} // namespace acfx
