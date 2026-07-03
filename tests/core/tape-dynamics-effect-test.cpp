#include <doctest/doctest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <vector>

#include "dsp/audio-block.h"
#include "dsp/param-id.h"
#include "dsp/parameter.h"
#include "dsp/process-context.h"
#include "dsp/span.h"
#include "effects/tape-dynamics/tape-dynamics-effect.h"

// T017 -- User Story 1 acceptance suite: TapeDynamicsEffect (the host-facing
// Effect wrapper, T016) exercised BLACK-BOX -- only prepare()/process()/
// setParameter(), never TapeDynamicsCore/Hysteresis/Oversampler directly.
// Covers contracts/tape-dynamics-effect-api.md's E1 (memory), E2 (unity
// passthrough), E3 (finiteness), and the Effect-concept smoke surface, per
// spec.md User Stories 1.1/1.2/1.3 and FR-014.
//
// Mirrors the shipped compressor-effect-test.cpp / saturation-effect-test.cpp
// idiom (AudioBlock/ProcessContext construction, setParameter(ParamId,
// normalized) via the descriptor table, the pending-atomic "applied only at
// the NEXT process() call" handoff). Where a loop/memory technique already
// has a working precedent in this feature, this file reuses it verbatim
// through the Effect instead of re-deriving a new one:
//   - the rising-vs-falling ramp probe is tape-dynamics-core-test.cpp's own
//     "JA memory" SUBCASE, run here through TapeDynamicsEffect;
//   - the T016 oversampling-factor dispatch smoke case is kept as-is below.

using namespace acfx;

namespace {

// Convert a desired PLAIN-units value for a TapeDynamicsEffect parameter into
// the normalized 0..1 value setParameter() expects, via the shared
// descriptor table -- mirrors compressor-effect-test.cpp's normFor. Never
// hand-roll the normalize math here: the descriptor table (min/max/skew) is
// the single source of truth (FR-019).
float normFor(TapeDynamicsEffect::Param p, float plainValue) {
    return normalize(TapeDynamicsEffect::kParams[p], plainValue);
}

constexpr float kTwoPi = 2.0f * 3.14159265358979323846f;

} // namespace

// ---------------------------------------------------------------------------
// TEST 1: US1.1 / contract E1 -- at moderate drive the output is finite and
// saturated (a full-scale sinusoid's peak stays well under the linear
// ceiling driveLinear*amp), and shows hysteretic memory (the same nominal
// input level on a rising vs a falling field yields a different output).
// ---------------------------------------------------------------------------

