// analysis-live-offline-parity-test.cpp
// T028 -- harmonic-analysis feature, User Story 5: RED test for the
// ONE-ENGINE guarantee (spec.md US5 scenario 4, FR-014/FR-015, SC-005;
// data-model.md "LiveReadout"; research.md Decision 9).
//
// The live readout and the offline engine MUST be the SAME engine reached two
// different ways:
//   - OFFLINE: the host/analysis functions (harmonicSpectrum, thdPlusN) called
//     directly on a captured buffer, exactly as every other suite in this
//     tree does.
//   - LIVE: the identical stimulus pushed through core::CaptureProbeRing in
//     audio-callback-sized blocks (simulating the RT producer), then drained
//     and analyzed by the NOT-YET-EXISTING shared `acfx::analysis::LiveReadout`
//     (host-only, host/analysis/live-readout.h) -- the ONE implementation both
//     the workbench and plugin adapters will call (US5 scenario 2).
//
// Because both paths call the SAME harmonicSpectrum/thdPlusN functions on
// bit-identical sample data (the ring performs only a bounded float copy; no
// resampling, no reordering, no windowing of its own), agreement is expected
// to be extremely tight -- named here as kParityEpsilon, mirroring
// analysis-goertzel-parity-test.cpp's kParityEpsilon rationale for the same
// "same computation, reached two ways" shape.
//
// host/analysis/live-readout.h does not exist yet at RED time -- this test is
// expected to FAIL TO BUILD until T029 lands it. Do NOT implement
// live-readout.h to make this test pass; that is a separate task.

#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <stdexcept>
#include <vector>

#include "analysis/live-readout.h"  // UNDER TEST (does not exist at RED time)
#include "analysis/spectrum.h"      // acfx::analysis::harmonicSpectrum -- OFFLINE reference
#include "analysis/thdn.h"          // acfx::analysis::thdPlusN -- OFFLINE reference
#include "dsp/span.h"
#include "primitives/analysis/capture-probe.h"  // acfx::CaptureProbeRing -- the RT probe

using acfx::CaptureProbeRing;
using acfx::analysis::HarmonicSpectrum;
using acfx::analysis::harmonicSpectrum;
using acfx::analysis::LiveReadout;
using acfx::analysis::LiveReadoutConfig;
using acfx::analysis::ThdnResult;
using acfx::analysis::thdPlusN;

