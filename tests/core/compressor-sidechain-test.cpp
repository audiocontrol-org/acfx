#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "dsp/audio-block.h"
#include "dsp/param-id.h"
#include "dsp/parameter.h"
#include "dsp/process-context.h"
#include "effects/compressor/compressor-core.h"
#include "effects/compressor/compressor-effect.h"

// T029 (US8) + T031 (US9) -- sidechain HPF and external-key doctests.
//
// Written against specs/compressors/spec.md US8 ("Filter the sidechain
// (HPF / tilt)"), US9 ("Key the compressor from an external sidechain"),
// SC-006, SC-007, and the composition documented in
// core/effects/compressor/compressor-core.h (detectGainLin()'s
// `(scHpfHz_ > 0.0f) ? scFilter_.process(key) : key` branch feeding the
// composed SvfPrimitive highpass; process(x, key)) and
// core/effects/compressor/compressor-effect.h (the process(io) vs
// process(io, sidechain) overload + keyAt() fallback rule: no sidechain or an
// empty one falls back to the channel's own input as the key).
//
// Every assertion is COMPARATIVE (less GR vs more GR; reduction present vs
// absent; a ratio held constant) rather than a brittle absolute dB target --
// matching the shipped tests/support/svf-reference.h / svf-test.cpp /
// compressor-effect-test.cpp philosophy of asserting analytic truths and
// measured contrasts, not fabricated numbers. Gain reduction ("GR") is always
// the steady-state RMS ratio of output to a reference drive amplitude, in dB
// (<= 0 by convention: more negative = more reduction).
//
// CompressorCore is a plain, copyable per-channel value type (no atomics),
// matching tests/core/compressor-test.cpp's `CompressorCore makeCore(...)`
// return-by-value idiom. CompressorEffect owns cross-thread atomics
// (pendingBits_/pendingDirty_) and is therefore NOT copyable/movable
// (tests/core/compressor-effect-test.cpp always declares it as a local, never
// returns it by value) -- so the effect-level helpers below take
// `CompressorEffect&` and configure it in place instead.

using namespace acfx;

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr float  kSampleRate = 48000.0f;

// Settling/measurement windows shared by every steady-state measurement
// below: 400 ms lets attack (5 ms) and release (50 ms) ballistics fully
// converge (release is the slower of the two -- ~8 time constants of
// margin); the following 100 ms is the measurement window (its tail RMS is
// what every gain-reduction figure below is computed from).
constexpr int kSettleSamples  = static_cast<int>(0.400 * kSampleRate); // 19200
constexpr int kMeasureSamples = static_cast<int>(0.100 * kSampleRate); // 4800
constexpr int kTotalSamples   = kSettleSamples + kMeasureSamples;

// Shared compressor tuning for every case below: threshold -20 dBFS, 4:1
// ratio, hard knee (0 dB, a simple two-segment map), fast-ish attack/release
// so steady state settles well inside kSettleSamples.
constexpr float kThresholdDb    = -20.0f;
constexpr float kRatio          = 4.0f;
constexpr float kKneeDb         = 0.0f;
constexpr float kAttackSeconds  = 0.005f;
constexpr float kReleaseSeconds = 0.050f;

// ---------------------------------------------------------------------------
// Measurement helpers (test-only).
// ---------------------------------------------------------------------------

std::vector<float> sineBuffer(double freqHz, double amplitude, int n) {
    std::vector<float> buf(static_cast<std::size_t>(n));
    const double w = 2.0 * kPi * freqHz / static_cast<double>(kSampleRate);
    for (int i = 0; i < n; ++i)
        buf[static_cast<std::size_t>(i)] =
            static_cast<float>(amplitude * std::sin(w * static_cast<double>(i)));
    return buf;
}

// RMS over the last `tailSamples` of `buf` -- the settled measurement window,
// discarding the settle prefix (mirrors compressor-effect-test.cpp's `rms`).
double rmsTail(const std::vector<float>& buf, int tailSamples) {
    const int n = static_cast<int>(buf.size());
    const int start = (tailSamples < n) ? (n - tailSamples) : 0;
    double sumSq = 0.0;
    int count = 0;
    for (int i = start; i < n; ++i) {
        const double v = static_cast<double>(buf[static_cast<std::size_t>(i)]);
        sumSq += v * v;
        ++count;
    }
    return count > 0 ? std::sqrt(sumSq / static_cast<double>(count)) : 0.0;
}