TEST_CASE("TapeDynamicsEffect at moderate drive is saturated and shows hysteretic memory (US1.1, E1, T017)") {
    constexpr float kSampleRate = 48000.0f;
    constexpr float kDriveDb = 12.0f; // moderate (range is 0..24 dB, default 0)

    SUBCASE("full-scale sinusoid: finite output, saturated well under the linear ceiling") {
        constexpr int kStepsPerCycle = 480; // 100 Hz tone at 48 kHz
        constexpr int kCycles = 9;
        constexpr int kTotalSamples = kStepsPerCycle * kCycles;
        constexpr float kAmp = 1.0f; // full-scale (US1.1)

        TapeDynamicsEffect fx;
        fx.prepare(ProcessContext{static_cast<double>(kSampleRate), kTotalSamples, 1});
        fx.setParameter(ParamId{TapeDynamicsEffect::kDrive}, normFor(TapeDynamicsEffect::kDrive, kDriveDb));

        std::vector<float> buf(static_cast<std::size_t>(kTotalSamples));
        for (int n = 0; n < kTotalSamples; ++n) {
            const float phase = kTwoPi * (static_cast<float>(n) / static_cast<float>(kStepsPerCycle));
            buf[static_cast<std::size_t>(n)] = kAmp * std::sin(phase);
        }

        float* chans[1] = {buf.data()};
        AudioBlock block(chans, 1, kTotalSamples);
        fx.process(block); // applyPending() consumes the drive edit, THEN processes the whole buffer at 12 dB

        for (float sample : buf)
            REQUIRE(std::isfinite(sample));

        // Saturation proof: a purely linear stage would reach
        // driveLinear*kAmp at its peak; JA magnetization is bounded near the
        // default Ms=1.0 ceiling, so the SETTLED (last 2 cycles, past the
        // limit-cycle warm-up) peak must sit comfortably under that linear
        // ceiling. Named tolerance: 0.9x margin -- generous enough to never
        // be tripped by float rounding, tight enough to fail if the drive
        // stage were accidentally left unsaturated (a bug that just applied
        // driveLinear with no magnetics nonlinearity would land AT the
        // ceiling, above this bar).
        const double driveLinear = std::pow(10.0, static_cast<double>(kDriveDb) / 20.0);
        const int settledStart = kTotalSamples - 2 * kStepsPerCycle;
        float peakAbs = 0.0f;
        for (int n = settledStart; n < kTotalSamples; ++n)
            peakAbs = std::max(peakAbs, std::fabs(buf[static_cast<std::size_t>(n)]));
        INFO("peakAbs=" << peakAbs << " linearCeiling=" << driveLinear * static_cast<double>(kAmp));
        CHECK(static_cast<double>(peakAbs) < 0.9 * driveLinear * static_cast<double>(kAmp));
    }

    SUBCASE("rising vs falling field at the same nominal level yields different output (loop memory)") {
        // Reuses tape-dynamics-core-test.cpp's own "JA memory" ramp probe
        // (SUBCASE "JA memory: rising vs falling field give different
        // magnetization") verbatim in shape, run through the Effect wrapper
        // instead of TapeDynamicsCore<8> directly -- the default oversampling
        // bucket (index 2) IS TapeDynamicsCore<8>, so this is the same
        // composed signal path that SUBCASE was written against.
        constexpr int kSteps = 200;
        constexpr float kTarget = 0.5f;
        constexpr int kTotalSamples = 2 * (kSteps + 1);

        TapeDynamicsEffect fx;
        fx.prepare(ProcessContext{static_cast<double>(kSampleRate), kTotalSamples, 1});
        fx.setParameter(ParamId{TapeDynamicsEffect::kDrive}, normFor(TapeDynamicsEffect::kDrive, kDriveDb));

        std::vector<float> buf(static_cast<std::size_t>(kTotalSamples));
        for (int n = 0; n <= kSteps; ++n) // ramp UP: 0 -> kTarget
            buf[static_cast<std::size_t>(n)] = kTarget * static_cast<float>(n) / static_cast<float>(kSteps);
        for (int n = 0; n <= kSteps; ++n) // ramp DOWN: kTarget -> 0
            buf[static_cast<std::size_t>(kSteps + 1 + n)] =
                kTarget * static_cast<float>(kSteps - n) / static_cast<float>(kSteps);

        float* chans[1] = {buf.data()};
        AudioBlock block(chans, 1, kTotalSamples);
        fx.process(block); // applyPending() consumes the drive edit, THEN processes the whole ramp

        for (float sample : buf)
            REQUIRE(std::isfinite(sample));

        // Both branches pass through the SAME nominal input level
        // (kTarget/2) at their midpoint index -- the up-ramp at n=kSteps/2
        // (x = kTarget*0.5) and the down-ramp at the mirrored index
        // (x = kTarget*(kSteps - kSteps/2)/kSteps = kTarget*0.5 too).
        const float risingAtHalf = buf[static_cast<std::size_t>(kSteps / 2)];
        const float fallingAtHalf = buf[static_cast<std::size_t>(kSteps + 1 + kSteps / 2)];
        // Named tolerance: mirrors tape-dynamics-core-test.cpp's own
        // 1.0e-4f bar for "the loop opens" -- far above any float-rounding
        // floor, well below the actual JA loop separation at width=1.0's
        // "genuinely open loop" default (that file's own comment).
        INFO("risingAtHalf=" << risingAtHalf << " fallingAtHalf=" << fallingAtHalf);
        CHECK(std::fabs(risingAtHalf - fallingAtHalf) > 1.0e-4f);
    }
}

