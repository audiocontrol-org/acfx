// program-dependent-saturation-matrix-test.cpp
// T019 -- User Story 4: the modulation matrix (drive, bias, tone, mix).
//
// SC-003 / FR-006: each of the four targets is offset by its OWN
// DynamicsModulator fed from the single shared envelope, with NO cross-talk
// between targets. This suite drives ProgramDependentSaturationCore directly
// (so it can call newBlock() itself and read lastNorm(), exactly as the effect
// wrapper does) and asserts, per target with the OTHER three depths at 0, that
// the target's characteristic effect moves WITH LEVEL when its depth is
// non-zero and does NOT move when its depth is 0:
//
//   BIAS (TEST 1): a non-zero biasDepth makes the EVEN-harmonic content
//     (2nd-harmonic ratio -- the DC-blocked signature of shape asymmetry;
//     bias is `u = drive*x + bias` in Waveshaper, so more bias => more
//     asymmetry => more even harmonics) grow with level. At biasDepth=0 the
//     symmetric softClip shape keeps even harmonics near zero at every level.
//
//   TONE (TEST 2): a non-zero toneDepth makes the spectral TILT (a post
//     highpass tilt: tone>0 raises the HP corner, thinning the lows) brighten
//     with level. Measured as mag(highTone)/mag(lowTone) of a two-tone probe
//     run near-linearly so the tilt filter -- not saturation -- dominates.
//     Tone is per-BLOCK (Decision 4): the helper refreshes it via newBlock()
//     at block boundaries, mirroring the effect wrapper. At toneDepth=0 the
//     tilt is level-independent.
//
//   MIX (TEST 3): a non-zero mixDepth makes the wet/dry blend move with level.
//     With a dry static base (staticMix=0) and a heavily-driven wet path, the
//     "wetness" (relative RMS distance of the output from the dry input) rises
//     with level. At mixDepth=0 (staticMix=0) the output stays byte-dry, so
//     wetness is ~0 at every level.
//
//   NO CROSS-TALK (TEST 4): with ONLY biasDepth>0 (drive/tone/mix depths 0),
//     the modulated core's steady-state harmonic signature MATCHES a reference
//     core whose bias is FROZEN static at the same steady value (all depths 0)
//     -- proving the bias modulation left drive/tone/mix at their static base
//     -- while DIFFERING strongly from a no-bias core (so the match is not
//     vacuous). The frozen bias value is read from the modulated core's own
//     lastNorm() (linear curve, depth +1 => steady bias == norm).
//
// Determinism / anti-leakage: sr=48000, capture window N=4800 => every probe
// tone (1000/250/7000 Hz) completes an integer number of cycles, so every
// Goertzel readout lands exactly on a bin (measurement-support.h window
// contract). No per-run randomness. Robust ORDERING assertions (A > B + margin)
// with named tolerances, not brittle absolute targets.

#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <functional>
#include <limits>
#include <vector>

#include "effects/program-dependent-saturation/program-dependent-saturation-core.h"
#include "effects/saturation/saturation-core.h"  // SaturationVoicing / SaturationQuality
#include "core/measurement-support.h"
#include "support/measurement/analyzers.h"  // acfx::measure::GoertzelAnalyzer

using namespace acfx;

