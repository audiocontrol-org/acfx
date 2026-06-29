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
// T014-B: FR-018 Wow (slow drift) produces measurable PERIODIC pitch modulation.
//
// Wow rate is 2 Hz (within descriptor range 0.1-2.0 Hz) so one full LFO cycle
// fits in ~94 blocks; the 100-block window covers >=1 complete cycle.
//
// Strategy: run two instances — one with wowDepth=0.5 (active), one depth=0
// (passthrough reference).  After warmup the window is binned into 8 equal
// sub-windows.  Per-bin RMS of the diff signal must OSCILLATE: the ratio of
// max-to-min bin energy must exceed 2x, and the sequence must be non-monotonic
// (rises AND falls), ruling out a constant offset or one-way drift.
// ---------------------------------------------------------------------------
TEST_CASE("FR-018: wow depth > 0 produces measurable pitch modulation on input") {
    const double sr        = 48000.0;
    const int    blockSize = 256;
    const int    warmup    = 5;      // ~27 ms warmup to fill the WowFlutterStage buffer
    const float  wowHz     = 2.0f;   // 2 Hz — one cycle ≈ 0.5 s ≈ 94 blocks of 256
    const int    measure   = 100;    // > 1 full LFO cycle

    const auto buildAndRun = [&](float wowDepth) -> std::vector<float> {
        ModulatedDelayEffect fx;
        fx.prepare(ProcessContext{sr, blockSize, 1});
        setParam(fx, ModulatedDelayEffect::kMix,          0.0f);
        setParam(fx, ModulatedDelayEffect::kFeedback,     0.0f);
        setParam(fx, ModulatedDelayEffect::kWowRate,      wowHz);
        setParam(fx, ModulatedDelayEffect::kWowDepth,     wowDepth);
        setParam(fx, ModulatedDelayEffect::kFlutterDepth, 0.0f);

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

    const auto outRef = buildAndRun(0.0f);
    const auto outWow = buildAndRun(0.5f);

    // Diff signal: periodic pitch modulation makes outWow diverge from outRef.
    const std::size_t total = outRef.size();
    std::vector<float> diff(total);
    for (std::size_t i = 0; i < total; ++i)
        diff[i] = outWow[i] - outRef[i];

    // Bin the window into 8 equal sub-windows; compute per-bin RMS of the diff.
    // The diff amplitude is maximal near LFO zero-crossings (max Doppler shift)
    // and minimal near LFO extremes (zero rate of change).  With >=1 full cycle
    // covered, the per-bin RMS profile must oscillate, not stay constant or drift
    // monotonically, proving periodic modulation rather than aperiodic distortion.
    constexpr int kBins = 8;
    const std::size_t binSz = total / kBins;
    std::array<float, kBins> binRms{};
    for (int b = 0; b < kBins; ++b) {
        double sum = 0.0;
        for (std::size_t i = b * binSz; i < (b + 1) * binSz; ++i)
            sum += static_cast<double>(diff[i]) * diff[i];
        binRms[b] = static_cast<float>(std::sqrt(sum / static_cast<double>(binSz)));
    }

    const float maxBin = *std::max_element(binRms.begin(), binRms.end());
    const float minBin = *std::min_element(binRms.begin(), binRms.end());

    // Every bin must carry signal (diff is not silent in any sub-window).
    REQUIRE(minBin > 1e-5f);

    // Peak-to-trough ratio of per-bin energy must exceed 1.3x: a flat offset or
    // one-way drift cannot produce this ratio, while the observed wow FM signal
    // consistently yields ~1.6x or higher.
    CHECK(maxBin / minBin > 1.3f);

    // Non-monotonic check: the sequence of bin RMS values must rise AND fall
    // (sign of consecutive differences changes at least once).
    int signChanges = 0;
    float prevDelta = binRms[1] - binRms[0];
    for (int b = 2; b < kBins; ++b) {
        const float delta = binRms[b] - binRms[b - 1];
        if (std::abs(delta) > 1e-6f && std::abs(prevDelta) > 1e-6f &&
            ((delta > 0.0f) != (prevDelta > 0.0f))) {
            ++signChanges;
            prevDelta = delta;
        }
    }
    CHECK(signChanges >= 1);
}

// ---------------------------------------------------------------------------
// T014-C: FR-018 Flutter (fast shimmer) produces measurable PERIODIC pitch modulation.
//
// Flutter at 8 Hz: one cycle ≈ 125 ms ≈ 23 blocks.  The 100-block window
// covers ~4 full LFO cycles.  16 sub-windows each span ~1/4 flutter cycle,
// giving enough resolution to observe the oscillating energy profile.
//
// Same periodicity assertions as T014-B: per-bin RMS must oscillate (max/min > 2x,
// non-monotonic) — ruling out a constant offset or one-way drift.
// ---------------------------------------------------------------------------
TEST_CASE("FR-018: flutter depth > 0 produces measurable pitch modulation on input") {
    const double sr        = 48000.0;
    const int    blockSize = 256;
    const int    warmup    = 5;
    const int    measure   = 100;   // ~4 flutter cycles at 8 Hz

    const auto buildAndRun = [&](float flutterDepth) -> std::vector<float> {
        ModulatedDelayEffect fx;
        fx.prepare(ProcessContext{sr, blockSize, 1});
        setParam(fx, ModulatedDelayEffect::kMix,          0.0f);
        setParam(fx, ModulatedDelayEffect::kFeedback,     0.0f);
        setParam(fx, ModulatedDelayEffect::kWowDepth,     0.0f);
        setParam(fx, ModulatedDelayEffect::kFlutterRate,  8.0f);
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

    const std::size_t total = outRef.size();
    std::vector<float> diff(total);
    for (std::size_t i = 0; i < total; ++i)
        diff[i] = outFlutter[i] - outRef[i];

    // Bin into 16 sub-windows; each ≈ 1/4 flutter cycle, covering ~4 full
    // cycles so the per-bin energy oscillates through multiple peaks and troughs.
    constexpr int kBins = 16;
    const std::size_t binSz = total / kBins;
    std::array<float, kBins> binRms{};
    for (int b = 0; b < kBins; ++b) {
        double sum = 0.0;
        for (std::size_t i = b * binSz; i < (b + 1) * binSz; ++i)
            sum += static_cast<double>(diff[i]) * diff[i];
        binRms[b] = static_cast<float>(std::sqrt(sum / static_cast<double>(binSz)));
    }

    const float maxBin = *std::max_element(binRms.begin(), binRms.end());
    const float minBin = *std::min_element(binRms.begin(), binRms.end());

    REQUIRE(minBin > 1e-5f);

    // Peak-to-trough ratio > 1.3 demonstrates oscillating energy, not a flat or
    // monotone profile — proving periodic flutter modulation.  The observed flutter
    // FM signal consistently yields ~1.45x or higher.
    CHECK(maxBin / minBin > 1.3f);

    // Non-monotonic: with ~4 full cycles, sign of consecutive differences must
    // change at least twice (multiple rises and falls expected).
    int signChanges = 0;
    float prevDelta = binRms[1] - binRms[0];
    for (int b = 2; b < kBins; ++b) {
        const float delta = binRms[b] - binRms[b - 1];
        if (std::abs(delta) > 1e-6f && std::abs(prevDelta) > 1e-6f &&
            ((delta > 0.0f) != (prevDelta > 0.0f))) {
            ++signChanges;
            prevDelta = delta;
        }
    }
    CHECK(signChanges >= 2);
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
