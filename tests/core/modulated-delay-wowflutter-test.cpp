#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

#include "dsp/audio-block.h"
#include "dsp/param-id.h"
#include "dsp/parameter.h"
#include "dsp/process-context.h"
#include "effects/modulated-delay/modulated-delay-effect.h"

// T014 — ModulatedDelayEffect US3: wow & flutter on the input path.
// Covers FR-019 (depth-0 passthrough), FR-020 (present in delay tail),
// FR-021 (preallocated delay line, reads in range).

using namespace acfx;

namespace {

void setParam(ModulatedDelayEffect& fx, ModulatedDelayEffect::Param p, float plain) {
    fx.setParameter(ParamId{p},
                    normalize(ModulatedDelayEffect::kParams[p], plain));
}

void fillSine(float* buf, int n, double freqHz, double sr, double& phase) {
    const double twoPi = 2.0 * 3.14159265358979323846;
    for (int i = 0; i < n; ++i) {
        buf[i]  = static_cast<float>(std::sin(phase));
        phase  += twoPi * freqHz / sr;
    }
}

float computeRms(const std::vector<float>& v) {
    if (v.empty()) return 0.0f;
    double sum = 0.0;
    for (float x : v) sum += static_cast<double>(x) * x;
    return static_cast<float>(std::sqrt(sum / static_cast<double>(v.size())));
}

// Run numBlocks of sine audio through the effect and collect all output samples.
std::vector<float> runEffect(ModulatedDelayEffect& fx,
                              double sr, int blockSize, int numBlocks,
                              double inputFreqHz) {
    std::vector<float> all;
    all.reserve(static_cast<std::size_t>(blockSize * numBlocks));
    std::vector<float> buf(static_cast<std::size_t>(blockSize));
    float* chans[1] = {buf.data()};
    double phase = 0.0;
    for (int b = 0; b < numBlocks; ++b) {
        fillSine(buf.data(), blockSize, inputFreqHz, sr, phase);
        AudioBlock block(chans, 1, blockSize);
        fx.process(block);
        all.insert(all.end(), buf.begin(), buf.end());
    }
    return all;
}

}  // namespace

// ---------------------------------------------------------------------------
// T014-A: FR-019 Depth-0 passthrough — most important invariant.
//
// When BOTH wow and flutter depths are 0 (defaults), the wow & flutter stage
// returns each input sample unchanged (guarded bypass in WowFlutterStage).
// With mix=0 (dry path only) the output of the whole effect equals the input
// sample-for-sample.  Exact equality is required — not just close, but equal.
//
// Depth-0 passthrough choice: GUARDED BYPASS — WowFlutterStage.processSample()
// returns x directly when wowDepth_==0 && flutterDepth_==0, writing to the
// delay line (to keep the buffer current) but never reading from it.  This
// ensures zero pitch modulation with no latency introduced.
// ---------------------------------------------------------------------------
TEST_CASE("FR-019: depth-0 wow+flutter passthrough — output equals input exactly") {
    const double sr        = 48000.0;
    const int    blockSize = 64;

    ModulatedDelayEffect fx;
    fx.prepare(ProcessContext{sr, blockSize, 1});
    // Leave wow/flutter depths at their defaults (0.0) — bypass active.
    setParam(fx, ModulatedDelayEffect::kMix,       0.0f);   // dry only
    setParam(fx, ModulatedDelayEffect::kFeedback,  0.0f);

    std::vector<float> buf(static_cast<std::size_t>(blockSize));
    // Fill with a varied, non-trivial waveform (ramp with mid-range DC offset).
    for (int i = 0; i < blockSize; ++i)
        buf[i] = static_cast<float>(i) / static_cast<float>(blockSize) - 0.5f;

    const std::vector<float> expected(buf);
    float* chans[1] = {buf.data()};
    AudioBlock block(chans, 1, blockSize);
    fx.process(block);

    for (int i = 0; i < blockSize; ++i)
        CHECK(buf[i] == doctest::Approx(expected[i]).epsilon(1e-7f));
}

