#include <doctest/doctest.h>

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "dsp/audio-block.h"
#include "dsp/param-id.h"
#include "dsp/parameter.h"
#include "dsp/process-context.h"
#include "effects/saturation/saturation-core.h"    // SaturationVoicing / SaturationQuality
#include "effects/saturation/saturation-effect.h"  // the shipped static reference (US3)
#include "effects/program-dependent-saturation/program-dependent-saturation-effect.h"

// T017 -- User Story 3 suite: the load-bearing ZERO-DEPTH ORTHOGONALITY
// contract (FR-007, SC-002). With every modulation depth = 0 and
// dynamicPreset = none, ProgramDependentSaturationEffect MUST reproduce the
// shipped static SaturationEffect at the SAME static parameters -- byte-for-
// byte where the paths coincide.
//
// WHY EXACT (== 0 diff) IS THE RIGHT TOLERANCE, NOT A LOOSE EPSILON. The two
// effects wrap the SAME SaturationCore. At zero depth the PDS core takes the
// additive path base + DynamicsModulator::modulate(env)*span, and
// DynamicsModulator::modulate returns EXACTLY 0.0f when depth == 0 (it is
// depth_ * curve(env); 0.0f * finite == +0.0f). So:
//   drive' = clamp(staticDriveDb + 0.0f*48, 0, 48) == staticDriveDb (in range)
//   bias'  = clamp(staticBias   + 0.0f,     -1, 1) == staticBias
//   mix'   = clamp(staticMix    + 0.0f,      0, 1) == staticMix
// and the PDS core's dbToGain() mirrors SaturationEffect::dbToGain() exactly,
// so setDrive(dbToGain(staticDriveDb)) receives a BIT-IDENTICAL gain to the one
// SaturationEffect pushes. The PDS wrapper re-pushes setDrive/setBias/setMix
// every sample, but Waveshaper::setDrive / setBias / SaturationCore::setMix only
// store scalars (+ a deterministic gain-comp factor); they never touch DC-block
// or ADAA history (waveshaper.h / adaa-waveshaper.h), so per-sample re-pushing
// leaves state BIT-IDENTICAL to a single push. tone is per-block, and PDS's
// newBlock() is a no-op at tone depth 0 (the static tilt set by setStaticTone
// stays in effect). Therefore the two SaturationCores hold bit-identical applied
// state + filter coefficients and see bit-identical input -> bit-identical
// output. Exact equality is provable, so this suite asserts maxAbsDiff == 0.
//
// Static-parameter alignment (verified against both param tables): PDS ids 0..6
// are drive,voicing,tone,mix,output,bias,quality with the SAME order, units, and
// ranges as SaturationEffect's seven params (program-dependent-saturation-
// parameters.h rows 0..6 mirror saturation-effect.h kParams). So the SAME
// normalized value on id k means the same thing on both effects; this suite
// normalizes each plain value through each effect's OWN descriptor row.
//
// Deterministic stimuli only (fixed sine / chirp / LCG-noise / impulse burst) --
// no per-run randomness (measurement-support.h integer-cycle-window discipline
// is not required here: this is an exact effect-vs-effect identity, not a
// spectral readout).

using namespace acfx;

