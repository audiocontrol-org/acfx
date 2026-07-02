#include <doctest/doctest.h>

#include <cmath>
#include <vector>

#include "dsp/audio-block.h"
#include "dsp/param-id.h"
#include "dsp/process-context.h"
#include "dsp/span.h"
#include "effects/svf/svf-effect.h"
#include "effects/saturation/saturation-core.h"
#include "primitives/analysis/capture-probe.h"
#include "primitives/dynamics/envelope-follower.h"
#include "primitives/oversampling/oversampler.h"
#include "processor-node/processor-node.h"
#include "support/allocation-sentinel.h"

// T018 — the no-heap-allocation-in-process() invariant (FR-014). Uses the
// thread-local allocation sentinel across several block sizes, on both the bare
// SvfEffect and the EffectNode<SvfEffect> host boundary. The count is captured
// out of the measured region before any assertion macro runs.

using namespace acfx;
using acfx::test::AllocationSentinel;

TEST_CASE("SvfEffect::process allocates nothing across block sizes") {
    for (int blockSize : {16, 64, 256, 512}) {
        SvfEffect fx;
        fx.prepare(ProcessContext{48000.0, blockSize, 2});

        std::vector<float> left(static_cast<std::size_t>(blockSize), 0.1f);
        std::vector<float> right(static_cast<std::size_t>(blockSize), 0.1f);
        float* chans[2] = {left.data(), right.data()};

        AllocationSentinel::reset();
        for (int i = 0; i < 100; ++i) {
            AudioBlock block(chans, 2, blockSize);
            fx.process(block);
            // parameter changes on the audio thread must also be allocation-free
            fx.setParameter(ParamId{SvfEffect::kCutoff}, (i % 2 == 0) ? 0.25f : 0.75f);
        }
        const std::size_t allocations = AllocationSentinel::allocations();

        CHECK_MESSAGE(allocations == 0, "block size ", blockSize, " allocated ", allocations);
    }
}

TEST_CASE("EffectNode<SvfEffect>::processBlock allocates nothing") {
    EffectNode<SvfEffect> node;
    const int blockSize = 256;
    node.prepare(ProcessContext{48000.0, blockSize, 2});

    std::vector<float> left(static_cast<std::size_t>(blockSize), 0.1f);
    std::vector<float> right(static_cast<std::size_t>(blockSize), 0.1f);
    float* chans[2] = {left.data(), right.data()};

    AllocationSentinel::reset();
    for (int i = 0; i < 100; ++i) {
        AudioBlock block(chans, 2, blockSize);
        node.processBlock(block);
    }
    const std::size_t allocations = AllocationSentinel::allocations();

    CHECK_MESSAGE(allocations == 0, "EffectNode processBlock allocated ", allocations);
}

// T010 — the no-heap-allocation-in-process() invariant for SaturationCore
// (FR-018/FR-020, spec.md SC-005). Mirrors the SvfEffect case above: prepare()
// and voicing/quality selection reconfigure filter coefficients (and, for
// ADAAWaveshaper::setShape, are a documented control-thread-only call that may
// throw) so they run OUTSIDE the sentinel scope. Only the true audio-path
// calls — process() and the lightweight scalar setters (setDrive/setBias/
// setTone/setMix/setOutput, all noexcept and allocation-free per
// saturation-core.h/waveshaper.h/adaa-waveshaper.h/svf-primitive.h) — run
// INSIDE the sentinel, across all four voicings and both quality modes.
TEST_CASE("SaturationCore::process allocates nothing across voicings and quality modes") {
    constexpr SaturationVoicing kVoicings[] = {
        SaturationVoicing::softClip, SaturationVoicing::tape,
        SaturationVoicing::console, SaturationVoicing::tubePreamp};
    constexpr SaturationQuality kQualities[] = {SaturationQuality::naive, SaturationQuality::adaa};

    for (SaturationVoicing voicing : kVoicings) {
        for (SaturationQuality quality : kQualities) {
            SaturationCore core;
            core.prepare(48000.0f);   // control thread: filter/table setup, may allocate
            core.setVoicing(voicing); // control thread: reconfigures shapers + emphasis SVFs
            core.setQuality(quality); // control thread: selects naive/adaa path

            AllocationSentinel::reset();
            for (int i = 0; i < 200; ++i) {
                const float x = (i % 2 == 0) ? 0.3f : -0.3f;
                (void)core.process(x);
                // audio-thread-callable scalar parameter setters (FR-018/020)
                core.setDrive(1.0f + 0.01f * static_cast<float>(i % 5));
                core.setBias(0.05f * static_cast<float>((i % 3) - 1));
                core.setTone(0.01f * static_cast<float>((i % 7) - 3));
                core.setMix(0.5f + 0.001f * static_cast<float>(i % 2));
                core.setOutput(1.0f - 0.001f * static_cast<float>(i % 2));
            }
            const std::size_t allocations = AllocationSentinel::allocations();

            CHECK_MESSAGE(allocations == 0, "voicing=", static_cast<int>(voicing),
                          " quality=", static_cast<int>(quality), " allocated ", allocations);
        }
    }
}