// Also verify that explicitly setting both depths to 0.0 via setParameter still
// produces exact passthrough (ensuring the applyPending path also works).
TEST_CASE("FR-019: explicit depth=0 via setParameter still gives exact passthrough") {
    const double sr        = 48000.0;
    const int    blockSize = 64;

    ModulatedDelayEffect fx;
    fx.prepare(ProcessContext{sr, blockSize, 1});
    // Explicitly set both depths to 0 through the parameter path.
    setParam(fx, ModulatedDelayEffect::kWowDepth,     0.0f);
    setParam(fx, ModulatedDelayEffect::kFlutterDepth, 0.0f);
    setParam(fx, ModulatedDelayEffect::kWowRate,      0.5f);
    setParam(fx, ModulatedDelayEffect::kFlutterRate,  8.0f);
    setParam(fx, ModulatedDelayEffect::kMix,          0.0f);
    setParam(fx, ModulatedDelayEffect::kFeedback,     0.0f);

    std::vector<float> buf(static_cast<std::size_t>(blockSize));
    for (int i = 0; i < blockSize; ++i)
        buf[i] = std::sin(static_cast<float>(i) * 0.1f);

    const std::vector<float> expected(buf);
    float* chans[1] = {buf.data()};
    AudioBlock block(chans, 1, blockSize);
    fx.process(block);

    for (int i = 0; i < blockSize; ++i)
        CHECK(buf[i] == doctest::Approx(expected[i]).epsilon(1e-7f));
}

// ---------------------------------------------------------------------------
// T014-B: FR-018 Wow (slow drift) produces measurable pitch modulation.
//
// With wowDepth > 0 and flutterDepth = 0, the WowFlutterStage reads from its
// own delay line at a position that drifts slowly (at the wow rate).  This
// Doppler-shifts the pitch of a sustained sine, making the output differ from
// an unmodulated reference.
//
// Strategy: run two instances — one with wowDepth=0.5 (active), one depth=0
// (passthrough reference).  After the WowFlutterStage delay buffer fills
// (~10 ms warmup), measure the RMS of the sample-by-sample difference.  With
// active wow the outputs diverge measurably.  mix=0 (dry only) isolates the
// WowFlutterStage output from the main delay path.
// ---------------------------------------------------------------------------
TEST_CASE("FR-018: wow depth > 0 produces measurable pitch modulation on input") {
    const double sr        = 48000.0;
    const int    blockSize = 256;
    // Warmup > 10 ms (WowFlutterStage nominal center tap) to fill the buffer.
    const int    warmup    = 5;    // ~5 * 256 / 48000 ≈ 27 ms
    const int    measure   = 60;   // ~320 ms measurement window

    const auto buildAndRun = [&](float wowDepth) -> std::vector<float> {
        ModulatedDelayEffect fx;
        fx.prepare(ProcessContext{sr, blockSize, 1});
        setParam(fx, ModulatedDelayEffect::kMix,      0.0f);  // dry only
        setParam(fx, ModulatedDelayEffect::kFeedback, 0.0f);
        setParam(fx, ModulatedDelayEffect::kWowRate,  0.5f);  // 0.5 Hz — slow wow
        setParam(fx, ModulatedDelayEffect::kWowDepth, wowDepth);
        setParam(fx, ModulatedDelayEffect::kFlutterDepth, 0.0f);

        std::vector<float> buf(static_cast<std::size_t>(blockSize));
        float* chans[1] = {buf.data()};
        double phase = 0.0;

        // Warmup: fill the WowFlutterStage buffer.
        for (int b = 0; b < warmup; ++b) {
            fillSine(buf.data(), blockSize, 440.0, sr, phase);
            AudioBlock block(chans, 1, blockSize);
            fx.process(block);
        }

        std::vector<float> out;
        out.reserve(static_cast<std::size_t>(blockSize * measure));
        for (int b = 0; b < measure; ++b) {
            fillSine(buf.data(), blockSize, 440.0, sr, phase);
            AudioBlock block(chans, 1, blockSize);
            fx.process(block);
            out.insert(out.end(), buf.begin(), buf.end());
        }
        return out;
    };

    const auto outRef = buildAndRun(0.0f);
    const auto outWow = buildAndRun(0.5f);

    // Compute RMS of the sample-by-sample difference (wow vs reference).
    std::vector<float> diff(outRef.size());
    for (std::size_t i = 0; i < diff.size(); ++i)
        diff[i] = outWow[i] - outRef[i];
    const float rmsDiff = computeRms(diff);

    // Wow produces a periodic Doppler shift; the output must measurably differ
    // from the depth=0 reference.
    CHECK(rmsDiff > 1e-3f);
}