namespace {

// ---------------------------------------------------------------------------
// Window / driving constants.
// ---------------------------------------------------------------------------
constexpr double      kSampleRate = 48000.0;
constexpr std::size_t kCaptureN   = 4800;   // 100 cycles @ 1 kHz; integer cycles for 250/7000 too
constexpr std::size_t kWarmup     = 24000;  // 0.5 s -- envelope + per-block tone fully settled
constexpr int         kBlockSize  = 64;     // per-block tone refresh cadence (as the effect wrapper)
constexpr double      kPi         = 3.14159265358979323846;

// Two well-separated stimulus levels (single-tone amplitudes). RMS -> dBFS ->
// normalized window position (default window -60..0 dBFS):
//   A=0.50 -> RMS -9.0 dBFS  -> norm ~= 0.85  (HIGH)
//   A=0.05 -> RMS -29.0 dBFS -> norm ~= 0.52  (LOW)
constexpr float kHiAmp = 0.50f;
constexpr float kLoAmp = 0.05f;

// ---------------------------------------------------------------------------
// Deterministic stimulus generators: absolute-sample-index -> input sample.
// ---------------------------------------------------------------------------
std::function<float(std::size_t)> sineStim(double freqHz, float amp) {
    const double w = 2.0 * kPi * freqHz / kSampleRate;
    return [w, amp](std::size_t n) noexcept {
        return amp * static_cast<float>(std::sin(w * static_cast<double>(n)));
    };
}

std::function<float(std::size_t)> twoToneStim(double loHz, double hiHz, float amp) {
    const double wl = 2.0 * kPi * loHz / kSampleRate;
    const double wh = 2.0 * kPi * hiHz / kSampleRate;
    return [wl, wh, amp](std::size_t n) noexcept {
        const double t = static_cast<double>(n);
        return amp * static_cast<float>(std::sin(wl * t) + std::sin(wh * t));
    };
}

// ---------------------------------------------------------------------------
// Drive a steady stimulus through the core, warming up so the shared envelope
// AND the per-block tone modulation settle, then capture kCaptureN steady
// samples. newBlock() is called at block boundaries (fed the core's own
// lastNorm(), a one-block control-rate lag) exactly as
// ProgramDependentSaturationEffect::process() does, so per-block tone
// modulation is exercised realistically. Phase is continuous across the
// warm-up/capture boundary (absolute index), and the capture window spans
// integer cycles, so every Goertzel readout is leakage-free.
// ---------------------------------------------------------------------------
std::vector<float> captureSteady(ProgramDependentSaturationCore& core,
                                 const std::function<float(std::size_t)>& stim) {
    std::size_t n = 0;
    int inBlock = 0;
    auto step = [&](float* dst) {
        if (inBlock == 0)
            core.newBlock(core.lastNorm());  // block boundary: refresh per-block tone
        const float x = stim(n);
        const float y = core.process(x, x);  // key == main (no external sidechain)
        if (dst != nullptr)
            *dst = y;
        ++n;
        inBlock = (inBlock + 1) % kBlockSize;
    };
    for (std::size_t i = 0; i < kWarmup; ++i)
        step(nullptr);
    std::vector<float> out(kCaptureN, 0.0f);
    for (std::size_t i = 0; i < kCaptureN; ++i)
        step(&out[i]);
    return out;
}

// Build the dry (input) buffer over the capture window, aligned sample-for-
// sample with captureSteady()'s output (warm-up consumes indices 0..kWarmup-1;
// capture is kWarmup..kWarmup+N-1). Used by the mix test: at mix=0 the core's
// output equals this buffer exactly (naive/adaa are zero-latency).
std::vector<float> dryOverCapture(const std::function<float(std::size_t)>& stim) {
    std::vector<float> dry(kCaptureN, 0.0f);
    for (std::size_t i = 0; i < kCaptureN; ++i)
        dry[i] = stim(kWarmup + i);
    return dry;
}

// Single-bin amplitude at freqHz over a captured buffer.
double magAt(const std::vector<float>& buf, double freqHz) {
    return acfx::measure::GoertzelAnalyzer{freqHz, kSampleRate}
        .analyze(span<const float>(buf))
        .magnitude;
}

// 2nd-harmonic ratio (even-harmonic / asymmetry signature) at a 1 kHz probe.
double evenHarmonicRatio(const std::vector<float>& buf) {
    const meastest::HarmonicSignature sig =
        meastest::harmonicSignature(span<const float>(buf), 1000.0, kSampleRate, 6);
    return sig.ratio(2);
}

// Max abs difference of harmonic ratios h=2..6 between two 1 kHz captures --
// a shape-distance that ignores fundamental loudness (mirrors the
// spectralDistance idiom in saturation-voicings-test.cpp).
double harmonicShapeDistance(const std::vector<float>& a, const std::vector<float>& b) {
    const meastest::HarmonicSignature sa =
        meastest::harmonicSignature(span<const float>(a), 1000.0, kSampleRate, 6);
    const meastest::HarmonicSignature sb =
        meastest::harmonicSignature(span<const float>(b), 1000.0, kSampleRate, 6);
    double maxDiff = 0.0;
    for (int h = 2; h <= 6; ++h) {
        const double ra = sa.ratio(h);
        const double rb = sb.ratio(h);
        if (!std::isfinite(ra) || !std::isfinite(rb))
            continue;
        maxDiff = std::max(maxDiff, std::abs(ra - rb));
    }
    return maxDiff;
}

// Configure a core to the neutral matrix baseline: softClip/adaa, unity static
// character, rms detection with fast settling, default -60..0 window, and ALL
// four modulation depths 0 (linear curve). Individual tests override the one
// static base + one depth they exercise.
void configureBaseline(ProgramDependentSaturationCore& core) {
    core.prepare(static_cast<float>(kSampleRate));
    core.setVoicing(SaturationVoicing::softClip);
    core.setQuality(SaturationQuality::adaa);
    core.setDetectorMode(DetectMode::rms);
    core.setBallistics(Ballistics::branching);
    core.setAttack(0.005f);
    core.setRelease(0.050f);
    core.setDetection(PdsDetection::feedForward);
    core.setRefWindow(-60.0f, 0.0f);

    core.setStaticDrive(0.0f);  // dB
    core.setStaticBias(0.0f);
    core.setStaticTone(0.0f);
    core.setStaticMix(1.0f);
    core.setOutput(1.0f);

    for (ModTarget t : {ModTarget::drive, ModTarget::bias, ModTarget::tone, ModTarget::mix}) {
        core.setDepth(t, 0.0f);
        core.setCurve(t, ModCurve::linear);  // linear => steady offset == norm (predictable)
    }
}

} // namespace