// ---------------------------------------------------------------------------
// TEST 2: US1.2 / contract E2 / FR-014 -- the effect's REAL, guaranteed
// unity-passthrough semantics. mix=0 (bypass) is an exact dry passthrough
// regardless of drive/saturation/width -- this is the actual FR-014
// guarantee. drive=0 dB ALONE (mix=1, the T016 default) is documented to NOT
// be a unity passthrough: T016's own comment scopes "unity" to "unity into
// the magnetics" (no extra pre-gain), not "the whole chain is transparent" --
// the default saturation=1.0/width=1.0 magnetics (Ms=k=1) still colors the
// signal at drive=0. This is the implementation's ACTUAL behavior, asserted
// directly rather than weakened to force a naive "drive=0 => output==input"
// expectation to pass.
// ---------------------------------------------------------------------------

TEST_CASE("TapeDynamicsEffect bypass/unity-passthrough semantics (US1.2, E2, FR-014, T017)") {
    constexpr float kSampleRate = 48000.0f;
    constexpr int kBlockSize = 256;

    SUBCASE("mix=0 bypass: output == input, bit-exact, regardless of drive/saturation/width") {
        TapeDynamicsEffect fx;
        fx.prepare(ProcessContext{static_cast<double>(kSampleRate), kBlockSize, 1});

        // Push every other macro to its most aggressive corner so a bug that
        // ignored mix would be loudly visible (mirrors
        // compressor-effect-test.cpp TEST 4's "push threshold/ratio to the
        // corner" idiom).
        fx.setParameter(ParamId{TapeDynamicsEffect::kDrive}, normFor(TapeDynamicsEffect::kDrive, 24.0f));
        fx.setParameter(ParamId{TapeDynamicsEffect::kSaturation}, normFor(TapeDynamicsEffect::kSaturation, 2.0f));
        fx.setParameter(ParamId{TapeDynamicsEffect::kWidth}, normFor(TapeDynamicsEffect::kWidth, 0.1f));
        fx.setParameter(ParamId{TapeDynamicsEffect::kMix}, 0.0f); // denormalize(0.0) == mix.min == 0.0 exactly

        std::vector<float> in(static_cast<std::size_t>(kBlockSize));
        for (int i = 0; i < kBlockSize; ++i) {
            const float t = static_cast<float>(i) / kSampleRate;
            in[static_cast<std::size_t>(i)] = 0.9f * std::sin(kTwoPi * 1000.0f * t);
        }
        std::vector<float> out = in;
        float* chans[1] = {out.data()};
        AudioBlock block(chans, 1, kBlockSize);
        fx.process(block); // applyPending() consumes drive/saturation/width/mix here

        // TapeDynamicsCore::processSample(): y = mix*wet + (1-mix)*dry, then
        // *outputGain (0 dB default -> 1.0 exactly). mix denormalized to
        // EXACTLY 0.0f zeroes the wet term (0.0f*wet == 0.0f for any finite
        // wet), leaving y == dry -- bit-for-bit, no artifacts. Named
        // tolerance: 1e-6 epsilon, purely to absorb float-store rounding,
        // not because any DSP tolerance is actually needed here.
        for (int i = 0; i < kBlockSize; ++i) {
            INFO("sample=" << i);
            CHECK(out[static_cast<std::size_t>(i)] ==
                  doctest::Approx(in[static_cast<std::size_t>(i)]).epsilon(1.0e-6));
        }
    }

    SUBCASE("drive=0 dB ALONE (mix=1 default) is NOT unity passthrough: the magnetics still colors") {
        // At the T016 defaults (saturation=1.0 -> Ms=1, width=1.0 -> k=1),
        // the JA anhysteretic curve M = Ms*L(H/a) has slope L'(0) = 1/3 at
        // the origin (langevinDeriv's small-x series in hysteresis.h), not
        // 1 -- so even drive=0's unity-into-the-magnetics input is NOT
        // reproduced at unity gain by the magnetics stage itself. This
        // documents that reality directly: the FR-014 guarantee is realized
        // via the mix=0 bypass path above, not by "drive=0" in isolation.
        TapeDynamicsEffect fx;
        fx.prepare(ProcessContext{static_cast<double>(kSampleRate), kBlockSize, 1});
        // drive/mix set explicitly to their T016 defaults (0 dB, fully wet)
        // via the SAME publish/applyPending path the other cases use, not a
        // reliance on the constructor's raw member values.
        fx.setParameter(ParamId{TapeDynamicsEffect::kDrive}, normFor(TapeDynamicsEffect::kDrive, 0.0f));
        fx.setParameter(ParamId{TapeDynamicsEffect::kMix}, normFor(TapeDynamicsEffect::kMix, 1.0f));

        std::vector<float> in(static_cast<std::size_t>(kBlockSize));
        for (int i = 0; i < kBlockSize; ++i) {
            const float t = static_cast<float>(i) / kSampleRate;
            in[static_cast<std::size_t>(i)] = 0.5f * std::sin(kTwoPi * 200.0f * t);
        }
        std::vector<float> out = in;
        float* chans[1] = {out.data()};
        AudioBlock block(chans, 1, kBlockSize);
        fx.process(block);

        for (float v : out)
            REQUIRE(std::isfinite(v));

        double sumSqDiff = 0.0;
        for (int i = 0; i < kBlockSize; ++i) {
            const double d = static_cast<double>(out[static_cast<std::size_t>(i)]) -
                              static_cast<double>(in[static_cast<std::size_t>(i)]);
            sumSqDiff += d * d;
        }
        const double rmsDiff = std::sqrt(sumSqDiff / kBlockSize);
        // Named tolerance: RMS(output-input) over a 0.5-amplitude, 200 Hz
        // tone must exceed 5% of the input amplitude -- comfortably above
        // any float-rounding floor, proving the magnetics stage measurably
        // colors the signal at drive=0/mix=1 rather than passing it
        // through unchanged.
        constexpr double kColorationFloor = 0.05 * 0.5;
        INFO("rmsDiff=" << rmsDiff << " floor=" << kColorationFloor);
        CHECK(rmsDiff > kColorationFloor);
    }
}