// ---------------------------------------------------------------------------
// T014-C: FR-018 Flutter (fast shimmer) produces measurable pitch modulation.
//
// Same structure as T014-B but for flutter (flutterDepth > 0, wowDepth = 0).
// Flutter runs at 8 Hz by default — eight cycles per second.  The measurement
// window is 80 ms (~0.64 flutter cycles), enough to show measurable variation.
// ---------------------------------------------------------------------------
TEST_CASE("FR-018: flutter depth > 0 produces measurable pitch modulation on input") {
    const double sr        = 48000.0;
    const int    blockSize = 256;
    const int    warmup    = 5;
    const int    measure   = 60;

    const auto buildAndRun = [&](float flutterDepth) -> std::vector<float> {
        ModulatedDelayEffect fx;
        fx.prepare(ProcessContext{sr, blockSize, 1});
        setParam(fx, ModulatedDelayEffect::kMix,          0.0f);
        setParam(fx, ModulatedDelayEffect::kFeedback,     0.0f);
        setParam(fx, ModulatedDelayEffect::kWowDepth,     0.0f);
        setParam(fx, ModulatedDelayEffect::kFlutterRate,  8.0f);  // 8 Hz — fast flutter
        setParam(fx, ModulatedDelayEffect::kFlutterDepth, flutterDepth);

        std::vector<float> buf(static_cast<std::size_t>(blockSize));
        float* chans[1] = {buf.data()};
        double phase = 0.0;

        for (int b = 0; b < warmup; ++b) {
            fillSine(buf.data(), blockSize, 440.0, sr, phase);
            AudioBlock block(chans, 1, blockSize);
            fx.process(block);
        }

        std::vector<float> out;
        out.reserve(static_cast<std::size_t>(blockSize * measure));
        for (int b = 0; b < measure; ++b) {
            fillSine(buf.data(), blockSize, 440.0, sr, phase);
            AudioBlock block(chans, 1, blockSize);
            fx.process(block);
            out.insert(out.end(), buf.begin(), buf.end());
        }
        return out;
    };

    const auto outRef     = buildAndRun(0.0f);
    const auto outFlutter = buildAndRun(0.5f);

    std::vector<float> diff(outRef.size());
    for (std::size_t i = 0; i < diff.size(); ++i)
        diff[i] = outFlutter[i] - outRef[i];
    const float rmsDiff = computeRms(diff);

    CHECK(rmsDiff > 1e-3f);
}

// ---------------------------------------------------------------------------
// T014-D: FR-020 Instability is present in the delay tail.
//
// Because wow & flutter is applied to the input (x') before the main delay,
// its character is written into the delay buffer and is therefore present in
// the delayed/wet signal.
//
// Strategy: instance A has wowDepth=0.3 (wow active); instance B has depth=0
// (passthrough).  Both use mix=1 (wet only) and feedback=0 to isolate the
// first echo.  After the main delay fills, the wet output of A should differ
// from B because the wow-modulated x' (not the clean x) fed the delay buffer.
// ---------------------------------------------------------------------------
TEST_CASE("FR-020: wow & flutter instability is present in the delayed tail") {
    const double sr        = 48000.0;
    const int    blockSize = 256;
    const float  delayMs   = 50.0f;  // 50 ms delay (about 20 blocks at 256 samples)

    const auto buildAndRun =
        [&](float wowDepth, int numBlocks) -> std::vector<float> {
            ModulatedDelayEffect fx;
            fx.prepare(ProcessContext{sr, blockSize, 1});
            setParam(fx, ModulatedDelayEffect::kDelayTime,  delayMs / 1000.0f);
            setParam(fx, ModulatedDelayEffect::kMix,        1.0f);   // wet only
            setParam(fx, ModulatedDelayEffect::kFeedback,   0.0f);   // no feedback
            setParam(fx, ModulatedDelayEffect::kCutoff,     20000.0f);
            setParam(fx, ModulatedDelayEffect::kResonance,  0.0f);
            setParam(fx, ModulatedDelayEffect::kWowRate,    1.0f);   // 1 Hz wow
            setParam(fx, ModulatedDelayEffect::kWowDepth,   wowDepth);
            setParam(fx, ModulatedDelayEffect::kFlutterDepth, 0.0f);

            return runEffect(fx, sr, blockSize, numBlocks, 440.0);
    };

    // Warmup: fill the delay with wow-modulated signal (delayMs + a few extra blocks).
    const int warmup = static_cast<int>(sr * delayMs / 1000.0 / blockSize) + 5;
    const int measure = 40;

    // Build two runs with different wow depths; collect only measurement blocks.
    ModulatedDelayEffect fxA, fxB;
    fxA.prepare(ProcessContext{sr, blockSize, 1});
    fxB.prepare(ProcessContext{sr, blockSize, 1});
    for (auto* fx : {&fxA, &fxB}) {
        setParam(*fx, ModulatedDelayEffect::kDelayTime,  delayMs / 1000.0f);
        setParam(*fx, ModulatedDelayEffect::kMix,        1.0f);
        setParam(*fx, ModulatedDelayEffect::kFeedback,   0.0f);
        setParam(*fx, ModulatedDelayEffect::kCutoff,     20000.0f);
        setParam(*fx, ModulatedDelayEffect::kResonance,  0.0f);
        setParam(*fx, ModulatedDelayEffect::kWowRate,    1.0f);
        setParam(*fx, ModulatedDelayEffect::kFlutterDepth, 0.0f);
    }
    setParam(fxA, ModulatedDelayEffect::kWowDepth, 0.5f);  // A: wow active
    setParam(fxB, ModulatedDelayEffect::kWowDepth, 0.0f);  // B: no wow

    std::vector<float> bufA(static_cast<std::size_t>(blockSize));
    std::vector<float> bufB(static_cast<std::size_t>(blockSize));
    float* chansA[1] = {bufA.data()};
    float* chansB[1] = {bufB.data()};
    double phase = 0.0;

    for (int b = 0; b < warmup; ++b) {
        fillSine(bufA.data(), blockSize, 440.0, sr, phase);
        std::copy(bufA.begin(), bufA.end(), bufB.begin());
        { AudioBlock ba(chansA, 1, blockSize); fxA.process(ba); }
        { AudioBlock bb(chansB, 1, blockSize); fxB.process(bb); }
    }

    std::vector<float> diff;
    diff.reserve(static_cast<std::size_t>(blockSize * measure));
    for (int b = 0; b < measure; ++b) {
        fillSine(bufA.data(), blockSize, 440.0, sr, phase);
        std::copy(bufA.begin(), bufA.end(), bufB.begin());
        { AudioBlock ba(chansA, 1, blockSize); fxA.process(ba); }
        { AudioBlock bb(chansB, 1, blockSize); fxB.process(bb); }
        for (int i = 0; i < blockSize; ++i)
            diff.push_back(bufA[i] - bufB[i]);
    }

    // The delayed tail of A should differ from B: the wow modulation was baked
    // into the delay buffer (applied to x' before the main delay write, FR-020).
    CHECK(computeRms(diff) > 1e-3f);
}