// ===========================================================================
// TEST 1 -- BIAS depth modulates even-harmonic asymmetry WITH LEVEL; zero
// depth does not (SC-003, FR-006).
//
// staticDrive 12 dB (gain ~4) saturates the 1 kHz probe so shape asymmetry
// shows as even harmonics; only biasDepth is engaged (+1, linear), so at
// steady state the applied bias == norm (grows with level). softClip is an odd
// (symmetric) shape, so with no bias the even-harmonic content is ~0 at every
// level -- the clean zero-depth reference.
// ===========================================================================
TEST_CASE("[T019][US4] bias depth shifts even-harmonic asymmetry with level; zero depth is flat (SC-003)") {
    // Baseline symmetric even-harmonic ceiling: softClip with no bias leaks
    // essentially no 2nd harmonic (two decades above the ~1e-6 leakage floor).
    constexpr double kEvenFloor      = 0.02;
    // A full-depth bias at high level must create clearly audible even content.
    constexpr double kEvenPresent    = 0.05;
    // Minimum level-driven rise (high vs low) and minimum modulated-vs-clean gap.
    constexpr double kEvenRiseMargin = 0.02;

    const auto stimHi = sineStim(1000.0, kHiAmp);
    const auto stimLo = sineStim(1000.0, kLoAmp);

    // Bias-modulated core (only biasDepth != 0).
    double modHi = 0.0, modLo = 0.0;
    {
        ProgramDependentSaturationCore core;
        configureBaseline(core);
        core.setStaticDrive(12.0f);
        core.setDepth(ModTarget::bias, 1.0f);
        modHi = evenHarmonicRatio(captureSteady(core, stimHi));
    }
    {
        ProgramDependentSaturationCore core;
        configureBaseline(core);
        core.setStaticDrive(12.0f);
        core.setDepth(ModTarget::bias, 1.0f);
        modLo = evenHarmonicRatio(captureSteady(core, stimLo));
    }

    // Zero-depth reference (same static drive, bias depth 0).
    double refHi = 0.0, refLo = 0.0;
    {
        ProgramDependentSaturationCore core;
        configureBaseline(core);
        core.setStaticDrive(12.0f);  // bias depth stays 0
        refHi = evenHarmonicRatio(captureSteady(core, stimHi));
    }
    {
        ProgramDependentSaturationCore core;
        configureBaseline(core);
        core.setStaticDrive(12.0f);
        refLo = evenHarmonicRatio(captureSteady(core, stimLo));
    }

    INFO("bias even2: modHi=" << modHi << " modLo=" << modLo
         << " refHi=" << refHi << " refLo=" << refLo);

    REQUIRE(std::isfinite(modHi));
    REQUIRE(std::isfinite(modLo));

    // Modulated: even-harmonic asymmetry is real AND rises with level.
    CHECK(modHi > kEvenPresent);
    CHECK(modHi > modLo + kEvenRiseMargin);
    // The rise is caused by bias modulation, not by drive (drive is unmodulated
    // here): the modulated high-level even content clears the clean reference.
    CHECK(modHi > refHi + kEvenRiseMargin);

    // Zero depth: symmetric shape => even content near zero and level-flat.
    CHECK(refHi < kEvenFloor);
    CHECK(refLo < kEvenFloor);
    CHECK(std::abs(refHi - refLo) < kEvenFloor);
}