namespace {

constexpr double      kSampleRate  = 48000.0;
constexpr std::size_t kNumSamples  = 4096;

// ---------------------------------------------------------------------------
// A matched static-parameter configuration applied identically to both effects
// (plain units; discrete params carry their bucket index as a float).
// ---------------------------------------------------------------------------
struct StaticConfig {
    const char*       label;
    float             driveDb;    // 0..48
    int               voicing;    // 0..3 (softClip/tape/console/tubePreamp)
    float             tonePlain;  // -1..1
    float             mixPlain;   // 0..1
    float             outputDb;   // -24..24
    float             biasPlain;  // -1..1
    int               quality;    // 0..2 (naive/adaa/oversampled)
};

// The battery of static settings -- several voicings, a couple of drives,
// tone != 0, bias != 0, mix < 1, and a non-adaa quality, to prove orthogonality
// holds for ALL static settings, not just the defaults.
constexpr StaticConfig kConfigs[] = {
    {"defaults (softClip/adaa/unity)",   0.0f, 0,  0.0f, 1.0f,  0.0f,  0.0f, 1},
    {"tape/adaa tone+bias",             12.0f, 1,  0.4f, 1.0f, -3.0f,  0.3f, 1},
    {"console/naive mix<1 tone-/bias-", 24.0f, 2, -0.5f, 0.6f,  6.0f, -0.4f, 0},
    {"tubePreamp/adaa mix<1",            6.0f, 3,  0.0f, 0.8f,  0.0f,  0.2f, 1},
    {"softClip/naive hot tone-/bias+",  30.0f, 0, -0.8f, 0.5f, -6.0f,  0.5f, 0},
};

// Configure a SaturationEffect or a ProgramDependentSaturationEffect with the
// same static parameters. Templated over the effect type: both expose the same
// Param enumerators (kDrive..kQuality) and the same kParams descriptor rows for
// ids 0..6, and both take setParameter(ParamId, normalized). The PDS effect's
// modulation depths / dynamicPreset are left at their construction defaults
// (all depths = 0, dynamicPreset = none), which IS the orthogonality baseline.
template <class Fx>
void configureStatic(Fx& fx, const StaticConfig& c) {
    fx.setParameter(ParamId{Fx::kDrive},
                    normalize(Fx::kParams[Fx::kDrive], c.driveDb));
    fx.setParameter(ParamId{Fx::kVoicing},
                    normalize(Fx::kParams[Fx::kVoicing], static_cast<float>(c.voicing)));
    fx.setParameter(ParamId{Fx::kTone},
                    normalize(Fx::kParams[Fx::kTone], c.tonePlain));
    fx.setParameter(ParamId{Fx::kMix},
                    normalize(Fx::kParams[Fx::kMix], c.mixPlain));
    fx.setParameter(ParamId{Fx::kOutput},
                    normalize(Fx::kParams[Fx::kOutput], c.outputDb));
    fx.setParameter(ParamId{Fx::kBias},
                    normalize(Fx::kParams[Fx::kBias], c.biasPlain));
    fx.setParameter(ParamId{Fx::kQuality},
                    normalize(Fx::kParams[Fx::kQuality], static_cast<float>(c.quality)));
}

// ---------------------------------------------------------------------------
// Deterministic stimuli (fixed generators; identical every run).
// ---------------------------------------------------------------------------

// Pure 1 kHz tone at 0.6 amplitude.
std::vector<float> makeSine() {
    std::vector<float> b(kNumSamples, 0.0f);
    constexpr double kPi = 3.14159265358979323846;
    const double w = 2.0 * kPi * 1000.0 / kSampleRate;
    for (std::size_t n = 0; n < kNumSamples; ++n)
        b[n] = 0.6f * static_cast<float>(std::sin(w * static_cast<double>(n)));
    return b;
}

// Linear chirp sweeping 100 Hz -> 8 kHz across the buffer (phase integrated).
std::vector<float> makeChirp() {
    std::vector<float> b(kNumSamples, 0.0f);
    constexpr double kPi = 3.14159265358979323846;
    const double f0 = 100.0, f1 = 8000.0;
    double phase = 0.0;
    for (std::size_t n = 0; n < kNumSamples; ++n) {
        const double t = static_cast<double>(n) / static_cast<double>(kNumSamples - 1);
        const double f = f0 + (f1 - f0) * t;
        b[n] = 0.6f * static_cast<float>(std::sin(phase));
        phase += 2.0 * kPi * f / kSampleRate;
    }
    return b;
}

// Deterministic white-ish noise from a fixed-seed LCG (Numerical Recipes
// constants). No std::rand / no per-run variation -- byte-identical every run.
std::vector<float> makeNoise() {
    std::vector<float> b(kNumSamples, 0.0f);
    std::uint32_t s = 0x13572468u; // fixed seed
    for (std::size_t n = 0; n < kNumSamples; ++n) {
        s = s * 1664525u + 1013904223u;
        const float u = static_cast<float>(s >> 8) / static_cast<float>(1u << 24); // [0,1)
        b[n] = 0.5f * (2.0f * u - 1.0f); // [-0.5, 0.5)
    }
    return b;
}

// Transient/impulse burst: silence with periodic alternating-sign impulses plus
// a short exponentially-decaying tail after each -- exercises the DC-blocker and
// the envelope detector's attack/release without a sustained tone.
std::vector<float> makeImpulseBurst() {
    std::vector<float> b(kNumSamples, 0.0f);
    for (std::size_t hit = 0; hit * 700 < kNumSamples; ++hit) {
        const std::size_t at = hit * 700;
        const float sign = (hit % 2 == 0) ? 1.0f : -1.0f;
        for (std::size_t k = 0; k < 32 && at + k < kNumSamples; ++k)
            b[at + k] += sign * 0.9f * std::exp(-static_cast<float>(k) * 0.35f);
    }
    return b;
}

// ---------------------------------------------------------------------------
// Difference measure between two equal-length buffers.
// ---------------------------------------------------------------------------
struct Diff {
    double      maxAbs      = 0.0;
    std::size_t firstBadIdx = static_cast<std::size_t>(-1);
    std::size_t mismatches  = 0;
    bool        anyNaN      = false;
};

Diff compare(const std::vector<float>& a, const std::vector<float>& b) {
    Diff d;
    const std::size_t n = a.size() < b.size() ? a.size() : b.size();
    for (std::size_t i = 0; i < n; ++i) {
        if (std::isnan(a[i]) || std::isnan(b[i]))
            d.anyNaN = true;
        const double diff = std::abs(static_cast<double>(a[i]) - static_cast<double>(b[i]));
        if (diff > d.maxAbs)
            d.maxAbs = diff;
        if (a[i] != b[i]) {
            ++d.mismatches;
            if (d.firstBadIdx == static_cast<std::size_t>(-1))
                d.firstBadIdx = i;
        }
    }
    return d;
}

// Process an entire buffer as a single mono block (applyPending() consumes every
// pending edit at the top of the first process()).
template <class Fx>
void processBuffer(Fx& fx, std::vector<float>& buf) {
    float* chans[1] = {buf.data()};
    AudioBlock block(chans, 1, static_cast<int>(buf.size()));
    fx.process(block);
}

double rms(const std::vector<float>& b) {
    double s = 0.0;
    for (float v : b) s += static_cast<double>(v) * static_cast<double>(v);
    return b.empty() ? 0.0 : std::sqrt(s / static_cast<double>(b.size()));
}

} // namespace