// ---------------------------------------------------------------------------
// TEST 3: US1.3 / contract E3 -- a hot transient (adversarial, far outside
// the nominal +-1 range) followed by a quiet recovery tail stays finite for
// EVERY sample, at an extreme parameter corner, across all three solvers
// (rk2/rk4/newtonRaphson) -- "no finite input, at any parameter setting or
// solver, produces NaN/Inf" (contract E3).
// ---------------------------------------------------------------------------

TEST_CASE("TapeDynamicsEffect stays finite through a hot transient, every solver (US1.3, E3, T017)") {
    constexpr float kSampleRate = 48000.0f;
    constexpr int kTransientSamples = 64;  // adversarial alternating-sign burst
    constexpr int kRecoverySamples = 4800; // 100 ms quiet tail
    constexpr int kTotalSamples = kTransientSamples + kRecoverySamples;
    constexpr float kTransientAmp = 50.0f; // far outside the nominal +-1 range
    constexpr float kRecoveryAmp = 0.1f;
    // kTapeDynamicsSolverLabels order: {"rk2", "rk4", "newtonRaphson"} ==
    // TapeDynamicsEffect::toSolver()'s bucket mapping.
    constexpr std::array<float, 3> kSolverBuckets = {0.0f, 1.0f, 2.0f};

    for (float solverBucket : kSolverBuckets) {
        TapeDynamicsEffect fx;
        fx.prepare(ProcessContext{static_cast<double>(kSampleRate), kTotalSamples, 1});
        fx.setParameter(ParamId{TapeDynamicsEffect::kSolver}, normFor(TapeDynamicsEffect::kSolver, solverBucket));
        // Extreme parameter corner (max drive/saturation, min width -- a
        // narrow/stiff loop) STACKED on top of the hot transient, per E3's
        // "at any parameter setting or solver" (not just default params).
        fx.setParameter(ParamId{TapeDynamicsEffect::kDrive}, normFor(TapeDynamicsEffect::kDrive, 24.0f));
        fx.setParameter(ParamId{TapeDynamicsEffect::kSaturation}, normFor(TapeDynamicsEffect::kSaturation, 2.0f));
        fx.setParameter(ParamId{TapeDynamicsEffect::kWidth}, normFor(TapeDynamicsEffect::kWidth, 0.1f));

        std::vector<float> buf(static_cast<std::size_t>(kTotalSamples));
        for (int n = 0; n < kTransientSamples; ++n) // alternating hot spikes: max |dH| every sample
            buf[static_cast<std::size_t>(n)] = (n % 2 == 0) ? kTransientAmp : -kTransientAmp;
        for (int n = kTransientSamples; n < kTotalSamples; ++n) {
            const float t = static_cast<float>(n) / kSampleRate;
            buf[static_cast<std::size_t>(n)] = kRecoveryAmp * std::sin(kTwoPi * 300.0f * t);
        }

        float* chans[1] = {buf.data()};
        AudioBlock block(chans, 1, kTotalSamples);
        fx.process(block); // applyPending() consumes solver+drive+saturation+width, then processes the adversarial buffer

        for (int n = 0; n < kTotalSamples; ++n) {
            INFO("solverBucket=" << solverBucket << " n=" << n);
            REQUIRE(std::isfinite(buf[static_cast<std::size_t>(n)]));
        }

        // Recovery/boundedness: Hysteresis::process()'s stiff-solver guard
        // clamps |M| <= kMBoundMultiplier(4)*Ms; at saturation=2.0 that
        // bound is 8.0, and output=0 dB default leaves outputGain==1.
        // Named tolerance: a 2x margin above that documented guard bound
        // tolerates the oversampler's own filter overshoot on a hot
        // transient without asserting an exact DSP recovery curve.
        constexpr double kGuardBoundMultiplier = 4.0; // Hysteresis::kMBoundMultiplier (hysteresis.h)
        constexpr double kMaxSaturation = 2.0;         // this run's Ms (kSaturation set above)
        constexpr double kRecoveryBound = 2.0 * kGuardBoundMultiplier * kMaxSaturation;
        for (int n = kTotalSamples - 480; n < kTotalSamples; ++n) { // last 10 ms
            INFO("solverBucket=" << solverBucket << " n=" << n);
            CHECK(std::fabs(static_cast<double>(buf[static_cast<std::size_t>(n)])) < kRecoveryBound);
        }
    }
}