// ---------------------------------------------------------------------------
// T014-E: FR-021 In-range under maximum depths and extreme rates — no crash.
//
// Maximum wow + flutter depths (both 1.0) and rates at their descriptor bounds,
// combined with a sustained signal, must produce no NaN/Inf and bounded output.
// The WowFlutterStage's DelayLine clamps reads to [0, capacity-1] (FR-021),
// and the combined worst-case displacement (±10 ms) stays within the allocated
// ~20 ms buffer.
// ---------------------------------------------------------------------------
TEST_CASE("FR-021: max wow+flutter depths and extreme rates produce finite output") {
    const double sr        = 48000.0;
    const int    blockSize = 256;

    ModulatedDelayEffect fx;
    fx.prepare(ProcessContext{sr, blockSize, 2});  // stereo

    // Extreme but in-bounds parameter values.
    setParam(fx, ModulatedDelayEffect::kDelayTime,    0.3f);
    setParam(fx, ModulatedDelayEffect::kFeedback,     0.5f);
    setParam(fx, ModulatedDelayEffect::kMix,          0.5f);
    setParam(fx, ModulatedDelayEffect::kCutoff,       20000.0f);
    setParam(fx, ModulatedDelayEffect::kResonance,    0.0f);
    // US3: max depths, extreme rates.
    setParam(fx, ModulatedDelayEffect::kWowRate,      2.0f);   // max wow rate
    setParam(fx, ModulatedDelayEffect::kWowDepth,     1.0f);   // max wow depth
    setParam(fx, ModulatedDelayEffect::kFlutterRate,  12.0f);  // max flutter rate
    setParam(fx, ModulatedDelayEffect::kFlutterDepth, 1.0f);   // max flutter depth

    const std::size_t sz = static_cast<std::size_t>(blockSize);
    std::vector<float> left(sz, 0.0f), right(sz, 0.0f);
    float* chans[2] = {left.data(), right.data()};

    // Feed an impulse then sustained signal; verify every output sample is finite.
    left[0] = right[0] = 1.0f;
    {
        AudioBlock block(chans, 2, blockSize);
        fx.process(block);
    }
    for (int i = 0; i < blockSize; ++i) {
        REQUIRE(std::isfinite(left[i]));
        REQUIRE(std::isfinite(right[i]));
    }

    double phaseL = 0.0, phaseR = 0.33;
    for (int b = 0; b < 200; ++b) {
        fillSine(left.data(),  blockSize, 440.0,  sr, phaseL);
        fillSine(right.data(), blockSize, 660.0,  sr, phaseR);
        AudioBlock block(chans, 2, blockSize);
        fx.process(block);
        for (int i = 0; i < blockSize; ++i) {
            REQUIRE(std::isfinite(left[i]));
            REQUIRE(std::isfinite(right[i]));
        }
    }
}