// Gain reduction in dB: 20*log10(measuredRms / referenceRms), referenceRms
// being the drive tone's own RMS (amplitude/sqrt2). <= 0 by construction
// (attenuation); 0 == no reduction. This is exactly "the ratio of output to
// input level (in dB) after settling" the task calls for.
double gainReductionDb(double measuredRms, double referenceAmplitude) {
    const double referenceRms = referenceAmplitude / std::sqrt(2.0);
    constexpr double kFloor = 1.0e-9; // guard log(0)
    const double rms = measuredRms < kFloor ? kFloor : measuredRms;
    return 20.0 * std::log10(rms / referenceRms);
}

// Relative deviation of the pointwise ratio out[n]/x[n] from its own mean,
// over the tail window. If the main path is a PURE scalar multiple of x
// (only the intended VCA gain applied -- no extra filtering), this ratio is
// constant and the deviation is ~0. If x were instead ALSO routed through the
// sidechain HPF, the filter's phase shift would make out[n] a phase-shifted
// (not scaled) version of x[n], and this ratio would swing over the window.
// Samples near a zero-crossing of x are excluded (a tiny denominator would
// otherwise amplify float noise into a spurious deviation).
double maxRelativeRatioDeviation(const std::vector<float>& x, const std::vector<float>& out,
                                 int tailSamples, double mainAmplitude) {
    const int n = static_cast<int>(x.size());
    const int start = (tailSamples < n) ? (n - tailSamples) : 0;

    std::vector<double> ratios;
    ratios.reserve(static_cast<std::size_t>(n - start));
    double sum = 0.0;
    for (int i = start; i < n; ++i) {
        const float xi = x[static_cast<std::size_t>(i)];
        if (std::fabs(xi) > 0.05 * mainAmplitude) {
            const double ratio = static_cast<double>(out[static_cast<std::size_t>(i)]) /
                                  static_cast<double>(xi);
            ratios.push_back(ratio);
            sum += ratio;
        }
    }
    REQUIRE(!ratios.empty());
    const double mean = sum / static_cast<double>(ratios.size());
    double maxDev = 0.0;
    for (double r : ratios) maxDev = std::max(maxDev, std::fabs(r - mean));
    return maxDev / std::fabs(mean);
}

// A freshly prepared CompressorCore with the shared tuning above and a given
// sidechain-HPF cutoff (0 = bypass). Returned by value: CompressorCore holds
// no atomics, matching compressor-test.cpp's makeCore() precedent.
CompressorCore makeCore(float scHpfHz) {
    CompressorCore core;
    core.prepare(kSampleRate, /*maxLookaheadSamples=*/0);
    core.setThreshold(kThresholdDb);
    core.setRatio(kRatio);
    core.setKnee(kKneeDb);
    core.setAttack(kAttackSeconds);
    core.setRelease(kReleaseSeconds);
    core.setSidechainHpf(scHpfHz);
    return core;
}

// Configure an already-declared CompressorEffect in place (never returned by
// value -- see the file-header note on why). Same tuning as makeCore(), plus
// prepare() sized for a single kTotalSamples-sample block.
void configureEffect(CompressorEffect& fx) {
    fx.prepare(ProcessContext{static_cast<double>(kSampleRate), kTotalSamples, 1});
    fx.setParameter(ParamId{CompressorEffect::kThreshold},
                    normalize(CompressorEffect::kParams[CompressorEffect::kThreshold],
                              kThresholdDb));
    fx.setParameter(ParamId{CompressorEffect::kRatio},
                    normalize(CompressorEffect::kParams[CompressorEffect::kRatio], kRatio));
    fx.setParameter(ParamId{CompressorEffect::kKnee},
                    normalize(CompressorEffect::kParams[CompressorEffect::kKnee], kKneeDb));
    fx.setParameter(ParamId{CompressorEffect::kAttack},
                    normalize(CompressorEffect::kParams[CompressorEffect::kAttack],
                              kAttackSeconds));
    fx.setParameter(ParamId{CompressorEffect::kRelease},
                    normalize(CompressorEffect::kParams[CompressorEffect::kRelease],
                              kReleaseSeconds));
}

