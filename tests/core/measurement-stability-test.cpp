// measurement-stability-test.cpp
// Doctest cases for US3: stability verdict, allocation invariant, and exec-time
// metric (T013).  FR-010, FR-011, FR-012, SC-004.
//
// NOTE on stability and the SVF: the DaisySP SVF (SvfPrimitive / SvfEffect)
// does not flush subnormal float values in its internal state, so it fails
// the "denormal" stability case when fed a subnormal-decaying input.  This is
// a genuine limitation of the underlying DaisySP implementation — but whether
// the SVF actually *produces* subnormals is environment-conditional: with FPU
// flush-to-zero / denormals-are-zero enabled (FTZ/DAZ, common on embedded
// ARM/release builds) the hardware flushes the decaying state and no subnormal
// appears (AUDIT-20260629-09).  So the real-SVF behavior is NOT hard-asserted
// here; it is recorded as a backlog item (TASK-1).  Instead the stability
// verdict is validated with deterministic, FPU-mode-independent stubs: CleanFx
// (passes), BrokenFx (NaN -> fails), and DenormalFx (a stored subnormal CONSTANT
// -> fails), the last portably guarding the harness's subnormal-DETECTION
// capability.  The remaining tests use the real SVF for the allocation and
// exec-time measurements (those do not depend on stability).

#include <cmath>
#include <limits>
#include <string>
#include <vector>

#include <doctest/doctest.h>

#include "support/measurement/metrics.h"
#include "support/allocation-sentinel.h"
#include "support/svf-reference.h"
#include "effects/svf/svf-effect.h"
#include "dsp/audio-block.h"
#include "dsp/process-context.h"

#include "measurement-support.h"

using namespace acfx::measure;
using namespace acfx::meastest;

using acfx::test::AllocationSentinel;
using acfx::test::kRefCutoffHz;
using acfx::test::kRefSampleRate;

namespace {

// CleanFx — a minimal effect stub whose process() flushes every subnormal
// sample to zero and otherwise passes samples through unchanged.  This
// represents a "correctly implemented" effect that satisfies all four
// stability cases: silence-in -> silence-out, DC bounded, no subnormal
// output under decaying input, and a quiet idle tail.
struct CleanFx {
    void prepare(const acfx::ProcessContext&) noexcept {}
    void reset() noexcept {}
    void process(acfx::AudioBlock& blk) noexcept {
        for (int ch = 0; ch < blk.numChannels(); ++ch) {
            float* samples = blk.channel(ch);
            for (int i = 0; i < blk.numSamples(); ++i) {
                const float x = samples[i];
                samples[i] = (std::fpclassify(x) == FP_SUBNORMAL) ? 0.0f : x;
            }
        }
    }
};

// BrokenFx — a minimal effect stub whose process() writes quiet_NaN into
// every sample.  stability() must detect the NaN and return ok == false.
struct BrokenFx {
    void prepare(const acfx::ProcessContext&) noexcept {}
    void reset() noexcept {}
    void process(acfx::AudioBlock& blk) noexcept {
        const float nan = std::numeric_limits<float>::quiet_NaN();
        for (int ch = 0; ch < blk.numChannels(); ++ch) {
            float* samples = blk.channel(ch);
            for (int i = 0; i < blk.numSamples(); ++i) {
                samples[i] = nan;
            }
        }
    }
};

// DenormalFx — a minimal effect stub whose process() writes a SUBNORMAL float
// (denorm_min) into every sample. This is a STORED CONSTANT, not the result of
// an arithmetic operation, so it is NOT affected by the FPU flush-to-zero /
// denormals-are-zero rounding mode (FTZ/DAZ) — it deterministically presents a
// subnormal to stability() on any host, unlike a real decaying filter whose
// subnormal generation is environment-conditional (AUDIT-20260629-09). It
// exercises the harness's subnormal-DETECTION capability portably.
struct DenormalFx {
    void prepare(const acfx::ProcessContext&) noexcept {}
    void reset() noexcept {}
    void process(acfx::AudioBlock& blk) noexcept {
        const float sub = std::numeric_limits<float>::denorm_min();
        for (int ch = 0; ch < blk.numChannels(); ++ch) {
            float* samples = blk.channel(ch);
            for (int i = 0; i < blk.numSamples(); ++i) {
                samples[i] = sub;
            }
        }
    }
};

} // namespace