// ---------------------------------------------------------------------------
// TEST 4: Effect-concept smoke (T016/T017) -- prepare()+process() runs to
// completion and produces continuous, finite output across block sizes
// (including a 1-sample block and a large block). The analytic invariant
// asserted: TapeDynamicsCore::processSample() carries no block-level state
// (every stage -- Oversampler, Hysteresis -- is purely per-sample and
// stateful ACROSS calls), so the SAME total sample stream must be
// bit-identical whether processed as one big block or chunked into many
// small ones. This is a stronger, analytically-grounded claim than "just
// check finite" for the 1-sample-block case FR-008/E8 calls out.
// ---------------------------------------------------------------------------

TEST_CASE("TapeDynamicsEffect output is block-size-invariant, including 1-sample blocks (Effect concept smoke, T016/T017)") {
    constexpr float kSampleRate = 48000.0f;
    constexpr int kTotalSamples = 2000;
    constexpr float kDriveDb = 6.0f;

    std::vector<float> dry(static_cast<std::size_t>(kTotalSamples));
    for (int n = 0; n < kTotalSamples; ++n) {
        const float t = static_cast<float>(n) / kSampleRate;
        dry[static_cast<std::size_t>(n)] = 0.7f * std::sin(kTwoPi * 440.0f * t);
    }

    // Reference: the whole stream in ONE process() call.
    TapeDynamicsEffect reference;
    reference.prepare(ProcessContext{static_cast<double>(kSampleRate), kTotalSamples, 1});
    reference.setParameter(ParamId{TapeDynamicsEffect::kDrive}, normFor(TapeDynamicsEffect::kDrive, kDriveDb));
    std::vector<float> refOut = dry;
    {
        float* chans[1] = {refOut.data()};
        AudioBlock block(chans, 1, kTotalSamples);
        reference.process(block);
    }
    for (float v : refOut)
        REQUIRE(std::isfinite(v));

    // Chunked: the IDENTICAL stream split across varying block sizes,
    // cycling through {1, 1, 7, 64, 512} -- exercises a 1-sample block
    // explicitly, plus a large (512-sample) block, within the same run. The
    // drive setParameter() is published once, before either instance's
    // first process() call, so both apply it starting from sample 0
    // (applyPending() runs at the top of process(), before that call's own
    // samples are processed) -- the two runs' parameter-application timing
    // is identical, only the chunking differs.
    TapeDynamicsEffect chunked;
    chunked.prepare(ProcessContext{static_cast<double>(kSampleRate), kTotalSamples, 1});
    chunked.setParameter(ParamId{TapeDynamicsEffect::kDrive}, normFor(TapeDynamicsEffect::kDrive, kDriveDb));
    std::vector<float> chunkedOut = dry;
    constexpr std::array<int, 5> kChunkSizes = {1, 1, 7, 64, 512};
    std::size_t offset = 0;
    std::size_t chunkIdx = 0;
    while (offset < static_cast<std::size_t>(kTotalSamples)) {
        int size = kChunkSizes[chunkIdx % kChunkSizes.size()];
        ++chunkIdx;
        const std::size_t remaining = static_cast<std::size_t>(kTotalSamples) - offset;
        if (static_cast<std::size_t>(size) > remaining)
            size = static_cast<int>(remaining);
        float* chans[1] = {chunkedOut.data() + offset};
        AudioBlock block(chans, 1, size);
        chunked.process(block);
        offset += static_cast<std::size_t>(size);
    }

    REQUIRE(chunkedOut.size() == refOut.size());
    for (std::size_t i = 0; i < chunkedOut.size(); ++i) {
        INFO("i=" << i);
        REQUIRE(std::isfinite(chunkedOut[i]));
        CHECK(chunkedOut[i] == doctest::Approx(refOut[i]).epsilon(1.0e-6));
    }
}