namespace {

// Analysis-window size for this test. Smaller than the FR-027 default
// (8192-pt) so the suite runs fast; LiveReadoutConfig::windowSize is a
// caller-supplied parameter precisely so callers (including this test) are
// not forced to use the production default. The ONE-ENGINE guarantee under
// test does not depend on window size.
constexpr std::size_t kWindowSize = 2048;
constexpr double      kSampleRate = 48000.0;

// Ring capacity: >= one window + margin (data-model.md "CaptureProbeRing"),
// exactly the FR-027 sizing rule, so a full window drains without overrun.
constexpr std::size_t kRingCapacity = kWindowSize + 512;

// Integer-cycle fundamental: 256 cycles across the 2048-sample window lands
// exactly on its own Goertzel/FFT bin (256 * 48000 / 2048 = 6000.0 Hz exactly),
// mirroring every other suite in this tree (analysis-spectrum-test.cpp,
// analysis-thdn-test.cpp). Harmonics 2x/3x/4x (12000/18000/24000 Hz) must stay
// below Nyquist (24000 Hz) for a well-defined comparison; 24000 Hz sits AT
// Nyquist, so this test uses only harmonics 1-3 (up to 18000 Hz) plus a 4th
// harmonic that is deliberately absent (0 amplitude) so its magnitude reads
// ~0 on BOTH paths (still a meaningful parity point, not a fabricated one).
constexpr double kFundamentalHz = 6000.0;
constexpr int    kNumHarmonics  = 4;

// Parity epsilon: live and offline results are two call sites over the SAME
// deterministic functions on bit-identical sample data (the ring performs
// only a bounded float copy, no re-derivation) -- agreement should differ
// only by double-rounding noise at the call boundary, mirroring
// analysis-goertzel-parity-test.cpp's kParityEpsilon for the same "same
// computation, reached two ways" shape.
constexpr double kParityEpsilon = 1.0e-9;

// Accumulate amplitude*sin(2*pi*freqHz*n/sampleRate) into `out` (matches
// SineGenerator's phase convention so harmonics superpose correctly). Local,
// self-contained helper -- mirrors analysis-thdn-test.cpp's addSine, per
// research.md Decision 1's one-way dependency (tests/support -> host/analysis,
// never the reverse; this test file reaches into neither, it is self-contained).
void addSine(std::vector<float>& out, double freqHz, double sampleRate, double amplitude) {
    constexpr double kTwoPi = 6.283185307179586476925286766559;
    const double omega = kTwoPi * freqHz / sampleRate;
    for (std::size_t n = 0; n < out.size(); ++n) {
        out[n] += static_cast<float>(amplitude * std::sin(omega * static_cast<double>(n)));
    }
}

// Builds the shared stimulus: fundamental + 2nd + 3rd harmonic (4th absent).
std::vector<float> makeStimulus() {
    std::vector<float> in(kWindowSize, 0.0f);
    addSine(in, 1.0 * kFundamentalHz, kSampleRate, 1.00);  // fundamental
    addSine(in, 2.0 * kFundamentalHz, kSampleRate, 0.20);  // 2nd harmonic
    addSine(in, 3.0 * kFundamentalHz, kSampleRate, 0.10);  // 3rd harmonic
    // 4th harmonic deliberately absent (0 amplitude).
    return in;
}

// Pushes `in` through `ring` in audio-callback-sized blocks, simulating the
// RT producer thread pushing successive process() buffers rather than one
// giant write.
template <std::size_t Capacity>
void pushInBlocks(CaptureProbeRing<Capacity>& ring, const std::vector<float>& in,
                   std::size_t blockSize) {
    std::size_t offset = 0;
    while (offset < in.size()) {
        const std::size_t n = std::min(blockSize, in.size() - offset);
        ring.push(acfx::span<const float>(in.data() + offset, n));
        offset += n;
    }
}

} // namespace

TEST_CASE("LiveReadout: fundamentalHz <= 0 throws (fail-loud, code-review D6)") {
    // LiveReadoutConfig::fundamentalHz defaults to 0.0 -- a "not yet
    // configured" marker, never a usable reference frequency. Silently
    // constructing on it would analyze the DC bin as the "fundamental,"
    // reporting a meaningless-but-plausible spectrum/THD instead of failing
    // loud.
    CaptureProbeRing<kRingCapacity> ring;
    LiveReadoutConfig zeroFundamentalConfig{/*fundamentalHz=*/0.0, kSampleRate,
                                             kNumHarmonics, kWindowSize};
    CHECK_THROWS_AS((LiveReadout<kRingCapacity>(ring, zeroFundamentalConfig)),
                     std::invalid_argument);

    LiveReadoutConfig negativeFundamentalConfig{/*fundamentalHz=*/-100.0, kSampleRate,
                                                 kNumHarmonics, kWindowSize};
    CHECK_THROWS_AS((LiveReadout<kRingCapacity>(ring, negativeFundamentalConfig)),
                     std::invalid_argument);
}

TEST_CASE("LiveReadout: underrun -- fewer than one window available -> no result (FR-013)") {
    CaptureProbeRing<kRingCapacity> ring;
    LiveReadout<kRingCapacity> readout(ring, LiveReadoutConfig{kFundamentalHz, kSampleRate,
                                                                kNumHarmonics, kWindowSize});

    // Push less than one full analysis window.
    std::vector<float> partial(kWindowSize / 4, 0.5f);
    ring.push(acfx::span<const float>(partial));

    CHECK_FALSE(readout.update());
    CHECK_FALSE(readout.hasResult());
}