// Drive a CompressorCore keyless (key == x, the internal-sidechain idiom)
// with one sine tone; returns the settled-tail RMS of the output.
double runKeylessCore(CompressorCore& core, double freqHz, double amplitude) {
    std::vector<float> x = sineBuffer(freqHz, amplitude, kTotalSamples);
    std::vector<float> out(x.size());
    for (std::size_t i = 0; i < x.size(); ++i) out[i] = core.process(x[i], x[i]);
    return rmsTail(out, kMeasureSamples);
}

// Drive a CompressorCore with an INDEPENDENT main (x) and key tone (the
// external-key idiom); returns the full x/out buffers (for both RMS and
// pointwise-ratio measurements).
struct KeyedRun {
    std::vector<float> x;
    std::vector<float> out;
};

KeyedRun runKeyedCore(CompressorCore& core, double mainFreqHz, double mainAmplitude,
                      double keyFreqHz, double keyAmplitude) {
    KeyedRun run;
    run.x = sineBuffer(mainFreqHz, mainAmplitude, kTotalSamples);
    const std::vector<float> key = sineBuffer(keyFreqHz, keyAmplitude, kTotalSamples);
    run.out.resize(run.x.size());
    for (std::size_t i = 0; i < run.x.size(); ++i) run.out[i] = core.process(run.x[i], key[i]);
    return run;
}

// Drive a configured CompressorEffect keyless (process(io), no sidechain
// block) with one sine tone; returns the settled-tail RMS of the output.
double runKeylessEffect(CompressorEffect& fx, double freqHz, double amplitude) {
    std::vector<float> buf = sineBuffer(freqHz, amplitude, kTotalSamples);
    float* chans[1] = {buf.data()};
    AudioBlock block(chans, 1, kTotalSamples);
    fx.process(block); // consumes the pending params published in configureEffect()
    return rmsTail(buf, kMeasureSamples);
}

// Drive a configured CompressorEffect via process(io, sidechain) with an
// INDEPENDENT main and key tone; returns the settled-tail RMS of the main
// (post-gain) output.
double runKeyedEffect(CompressorEffect& fx, double mainFreqHz, double mainAmplitude,
                      double keyFreqHz, double keyAmplitude) {
    std::vector<float> main = sineBuffer(mainFreqHz, mainAmplitude, kTotalSamples);
    std::vector<float> key = sineBuffer(keyFreqHz, keyAmplitude, kTotalSamples);
    float* mainChans[1] = {main.data()};
    float* keyChans[1] = {key.data()};
    AudioBlock io(mainChans, 1, kTotalSamples);
    AudioBlock sc(keyChans, 1, kTotalSamples);
    fx.process(io, sc);
    return rmsTail(main, kMeasureSamples);
}

} // namespace

// ===========================================================================
// T029 (US8) -- sidechain HPF (SC-006)
// ===========================================================================

TEST_CASE("CompressorCore - sidechain HPF at 120 Hz yields substantially less "
          "gain reduction for a below-cutoff tone than an above-cutoff tone at "
          "the same level; 0 Hz restores full-band detection (SC-006)") {
    constexpr float  kHpfHz  = 120.0f;
    constexpr double kLowHz  = 60.0;   // below cutoff
    constexpr double kHighHz = 1000.0; // well above cutoff
    constexpr double kAmplitude = 1.0; // 0 dBFS; 20 dB above threshold if undetected-filtered

    // Named tolerances (comparative, no fabricated absolute dB targets):
    constexpr double kReductionPresentDb = 3.0; // "reduction clearly happened"
    constexpr double kMarginDb           = 6.0; // "substantially less/more"

    // --- HPF engaged at 120 Hz ---------------------------------------------
    CompressorCore lowOn = makeCore(kHpfHz);
    const double grLowHpfOn = gainReductionDb(runKeylessCore(lowOn, kLowHz, kAmplitude),
                                              kAmplitude);

    CompressorCore highOn = makeCore(kHpfHz);
    const double grHighHpfOn = gainReductionDb(runKeylessCore(highOn, kHighHz, kAmplitude),
                                               kAmplitude);

    // Sanity: the above-cutoff tone actually crosses threshold and is reduced.
    CHECK(grHighHpfOn < -kReductionPresentDb);
    // AC1 (spec.md US8 scenario 1): 60 Hz produces SUBSTANTIALLY LESS
    // reduction than 1 kHz at the same level (the HPF removed it from
    // detection).
    CHECK(grLowHpfOn > grHighHpfOn + kMarginDb);

    // --- Bypass (scHpf = 0 Hz), SAME 60 Hz tone -----------------------------
    // AC2 (spec.md US8 scenario 2) is checked at a FIXED frequency (60 Hz)
    // rather than by comparing 60 Hz against 1 kHz here: the detector's
    // branching one-pole ballistics (fast 5 ms attack, slow 50 ms release)
    // settle to a mildly frequency-dependent level when a tone's own period
    // is comparable to those time constants (true at 60 Hz, negligible at
    // 1 kHz) -- an artifact of the ballistics, not of the HPF under test. Only
    // comparing HPF-on vs HPF-off AT THE SAME frequency isolates the filter's
    // effect from that artifact.
    CompressorCore lowOff = makeCore(0.0f);
    const double grLowHpfOff = gainReductionDb(runKeylessCore(lowOff, kLowHz, kAmplitude),
                                               kAmplitude);

    // Bypass must materially restore reduction at 60 Hz relative to the
    // HPF-engaged case (the HPF was suppressing it from detection; removing
    // the HPF removes that suppression).
    CHECK(grLowHpfOff < grLowHpfOn - kMarginDb);
    // And that restored reduction is itself clearly present -- i.e. bypass
    // detection of 60 Hz now behaves like a full-band, above-threshold tone,
    // not like a suppressed one.
    CHECK(grLowHpfOff < -kReductionPresentDb);
}