namespace {

// T022 — the no-heap-allocation-in-process() invariant for Oversampler<Factor>
// (FR-013, SC-005). construct+init() (which clears the half-band delay
// lines) runs OUTSIDE the sentinel scope; only Oversampler::process() itself
// is asserted allocation-free, driven with both an identity eval and a real
// nonlinearity (tanh) eval, matching the SvfEffect/SaturationCore pattern
// above. Factor is a template parameter (Oversampler<2/4/8> are distinct
// types), so this helper is instantiated once per supported factor below.
template <int Factor>
void checkOversamplerProcessAllocationFree() {
    Oversampler<Factor> os;
    os.init(48000.0f); // control thread: clears delay lines, outside the sentinel

    constexpr int kBlockSamples = 256;

    AllocationSentinel::reset();
    for (int i = 0; i < kBlockSamples; ++i) {
        const float x = std::sin(0.05f * static_cast<float>(i));
        (void)os.process(x, [](float s) noexcept { return s; });                // (a) identity eval
        (void)os.process(x, [](float s) noexcept { return std::tanh(s); });      // (b) real nonlinearity
    }
    const std::size_t allocations = AllocationSentinel::allocations();

    CHECK_MESSAGE(allocations == 0, "Factor=", Factor, " allocated ", allocations);
}

} // namespace

TEST_CASE("Oversampler<Factor>::process allocates nothing across supported factors") {
    checkOversamplerProcessAllocationFree<2>();
    checkOversamplerProcessAllocationFree<4>();
    checkOversamplerProcessAllocationFree<8>();
}

// T027 -- the no-heap-allocation invariant for CaptureProbeRing::push() (US5,
// FR-011, spec.md SC-004: "the audio callback ... performs only a bounded
// lock-free ring push ... verified to allocate nothing"). Capacity (8192)
// matches the feature's default FFT-window-plus-margin sizing intent
// (FR-022/FR-027). The source block is a pre-sized std::vector allocated
// OUTSIDE the sentinel scope (mirrors the SvfEffect/Oversampler pattern
// above) so only push() itself runs inside the measured region. Each block
// size drives enough pushes to wrap the ring several times over AND force
// overrun (nothing drains this ring), which the audio-path overrun handling
// (capture-probe.h: "producer NEVER blocks or allocates" on lap) must also
// clear at zero allocations.
TEST_CASE("CaptureProbeRing::push allocates nothing across block sizes, including overrun") {
    constexpr std::size_t kCapacity = 8192;

    for (int blockSize : {16, 64, 256, 512}) {
        acfx::CaptureProbeRing<kCapacity> ring;
        std::vector<float> block(static_cast<std::size_t>(blockSize), 0.1f);

        // Enough pushes to sweep past Capacity several times over (forcing
        // wraparound) and keep going well past that (forcing overrun, since
        // nothing here ever drains).
        const int pushes =
            static_cast<int>(kCapacity / static_cast<std::size_t>(blockSize)) * 3 + 5;

        AllocationSentinel::reset();
        for (int i = 0; i < pushes; ++i) {
            ring.push(acfx::span<const float>(block.data(), block.size()));
        }
        const std::size_t allocations = AllocationSentinel::allocations();

        CHECK_MESSAGE(allocations == 0, "block size ", blockSize, " push() allocated ",
                      allocations);
        CHECK_MESSAGE(ring.overrunCount() > 0, "block size ", blockSize,
                      " expected push() to force overrun but overrunCount() == 0");
    }
}