// ---------------------------------------------------------------------------
// TEST 1: zero-depth orthogonality is byte-for-byte across every static
// configuration and every stimulus (FR-007, SC-002, US3 acceptance scenario 1).
// ---------------------------------------------------------------------------
TEST_CASE("[T017][US3] PDS at zero depth equals static SaturationEffect byte-for-byte (FR-007/SC-002)") {
    struct Stim { const char* name; std::vector<float> (*make)(); };
    const Stim stimuli[] = {
        {"sine-1kHz", &makeSine},
        {"chirp-100-8k", &makeChirp},
        {"lcg-noise", &makeNoise},
        {"impulse-burst", &makeImpulseBurst},
    };

    for (const StaticConfig& cfg : kConfigs) {
        for (const Stim& stim : stimuli) {
            INFO("config=" << cfg.label << " stimulus=" << stim.name);

            SaturationEffect ref;
            ProgramDependentSaturationEffect pds;
            ref.prepare(ProcessContext{kSampleRate, static_cast<int>(kNumSamples), 1});
            pds.prepare(ProcessContext{kSampleRate, static_cast<int>(kNumSamples), 1});
            configureStatic(ref, cfg);
            configureStatic(pds, cfg);
            // Leave ALL PDS depths (driveDepth/biasDepth/toneDepth/mixDepth) and
            // dynamicPreset at their construction defaults (0 / none) -- the
            // orthogonality baseline -- so this is the pure zero-depth path.

            std::vector<float> a = stim.make();
            std::vector<float> b = a; // identical input to both effects
            processBuffer(ref, a);
            processBuffer(pds, b);

            const Diff d = compare(a, b);
            INFO("maxAbsDiff=" << d.maxAbs << " mismatches=" << d.mismatches
                               << " firstBadIdx=" << static_cast<long long>(d.firstBadIdx));
            CHECK_FALSE(d.anyNaN);
            // EXACT: the composed paths coincide bit-for-bit at zero depth (see
            // the file header proof). Any non-zero diff here is a CRITICAL
            // orthogonality regression, not a tolerance question.
            CHECK(d.maxAbs == 0.0);
            CHECK(d.mismatches == 0);
        }
    }
}