// ===========================================================================
// TEST 2 -- TONE depth modulates spectral tilt (brightness) WITH LEVEL; zero
// depth does not (SC-003, FR-006).
//
// Runs near-linear (staticDrive 0 dB, two 250/7000 Hz tones peaking <= 0.5) so
// the post tone tilt -- not saturation -- shapes the spectrum. tone>0 raises
// the highpass corner, thinning the 250 Hz low tone relative to the 7000 Hz
// high tone => tilt = mag(7000)/mag(250) BRIGHTENS with level. Only toneDepth
// is engaged; tone is applied per-block via newBlock() in captureSteady().
// ===========================================================================
TEST_CASE("[T019][US4] tone depth brightens spectral tilt with level; zero depth is flat (SC-003)") {
    // Minimum level-driven brightening (actual modeled rise is far larger) and
    // the flat-band tolerance for the zero-depth (level-independent) reference.
    constexpr double kTiltRiseFactor = 1.15;  // tilt_hi > tilt_lo * this
    constexpr double kTiltFlatBand   = 0.05;  // |tilt_hi - tilt_lo| / tilt_lo bound

    // Near-linear two-tone levels (peak = 2*amp): HIGH peaks at 0.44 (< 0.5
    // softKnee linear boundary), LOW is far quieter -> a wide envelope span.
    const auto stimHi = twoToneStim(250.0, 7000.0, 0.22f);
    const auto stimLo = twoToneStim(250.0, 7000.0, 0.03f);

    auto tilt = [](const std::vector<float>& buf) {
        const double lo = magAt(buf, 250.0);
        const double hi = magAt(buf, 7000.0);
        return (lo > 0.0) ? hi / lo : std::numeric_limits<double>::infinity();
    };

    // Tone-modulated core (only toneDepth != 0).
    double modHi = 0.0, modLo = 0.0;
    {
        ProgramDependentSaturationCore core;
        configureBaseline(core);
        core.setDepth(ModTarget::tone, 1.0f);
        modHi = tilt(captureSteady(core, stimHi));
    }
    {
        ProgramDependentSaturationCore core;
        configureBaseline(core);
        core.setDepth(ModTarget::tone, 1.0f);
        modLo = tilt(captureSteady(core, stimLo));
    }

    // Zero-depth reference (tone depth 0 => tilt filter bypassed at tone=0).
    double refHi = 0.0, refLo = 0.0;
    {
        ProgramDependentSaturationCore core;
        configureBaseline(core);
        refHi = tilt(captureSteady(core, stimHi));
    }
    {
        ProgramDependentSaturationCore core;
        configureBaseline(core);
        refLo = tilt(captureSteady(core, stimLo));
    }

    INFO("tone tilt: modHi=" << modHi << " modLo=" << modLo
         << " refHi=" << refHi << " refLo=" << refLo);

    REQUIRE(std::isfinite(modHi));
    REQUIRE(std::isfinite(modLo));
    REQUIRE(modLo > 0.0);
    REQUIRE(refLo > 0.0);

    // Modulated: brightness (tilt) rises with level, and the high-level
    // modulated tilt is brighter than the unmodulated reference at that level.
    CHECK(modHi > modLo * kTiltRiseFactor);
    CHECK(modHi > refHi);

    // Zero depth: linear path, tilt is level-independent (flat within band).
    CHECK(std::abs(refHi - refLo) < refLo * kTiltFlatBand);
}