// T016 smoke check (T017 owns the real suite above): prepare()+process()
// must run a block to completion and produce finite output for every
// oversampling bucket (2x/4x/8x), proving the factor dispatch wired up in
// processBlock() actually reaches all three TapeDynamicsCore instances.
TEST_CASE("TapeDynamicsEffect prepare+process produces finite output for every oversampling factor (T016 smoke)") {
    for (float bucket : {0.0f, 1.0f, 2.0f}) {
        TapeDynamicsEffect fx;
        const ProcessContext ctx{48000.0, 64, 1};
        fx.prepare(ctx);
        fx.setParameter(TapeDynamicsEffect::kParams[TapeDynamicsEffect::kOversampling].id,
                         normalize(TapeDynamicsEffect::kParams[TapeDynamicsEffect::kOversampling], bucket));

        std::vector<float> buf(64);
        for (std::size_t i = 0; i < buf.size(); ++i)
            buf[i] = 0.5f * std::sin(static_cast<float>(i) * 0.1f);

        std::array<float*, 1> channels{buf.data()};
        AudioBlock block(channels.data(), 1, static_cast<int>(buf.size()));

        // First call consumes the (default) pending params; second call
        // consumes the just-set oversampling bucket and actually processes
        // through the newly-selected core.
        fx.process(block);
        fx.process(block);

        for (float sample : buf)
            CHECK(std::isfinite(sample));

        fx.reset();
    }
}