TEST_CASE("CompressorCore - the sidechain HPF only shapes detection; the main "
          "path is a pure scalar multiple of x, unfiltered (US8 independent "
          "test: main-path signal identity preserved except for gain)") {
    // If setSidechainHpf() were wired into the MAIN path instead of only the
    // key path, a filtered main tone would come out phase-shifted (not just
    // scaled) relative to its input, so the pointwise ratio out[n]/x[n] would
    // vary over the measurement window instead of holding at one constant
    // (the applied VCA gain). Drive an UNRELATED main tone (500 Hz) against a
    // loud KEY that differs per case (60 Hz vs 1 kHz, mirroring the case
    // above) so the applied gain differs, but the main path's own frequency
    // content never should.
    constexpr float  kHpfHz         = 120.0f;
    constexpr double kMainFreqHz    = 500.0;
    constexpr double kMainAmplitude = 0.5;
    constexpr double kKeyAmplitude  = 0.9; // loud: clearly above threshold if undetected-filtered
    constexpr double kRatioConstancyTol = 0.01; // 1%: main path is a pure scalar multiple of x

    CompressorCore withLowKey = makeCore(kHpfHz);
    const KeyedRun lowKeyRun = runKeyedCore(withLowKey, kMainFreqHz, kMainAmplitude,
                                            /*keyFreqHz=*/60.0, kKeyAmplitude);
    const double devLowKey = maxRelativeRatioDeviation(lowKeyRun.x, lowKeyRun.out,
                                                       kMeasureSamples, kMainAmplitude);
    CHECK(devLowKey < kRatioConstancyTol);

    CompressorCore withHighKey = makeCore(kHpfHz);
    const KeyedRun highKeyRun = runKeyedCore(withHighKey, kMainFreqHz, kMainAmplitude,
                                             /*keyFreqHz=*/1000.0, kKeyAmplitude);
    const double devHighKey = maxRelativeRatioDeviation(highKeyRun.x, highKeyRun.out,
                                                        kMeasureSamples, kMainAmplitude);
    CHECK(devHighKey < kRatioConstancyTol);
}

// ===========================================================================
// T031 (US9) -- external sidechain key (SC-007)
// ===========================================================================

TEST_CASE("CompressorCore::process(x, key) - a loud external key attenuates a "
          "quiet x (SC-007, core level)") {
    constexpr double kMainFreqHz    = 300.0;
    constexpr double kMainAmplitude = 0.01; // -40 dBFS: well below threshold on its own
    constexpr double kKeyFreqHz     = 1000.0;
    constexpr double kKeyAmplitude  = 0.5;  // -6 dBFS: well above the -20 dBFS threshold

    constexpr double kReductionPresentDb = 3.0;

    CompressorCore core = makeCore(/*scHpfHz=*/0.0f);
    const KeyedRun run = runKeyedCore(core, kMainFreqHz, kMainAmplitude, kKeyFreqHz,
                                      kKeyAmplitude);
    const double grKeyed = gainReductionDb(rmsTail(run.out, kMeasureSamples), kMainAmplitude);

    // The quiet x is attenuated because the LOUD key drove the detector above
    // threshold, even though x alone never would have.
    CHECK(grKeyed < -kReductionPresentDb);
}