// ===========================================================================
// TEST 3 -- MIX depth modulates the wet/dry blend WITH LEVEL; zero depth does
// not (SC-003, FR-006).
//
// staticMix 0 (dry base) + a heavily-driven wet path (staticDrive 18 dB). With
// mixDepth +1, higher level => larger mix => output pulls toward the (very
// different) wet path. "Wetness" = relativeRmsError(out, dry): ~0 when the
// output equals the dry input, larger as the wet path dominates. Only mixDepth
// is engaged. At mixDepth=0 (staticMix=0) the output is byte-dry at all levels.
// ===========================================================================
TEST_CASE("[T019][US4] mix depth shifts the wet/dry blend with level; zero depth stays dry (SC-003)") {
    constexpr double kWetPresent    = 0.10;   // high-level modulated output is clearly wet
    constexpr double kWetRiseMargin = 0.05;   // minimum level-driven wetness rise
    constexpr double kDryFloor      = 1.0e-4; // mix=0 => output == dry (near-exact)

    const auto stimHi = sineStim(1000.0, kHiAmp);
    const auto stimLo = sineStim(1000.0, kLoAmp);
    const std::vector<float> dryHi = dryOverCapture(stimHi);
    const std::vector<float> dryLo = dryOverCapture(stimLo);

    auto wetness = [](const std::vector<float>& out, const std::vector<float>& dry) {
        return meastest::relativeRmsError(span<const float>(out), span<const float>(dry));
    };

    // Mix-modulated core: dry static base, wet path driven hard, only mixDepth.
    double modHi = 0.0, modLo = 0.0;
    {
        ProgramDependentSaturationCore core;
        configureBaseline(core);
        core.setStaticDrive(18.0f);
        core.setStaticMix(0.0f);
        core.setDepth(ModTarget::mix, 1.0f);
        modHi = wetness(captureSteady(core, stimHi), dryHi);
    }
    {
        ProgramDependentSaturationCore core;
        configureBaseline(core);
        core.setStaticDrive(18.0f);
        core.setStaticMix(0.0f);
        core.setDepth(ModTarget::mix, 1.0f);
        modLo = wetness(captureSteady(core, stimLo), dryLo);
    }

    // Zero-depth reference: dry static base, mix depth 0 => output stays dry.
    double refHi = 0.0, refLo = 0.0;
    {
        ProgramDependentSaturationCore core;
        configureBaseline(core);
        core.setStaticDrive(18.0f);
        core.setStaticMix(0.0f);  // mix depth stays 0
        refHi = wetness(captureSteady(core, stimHi), dryHi);
    }
    {
        ProgramDependentSaturationCore core;
        configureBaseline(core);
        core.setStaticDrive(18.0f);
        core.setStaticMix(0.0f);
        refLo = wetness(captureSteady(core, stimLo), dryLo);
    }

    INFO("mix wetness: modHi=" << modHi << " modLo=" << modLo
         << " refHi=" << refHi << " refLo=" << refLo);

    REQUIRE(std::isfinite(modHi));
    REQUIRE(std::isfinite(modLo));

    // Modulated: output is clearly wet at high level AND wetter than at low level.
    CHECK(modHi > kWetPresent);
    CHECK(modHi > modLo + kWetRiseMargin);

    // Zero depth: staticMix 0 keeps the output byte-dry regardless of level.
    CHECK(refHi < kDryFloor);
    CHECK(refLo < kDryFloor);
}