TEST_CASE("stability: clean effect stub passes all cases (FR-012)") {
    // Uses CleanFx — a denormal-flushing passthrough — to demonstrate that
    // the stability verdict returns {true, nullptr} for a numerically clean
    // effect across all four cases: silence, dc, denormal, idle.
    CleanFx cleanFx;

    const acfx::ProcessContext ctx{kRefSampleRate, 512, 1};
    const Stability result = stability(cleanFx, ctx);

    const std::string failedCaseStr = result.failedCase
                                      ? std::string(result.failedCase)
                                      : std::string("(none)");
    INFO("failedCase = " << failedCaseStr);
    CHECK(result.ok == true);
    CHECK(result.failedCase == nullptr);
}

TEST_CASE("stability: broken effect fails verdict (FR-012, discriminating)") {
    // BrokenFx writes NaN into every output sample — the harness must detect
    // this and return ok == false with a non-null failedCase name.
    BrokenFx brokenFx;

    const acfx::ProcessContext ctx{kRefSampleRate, 512, 1};
    const Stability result = stability(brokenFx, ctx);

    const std::string failedCaseStr = result.failedCase
                                      ? std::string(result.failedCase)
                                      : std::string("(none)");
    INFO("failedCase = " << failedCaseStr);
    CHECK(result.ok == false);
    CHECK(result.failedCase != nullptr);
}

TEST_CASE("stability: detects subnormal output deterministically (FR-012, AUDIT-20260629-09)") {
    // Portable executable guard for the harness's subnormal-DETECTION capability.
    // DenormalFx deterministically emits a subnormal CONSTANT (not affected by
    // FTZ/DAZ rounding mode), so stability() must flag it on ANY host. This
    // replaces an earlier guard that hard-asserted the real SVF fails the
    // denormal case — that assertion was environment-conditional (with FTZ/DAZ
    // enabled, common on embedded ARM/release builds, the SVF flushes and the
    // assertion inverts; AUDIT-20260629-09). The real SVF's denormal behavior
    // remains recorded as a backlog item (TASK-1), not a hard test invariant.
    DenormalFx denormalFx;

    const acfx::ProcessContext ctx{kRefSampleRate, 512, 1};
    const Stability result = stability(denormalFx, ctx);

    INFO("DenormalFx stability failedCase = "
         << (result.failedCase ? result.failedCase : "(none)"));
    CHECK(result.ok == false);          // subnormal output must be caught
    CHECK(result.failedCase != nullptr);
}

TEST_CASE("SVF process() allocates zero heap (FR-011)") {
    constexpr int kBlockSize = 256;
    acfx::SvfEffect svf;
    configureLowpass(svf, kRefCutoffHz);

    const acfx::ProcessContext ctx{kRefSampleRate, kBlockSize, 1};
    svf.prepare(ctx);
    svf.reset();

    std::vector<float> buf(static_cast<std::size_t>(kBlockSize), 0.1f);
    float* chans[1] = { buf.data() };

    AllocationSentinel::reset();
    for (int i = 0; i < 8; ++i) {
        acfx::AudioBlock blk(chans, 1, kBlockSize);
        svf.process(blk);
    }
    const std::size_t allocs = AllocationSentinel::allocations();

    CHECK_MESSAGE(allocs == 0, "SVF process() allocated ", allocs, " time(s)");
}

TEST_CASE("relativeExecTime produces a finite figure with the correct block size (FR-010, SC-004)") {
    constexpr int kBlockSize = 256;
    constexpr int kRepeats   = 32;

    acfx::SvfEffect svf;
    configureLowpass(svf, kRefCutoffHz);

    const acfx::ProcessContext ctx{kRefSampleRate, kBlockSize, 1};
    const ExecCost cost = relativeExecTime(svf, ctx, kBlockSize, kRepeats);

    INFO("timePerBlock = " << cost.timePerBlock << " s, blockSize = " << cost.blockSize);
    CHECK(cost.blockSize == kBlockSize);
    CHECK(cost.timePerBlock >= 0.0);
    CHECK(std::isfinite(cost.timePerBlock));
}