TEST_CASE("CompressorEffect::process(io, sidechain) - an external key above "
          "threshold attenuates a quiet main signal according to the key's "
          "level, not the main's (SC-007)") {
    constexpr double kMainFreqHz    = 300.0;
    constexpr double kMainAmplitude = 0.01; // -40 dBFS
    constexpr double kKeyFreqHz     = 1000.0;
    constexpr double kLoudKeyAmplitude   = 0.3; // -10.5 dBFS: above threshold
    constexpr double kLouderKeyAmplitude = 0.9; // ~-1 dBFS: much more above threshold
    // (same key FREQUENCY for both amplitudes above -- the analytic GR gap
    // between them, ~0.75 * 20*log10(0.9/0.3) ~= 7.2 dB, is independent of any
    // ballistics-vs-frequency artifact since only the amplitude differs.)

    constexpr double kReductionPresentDb     = 3.0;
    constexpr double kNoReductionToleranceDb = 1.0;
    constexpr double kMarginDb               = 3.0; // "louder key => more reduction"

    // AC1 (spec.md US9 scenario 1): quiet main + loud external key => main is
    // attenuated per the KEY's level.
    CompressorEffect keyedFx;
    configureEffect(keyedFx);
    const double grKeyed = gainReductionDb(
        runKeyedEffect(keyedFx, kMainFreqHz, kMainAmplitude, kKeyFreqHz, kLoudKeyAmplitude),
        kMainAmplitude);
    CHECK(grKeyed < -kReductionPresentDb);

    // Reference: the SAME quiet main alone (process(io), no key) produces
    // little/no reduction (it never crosses threshold by itself) -- the
    // contrast confirms the reduction above tracked the KEY, not the main.
    CompressorEffect keylessFx;
    configureEffect(keylessFx);
    const double grKeyless = gainReductionDb(
        runKeylessEffect(keylessFx, kMainFreqHz, kMainAmplitude), kMainAmplitude);
    CHECK(std::fabs(grKeyless) < kNoReductionToleranceDb);
    CHECK(grKeyed < grKeyless - kMarginDb);

    // "Tracks the key level": a LOUDER key (main unchanged) drives MORE
    // reduction -- the main level plays no part in this, only the key does.
    CompressorEffect louderKeyedFx;
    configureEffect(louderKeyedFx);
    const double grLouderKeyed = gainReductionDb(
        runKeyedEffect(louderKeyedFx, kMainFreqHz, kMainAmplitude, kKeyFreqHz,
                       kLouderKeyAmplitude),
        kMainAmplitude);
    CHECK(grLouderKeyed < grKeyed - kMarginDb);
}

TEST_CASE("CompressorEffect::process(io) - with no external key supplied, "
          "detection reads the main input, so a quiet main gets little/no "
          "reduction (SC-007 fallback)") {
    constexpr double kMainFreqHz     = 300.0;
    constexpr double kQuietAmplitude = 0.01; // -40 dBFS: below threshold
    constexpr double kLoudAmplitude  = 0.5;  // -6 dBFS: above threshold, driving itself

    constexpr double kNoReductionToleranceDb = 1.0;
    constexpr double kReductionPresentDb     = 3.0;

    // A quiet main, keyless: detection falls back to the main input itself,
    // which never crosses threshold, so GR ~ 0.
    CompressorEffect quietFx;
    configureEffect(quietFx);
    const double grQuiet = gainReductionDb(runKeylessEffect(quietFx, kMainFreqHz, kQuietAmplitude),
                                           kQuietAmplitude);
    CHECK(std::fabs(grQuiet) < kNoReductionToleranceDb);

    // Sanity check on the SAME keyless path: a LOUD main (still keyless) DOES
    // cross threshold and IS reduced -- confirming the detector is genuinely
    // reading the main input (internal sidechain), not silently disabled.
    CompressorEffect loudFx;
    configureEffect(loudFx);
    const double grLoud = gainReductionDb(runKeylessEffect(loudFx, kMainFreqHz, kLoudAmplitude),
                                          kLoudAmplitude);
    CHECK(grLoud < -kReductionPresentDb);
}