TEST_CASE("LiveReadout: update() drains AT MOST one analysis window per call") {
    // A generous capacity (>= 2 windows + margin) so pushing two full
    // windows' worth produces NO overrun -- isolating exactly one variable:
    // does a single update() call consume more than one window's samples?
    constexpr std::size_t kTwoWindowCapacity = 2 * kWindowSize + 512;
    CaptureProbeRing<kTwoWindowCapacity> ring;
    LiveReadout<kTwoWindowCapacity> readout(ring, LiveReadoutConfig{
                                                       kFundamentalHz, kSampleRate,
                                                       kNumHarmonics, kWindowSize});

    const std::vector<float> stimulus = makeStimulus(); // exactly kWindowSize samples
    pushInBlocks(ring, stimulus, 256);
    pushInBlocks(ring, stimulus, 256); // a 2nd window's worth queued behind it

    REQUIRE(ring.overrunCount() == 0);
    REQUIRE(ring.available() == 2 * kWindowSize);

    REQUIRE(readout.update());
    REQUIRE(readout.hasResult());
    // Exactly one window's worth was consumed by this update() call -- the
    // 2nd window's worth must still be sitting in the ring, untouched.
    CHECK(ring.available() == kWindowSize);
}

TEST_CASE("LiveReadout: live-drained analysis equals direct offline analysis "
          "(ONE-ENGINE guarantee, FR-015/SC-005)") {
    const std::vector<float> stimulus = makeStimulus();

    // --- OFFLINE: the engine called directly on the captured buffer. -------
    const HarmonicSpectrum offlineSpectrum =
        harmonicSpectrum(acfx::span<const float>(stimulus), kFundamentalHz, kSampleRate,
                          kNumHarmonics);
    const ThdnResult offlineThdn =
        thdPlusN(acfx::span<const float>(stimulus), kFundamentalHz, kSampleRate);

    // Sanity: the offline reference itself must have found a real fundamental
    // (otherwise this whole test would vacuously "pass" on two NaNs).
    REQUIRE(!std::isnan(offlineSpectrum.at(1).magnitude));
    REQUIRE(!std::isnan(offlineThdn.thdPlusN));

    // --- LIVE: the identical stimulus pushed through the RT probe in ------
    // --- audio-callback-sized blocks, then drained + analyzed by the ------
    // --- shared LiveReadout (US5 scenario 1: "the audio thread only ------
    // --- performs a bounded, lock-free copy ... a separate analysis ------
    // --- thread drains the ring and runs the SAME engine"). ---------------
    CaptureProbeRing<kRingCapacity> ring;
    pushInBlocks(ring, stimulus, /*blockSize=*/256); // a realistic process() block size

    LiveReadout<kRingCapacity> readout(ring, LiveReadoutConfig{kFundamentalHz, kSampleRate,
                                                                kNumHarmonics, kWindowSize});
    REQUIRE(ring.overrunCount() == 0); // capacity >= window + margin: no drops
    REQUIRE(readout.update());
    REQUIRE(readout.hasResult());

    const HarmonicSpectrum& liveSpectrum = readout.spectrum();
    const ThdnResult& liveThdn = readout.thdn();

    // ONE-ENGINE guarantee (FR-015/SC-005): live must equal offline within
    // the named parity tolerance for every harmonic (including the
    // deliberately-absent 4th, which must read ~0 on BOTH paths identically).
    for (int k = 1; k <= kNumHarmonics; ++k) {
        const HarmonicSpectrum::Bin off = offlineSpectrum.at(k);
        const HarmonicSpectrum::Bin live = liveSpectrum.at(k);

        CAPTURE(k);
        REQUIRE(std::isnan(off.magnitude) == std::isnan(live.magnitude));
        if (!std::isnan(off.magnitude)) {
            CHECK(live.magnitude == doctest::Approx(off.magnitude).epsilon(kParityEpsilon));
        }

        REQUIRE(std::isnan(off.phaseRad) == std::isnan(live.phaseRad));
        if (!std::isnan(off.phaseRad)) {
            CHECK(live.phaseRad ==
                  doctest::Approx(off.phaseRad).epsilon(kParityEpsilon).scale(1.0));
        }
    }

    REQUIRE(!std::isnan(liveThdn.thdPlusN));
    CHECK(liveThdn.thdPlusN == doctest::Approx(offlineThdn.thdPlusN).epsilon(kParityEpsilon));
    CHECK(liveThdn.noiseFloor == doctest::Approx(offlineThdn.noiseFloor).epsilon(kParityEpsilon));
    CHECK(liveThdn.snr == doctest::Approx(offlineThdn.snr).epsilon(kParityEpsilon));
}