// drain() runs off the audio thread (analysis/UI thread per FR-014), but it is
// still designed to be allocation-free (capture-probe.h: "Never blocks, never
// allocates, never fabricates samples"). Confirm that design holds with a
// pre-sized output buffer allocated outside the sentinel scope, interleaved
// with pushes so drain() sees a realistic mix of full/partial availability.
TEST_CASE("CaptureProbeRing::drain allocates nothing into a pre-sized buffer") {
    constexpr std::size_t kCapacity = 8192;
    acfx::CaptureProbeRing<kCapacity> ring;

    std::vector<float> block(256, 0.2f);
    std::vector<float> out(256, 0.0f);

    AllocationSentinel::reset();
    for (int i = 0; i < 200; ++i) {
        ring.push(acfx::span<const float>(block.data(), block.size()));
        (void)ring.drain(acfx::span<float>(out.data(), out.size()));
    }
    const std::size_t allocations = AllocationSentinel::allocations();

    CHECK_MESSAGE(allocations == 0, "CaptureProbeRing::drain allocated ", allocations);
}

// T014 — the no-heap-allocation-in-process() invariant for EnvelopeFollower
// (SC-007, FR-016). Mirrors the SvfEffect/SaturationCore pattern: init(),
// setMode(), setBallistics(), setDomain(), setAttack(), and setRelease() run
// OUTSIDE the sentinel scope (control-thread configuration, may allocate).
// Only process() itself is asserted allocation-free, driven with a US1 config
// (peak mode, branching ballistics, linear domain) and realistic attack/release
// times over a few hundred samples of varying input levels.
TEST_CASE("EnvelopeFollower::process allocates nothing (US1 config)") {
    EnvelopeFollower env;
    env.init(48000.0f);                           // control thread: caches fs, clears state
    env.setMode(DetectMode::peak);                // peak detection (US1 default)
    env.setBallistics(Ballistics::branching);     // branching ballistics (US1 default)
    env.setDomain(DetectDomain::linear);          // linear domain (US1 default)
    env.setAttack(0.010f);                        // 10 ms attack time
    env.setRelease(0.100f);                       // 100 ms release time

    AllocationSentinel::reset();
    for (int i = 0; i < 256; ++i) {
        // Drive with varying levels (sine-like sweep): low, high, low, …
        const float x = (i % 2 == 0) ? 0.3f : 0.8f;
        (void)env.process(x);
    }
    const std::size_t allocations = AllocationSentinel::allocations();

    CHECK_MESSAGE(allocations == 0, "EnvelopeFollower::process allocated ", allocations);
}

// T017 — the no-heap-allocation-in-process() invariant for EnvelopeFollower in
// RMS mode (SC-007, FR-016). Mirrors the peak mode case above (T014): init(),
// setMode(), setBallistics(), setDomain(), setRmsWindow(), setAttack(), and
// setRelease() run OUTSIDE the sentinel scope (control-thread configuration,
// may allocate). Only process() itself is asserted allocation-free, driven with
// an RMS config (rms detection mode, branching ballistics, linear domain, 50 ms
// RMS window) and realistic attack/release times over a few hundred samples of
// a sine sweep.
TEST_CASE("EnvelopeFollower::process allocates nothing (RMS config)") {
    EnvelopeFollower env;
    env.init(48000.0f);                           // control thread: caches fs, clears state
    env.setMode(DetectMode::rms);                 // RMS detection
    env.setBallistics(Ballistics::branching);     // branching ballistics
    env.setDomain(DetectDomain::linear);          // linear domain
    env.setRmsWindow(0.050f);                     // 50 ms RMS window
    env.setAttack(0.010f);                        // 10 ms attack time
    env.setRelease(0.100f);                       // 100 ms release time

    AllocationSentinel::reset();
    for (int i = 0; i < 256; ++i) {
        // Drive with sine sweep: std::sin to get a smooth variation
        const float x = std::sin(0.05f * static_cast<float>(i));
        (void)env.process(x);
    }
    const std::size_t allocations = AllocationSentinel::allocations();

    CHECK_MESSAGE(allocations == 0, "EnvelopeFollower::process allocated ", allocations);
}