// ---------------------------------------------------------------------------
// TEST 2: sanity that the identity is non-trivial -- the reference effect must
// actually DO something (non-silent output) so TEST 1's equality is meaningful
// and not two silent buffers matching vacuously.
// ---------------------------------------------------------------------------
TEST_CASE("[T017][US3] the zero-depth orthogonality identity is over non-silent output") {
    const StaticConfig& cfg = kConfigs[1]; // tape/adaa, tone+bias, drive 12 dB

    SaturationEffect ref;
    ref.prepare(ProcessContext{kSampleRate, static_cast<int>(kNumSamples), 1});
    configureStatic(ref, cfg);

    std::vector<float> a = makeSine();
    processBuffer(ref, a);
    CHECK(rms(a) > 1.0e-3); // the saturator produced real, audible output
}

// ---------------------------------------------------------------------------
// TEST 3: modulation is ADDITIVE on a static base (US3 acceptance scenario 2 /
// FR-007). A SINGLE non-zero depth on ONE target (drive) modulates ONLY that
// target: with driveDepth > 0 the output DIFFERS from the static base, while
// with driveDepth = 0 it matches byte-for-byte. Proves the dynamic layer never
// colors the sound until a depth is dialed in.
// ---------------------------------------------------------------------------
TEST_CASE("[T017][US3] a single non-zero drive depth diverges from static; zero depth matches (FR-007)") {
    // A hot, sustained tone so the detected envelope climbs into the
    // normalization window and the drive modulation is unmistakable.
    const StaticConfig cfg = {"drive-mod base", 6.0f, 1 /*tape*/, 0.0f, 1.0f, 0.0f, 0.0f, 1 /*adaa*/};

    std::vector<float> input(kNumSamples, 0.0f);
    constexpr double kPi = 3.14159265358979323846;
    const double w = 2.0 * kPi * 1000.0 / kSampleRate;
    for (std::size_t n = 0; n < kNumSamples; ++n)
        input[n] = 0.7f * static_cast<float>(std::sin(w * static_cast<double>(n)));

    // Static reference.
    std::vector<float> refOut = input;
    {
        SaturationEffect ref;
        ref.prepare(ProcessContext{kSampleRate, static_cast<int>(kNumSamples), 1});
        configureStatic(ref, cfg);
        processBuffer(ref, refOut);
    }

    // PDS with driveDepth = 0 (all depths 0): must equal the static reference.
    std::vector<float> zeroDepthOut = input;
    {
        ProgramDependentSaturationEffect pds;
        pds.prepare(ProcessContext{kSampleRate, static_cast<int>(kNumSamples), 1});
        configureStatic(pds, cfg);
        // driveDepth left at default 0.
        processBuffer(pds, zeroDepthOut);
    }
    {
        const Diff d = compare(refOut, zeroDepthOut);
        INFO("zero-depth maxAbsDiff=" << d.maxAbs << " mismatches=" << d.mismatches);
        CHECK_FALSE(d.anyNaN);
        CHECK(d.maxAbs == 0.0);
        CHECK(d.mismatches == 0);
    }

    // PDS with a single non-zero driveDepth (+1.0): must DIVERGE from static --
    // and ONLY because drive is modulated (bias/tone/mix depths stay 0).
    std::vector<float> driveDepthOut = input;
    {
        ProgramDependentSaturationEffect pds;
        pds.prepare(ProcessContext{kSampleRate, static_cast<int>(kNumSamples), 1});
        configureStatic(pds, cfg);
        pds.setParameter(
            ParamId{ProgramDependentSaturationEffect::kDriveDepth},
            normalize(ProgramDependentSaturationEffect::kParams[ProgramDependentSaturationEffect::kDriveDepth],
                      1.0f)); // full positive drive depth
        processBuffer(pds, driveDepthOut);
    }
    {
        const Diff d = compare(refOut, driveDepthOut);
        INFO("drive-depth maxAbsDiff=" << d.maxAbs << " mismatches=" << d.mismatches);
        CHECK_FALSE(d.anyNaN);
        // A non-zero depth on a live envelope MUST change the output. The offset
        // ramps in with the envelope (attack), so a substantial fraction of the
        // block differs; require a clearly non-trivial divergence.
        CHECK(d.maxAbs > 1.0e-3);
        CHECK(d.mismatches > kNumSamples / 4);
    }
}