// ===========================================================================
// TEST 4 -- NO CROSS-TALK: engaging ONE target leaves the other three at their
// static base (SC-003, FR-006).
//
// With ONLY biasDepth>0 (drive/tone/mix depths 0), the modulated core's
// steady-state harmonic signature must MATCH a reference core whose bias is
// FROZEN static at the same steady value (all depths 0) -- i.e. drive, tone,
// and mix were untouched by the bias path. The frozen value is read from the
// modulated core's own lastNorm() (linear curve, depth +1 => steady bias ==
// norm). To prove the match is non-vacuous, the modulated core must also DIFFER
// strongly from a no-bias core. The static bases are set to non-trivial values
// (drive 12 dB, tone +0.3, mix 0.7) so any leak into them would be visible.
// ===========================================================================
TEST_CASE("[T019][US4] only biasDepth>0 leaves drive/tone/mix at static base (no cross-talk) (SC-003)") {
    // Bias-mod vs frozen-static-bias must be near-identical (only tiny envelope
    // ripple differs); no-bias must be far away.
    constexpr double kCrossTalkTol   = 0.04;
    constexpr double kBiasEffectGap  = 0.05;

    const auto stim = sineStim(1000.0, kHiAmp);

    auto configureCharacter = [](ProgramDependentSaturationCore& core) {
        configureBaseline(core);
        core.setStaticDrive(12.0f);  // non-trivial static bases so a leak would show
        core.setStaticTone(0.3f);
        core.setStaticMix(0.7f);
    };

    // Bias-modulated (only biasDepth != 0). Capture, then read the settled norm
    // -- with a linear curve at depth +1 the steady applied bias equals it.
    ProgramDependentSaturationCore modCore;
    configureCharacter(modCore);
    modCore.setDepth(ModTarget::bias, 1.0f);
    const std::vector<float> outMod = captureSteady(modCore, stim);
    const float steadyBias = modCore.lastNorm();  // == steady modulated bias

    // Frozen-static-bias reference: same character, bias baked in statically,
    // ALL depths 0. If the bias path had bled into drive/tone/mix, outMod would
    // diverge from this.
    ProgramDependentSaturationCore staticCore;
    configureCharacter(staticCore);
    staticCore.setStaticBias(steadyBias);  // depths remain 0
    const std::vector<float> outStatic = captureSteady(staticCore, stim);

    // No-bias reference: proves the near-equality above is not vacuous.
    ProgramDependentSaturationCore noBiasCore;
    configureCharacter(noBiasCore);  // staticBias 0, all depths 0
    const std::vector<float> outNoBias = captureSteady(noBiasCore, stim);

    const double distStatic = harmonicShapeDistance(outMod, outStatic);
    const double distNoBias = harmonicShapeDistance(outMod, outNoBias);

    INFO("steadyBias=" << steadyBias
         << " dist(mod,staticEquiv)=" << distStatic
         << " dist(mod,noBias)=" << distNoBias);

    REQUIRE(steadyBias > 0.0f);  // the envelope actually climbed into the window

    // No cross-talk: bias modulation reproduces the equivalent STATIC bias
    // (drive/tone/mix untouched)...
    CHECK(distStatic < kCrossTalkTol);
    // ...and the bias modulation is genuinely doing work (not a vacuous match).
    CHECK(distNoBias > kBiasEffectGap);
    CHECK(distNoBias > distStatic);
}
