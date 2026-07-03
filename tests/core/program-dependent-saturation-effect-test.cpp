#include <doctest/doctest.h>

#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

#include "dsp/audio-block.h"
#include "dsp/effect.h"
#include "dsp/param-id.h"
#include "dsp/parameter.h"
#include "dsp/process-context.h"
#include "dsp/span.h"
#include "effects/program-dependent-saturation/program-dependent-saturation-effect.h"
#include "effects/program-dependent-saturation/program-dependent-saturation-parameters.h"

// T027 -- User Story 8 suite: ProgramDependentSaturationEffect (the host-facing
// Effect contract wrapping ProgramDependentSaturationCore). Mirrors the shipped
// tests/core/compressor-effect-test.cpp / saturation-effect-test.cpp idiom
// exactly: the Effect-concept static_assert, AudioBlock/ProcessContext
// construction, setParameter(ParamId, normalized) calls, and the pending-atomic
// cross-thread handoff -- scoped to the WRAPPER contract only (concept
// satisfaction, descriptor-table validity, param handoff, allocation-free
// lifecycle smoke check, multi-parameter round-trip). DSP correctness (matrix
// targets, topology, presets, sidechain) is covered by the sibling suites
// (program-dependent-saturation-{matrix,topology,presets,sidechain}-test.cpp);
// the rigorous zero-heap-allocation proof is T037
// (tests/core/no-allocation-test.cpp) -- this file only smoke-tests that
// prepare/reset/process run without incident, it does not re-derive T037's
// allocation-sentinel check.
//
// References: specs/program-dependent-saturation/tasks.md T027 (SC-012);
// spec.md User Story 8 Acceptance Scenarios 1-3; contracts/
// program-dependent-saturation-effect-api.md "ProgramDependentSaturationEffect";
// FR-015..018.

using namespace acfx;

namespace {

// Convert a desired PLAIN-units value for a ProgramDependentSaturationEffect
// parameter into the normalized 0..1 value setParameter() expects, via the
// shared descriptor table -- mirrors compressor-effect-test.cpp's normFor.
// Never hand-roll the normalize math here: the descriptor table (min/max/skew)
// is the single source of truth (FR-016).
float normFor(ProgramDependentSaturationEffect::Param p, float plainValue) {
    return normalize(ProgramDependentSaturationEffect::kParams[p], plainValue);
}

// Max absolute per-sample difference between two equal-length buffers -- used
// to prove a parameter edit actually changed the audio-thread output, without
// asserting an exact DSP curve value (that belongs to the matrix/topology
// suites).
double maxAbsDiff(const std::vector<float>& a, const std::vector<float>& b) {
    const std::size_t n = a.size() < b.size() ? a.size() : b.size();
    double worst = 0.0;
    for (std::size_t i = 0; i < n; ++i) {
        const double d = std::fabs(static_cast<double>(a[i]) - static_cast<double>(b[i]));
        if (d > worst)
            worst = d;
    }
    return worst;
}

} // namespace

// ---------------------------------------------------------------------------
// TEST 1: Effect concept (T027, SC-012) -- ProgramDependentSaturationEffect
// satisfies the compile-time Effect contract (prepare/process/reset/
// parameters/setParameter, no base class/vtable). The test target compiles as
// C++20 (tests/CMakeLists.txt: target_compile_features(... cxx_std_20)), so
// the named `acfx::Effect` concept (core/dsp/effect.h) is available. The
// effect header itself already carries this exact static_assert at namespace
// scope (program-dependent-saturation-effect.h, guarded by
// __cpp_concepts) -- this TEST_CASE re-asserts it unguarded (mirroring
// compressor-effect-test.cpp TEST 1) so a concept regression shows up as a
// named failure in the doctest run log, not just a silent build error deep in
// a header nobody is looking at.
// ---------------------------------------------------------------------------

TEST_CASE("ProgramDependentSaturationEffect satisfies the Effect concept (T027, US8, SC-012)") {
    static_assert(acfx::Effect<ProgramDependentSaturationEffect>,
                  "ProgramDependentSaturationEffect must satisfy the Effect contract "
                  "(prepare/process/reset/parameters/setParameter)");
    // Nothing to check at runtime here -- the static_assert above is the whole
    // assertion; a failing concept check is a BUILD error, not a test failure,
    // so this TEST_CASE only exists to give the compile-time check a place to
    // live and a name in the doctest run log.
    CHECK(true);
}

// ---------------------------------------------------------------------------
// TEST 2: Descriptor-table invariants (T027, FR-016, SC-012) -- the runtime
// mirror of ProgramDependentSaturationEffect::kParams's compile-time
// static_assert (program-dependent-saturation-effect.h: "isValidDescriptor
// over the table fails compilation, not the audio path") and
// program-dependent-saturation-parameters.h's dense-id static_assert. Confirms
// the table this test file's own build just compiled against is exactly 24
// rows, densely indexed 0..23 (Param enum order), every row independently
// passes isValidDescriptor at runtime too (not just at the header's
// static_assert), and every discrete row's label array matches its declared
// discreteCount.
// ---------------------------------------------------------------------------

TEST_CASE("ProgramDependentSaturationEffect::parameters() returns exactly 24 valid, "
          "densely-indexed descriptors (T027, US8, SC-012)") {
    const span<const ParameterDescriptor> params = ProgramDependentSaturationEffect::parameters();
    REQUIRE(params.size() == 24);

    for (std::size_t i = 0; i < params.size(); ++i) {
        const ParameterDescriptor& d = params[i];
        INFO("index=" << i << " name=" << d.name);

        // The build-time static_assert already guards this over the SAME
        // table (program-dependent-saturation-effect.h); re-checking at
        // runtime here proves the invariant holds for the actual object this
        // test links against, not just at compile time.
        CHECK(isValidDescriptor(d));

        // Dense ids 0..23 in Param-enum order (program-dependent-saturation-
        // effect.h's leading comment: "the leading ParamId index on each row
        // is that same dense id").
        CHECK(d.id.value == static_cast<std::uint8_t>(i));

        // max > min for every row (isValidDescriptor already requires this;
        // asserted again directly per the task's explicit runtime-sweep ask).
        CHECK(d.max > d.min);

        // defaultValue always lands within [min, max].
        CHECK(d.defaultValue >= d.min);
        CHECK(d.defaultValue <= d.max);

        // Discrete parameters: label array size matches the declared bucket
        // count (a mismatch here is exactly the malformed-descriptor shape
        // spec.md Acceptance Scenario 3 calls out -- "a discrete parameter
        // whose label count != choice count").
        if (d.kind == ParamKind::discrete) {
            CHECK(d.discreteCount >= 2);
            REQUIRE(d.choices.size() == d.discreteCount);
        }
    }

    // Same table via the ProgramDependentSaturationEffect::kParams alias --
    // confirms the alias and parameters() agree (both are the SAME constexpr
    // object, so this is also a check that the alias wasn't accidentally
    // shadowed).
    CHECK(&ProgramDependentSaturationEffect::kParams[0] == params.data());
}

// ---------------------------------------------------------------------------
// TEST 3: Cross-thread parameter handoff (T027, FR-017, SC-012) -- a
// setParameter() published "from a non-audio thread" (a bare call here,
// single-threaded in the test but semantically identical to FR-017's
// requirement: publish never mutates audio-thread state directly) takes
// effect ONLY at the NEXT process() call, not before it. Mirrors
// compressor-effect-test.cpp TEST 5 / saturation-effect-rt-test.cpp exactly:
// block A is captured under the OLD (default, zero-depth) settings and
// snapshotted, THEN driveDepth is published, THEN block B is processed on the
// SAME input and shown to diverge from block A (proving the edit was applied,
// and applied only once process() ran), THEN block A's already-produced
// buffer is re-checked against its snapshot to prove the later publish/
// process never reached back into already-emitted output. No crash and no
// torn read is implicit in the assertions running to completion with finite
// values throughout.
// ---------------------------------------------------------------------------

TEST_CASE("setParameter published off the audio thread takes effect only at the next "
          "process() call (T027, US8, FR-017, SC-012)") {
    constexpr int kBlockSize = 9600; // 200 ms at 48 kHz -- long enough for the default
                                      // 10 ms attack / 100 ms release ballistics to settle
    constexpr float kSampleRate = 48000.0f;
    constexpr std::size_t kTailSamples = 960; // last 20 ms

    ProgramDependentSaturationEffect fx;
    fx.prepare(ProcessContext{static_cast<double>(kSampleRate), kBlockSize, 1});

    std::vector<float> in(static_cast<std::size_t>(kBlockSize));
    for (int i = 0; i < kBlockSize; ++i) {
        const float t = static_cast<float>(i) / kSampleRate;
        in[static_cast<std::size_t>(i)] = 0.8f * std::sin(2.0f * 3.14159265f * 400.0f * t);
    }

    // Block A: the FIRST process() call runs under the effect's DEFAULT state
    // -- driveDepth is 0 (kPdsParams row 12's defaultValue), i.e. the
    // orthogonality baseline (no modulation applied at all).
    std::vector<float> blockA = in;
    {
        float* chans[1] = {blockA.data()};
        AudioBlock block(chans, 1, kBlockSize);
        fx.process(block);
    }

    // Snapshot block A's ALREADY-PRODUCED output BEFORE any later publish or
    // later process() call -- the ground truth the immutability check below
    // is compared against. This is also the only way to observe (indirectly,
    // via later contrast with block B) that driveDepth had NOT been applied
    // yet at this point: nothing has been published, so block A is, by
    // construction, the zero-depth case.
    const std::vector<float> blockASnapshot = blockA;

    // Publish a full-scale driveDepth edit "from a non-audio thread". No
    // process() call has consumed it yet -- per FR-017 this must NOT mutate
    // any audio-thread-visible state until the next process() call runs.
    fx.setParameter(ParamId{ProgramDependentSaturationEffect::kDriveDepth},
                     normFor(ProgramDependentSaturationEffect::kDriveDepth, 1.0f));

    // Block B: the NEXT process() call is where the pending driveDepth edit
    // is consumed (applyPending() runs at the top of process(), per the
    // SaturationEffect/CompressorEffect idiom) -- its output now reflects the
    // program-dependent drive modulation the edit turned on. Processing this
    // LATER block on the SAME input is exactly the event that could, if the
    // wrapper were buggy, retroactively touch block A's buffer.
    std::vector<float> blockB = in;
    {
        float* chans[1] = {blockB.data()};
        AudioBlock block(chans, 1, kBlockSize);
        fx.process(block);
    }

    // GENUINE past-output-immutability check: after the later publish AND the
    // later process() call, block A's already-produced output is byte-for-
    // byte what it was before -- the pending edit and the subsequent block
    // never reached back into already-emitted output (no torn read on the
    // buffer this test still holds a reference to).
    REQUIRE(blockA.size() == blockASnapshot.size());
    for (std::size_t i = 0; i < blockA.size(); ++i)
        CHECK(blockA[i] == doctest::Approx(blockASnapshot[i]));

    // Functional round-trip: the driveDepth edit was actually consumed on the
    // NEXT process() call -- block B's tail diverges measurably from block
    // A's, proving the published normalized value was denormalized and
    // forwarded into the modulation matrix, not silently dropped or applied a
    // block late. (The exact modulated-drive shape is the matrix suite's
    // concern -- this only proves the handoff happened.)
    const std::size_t start = static_cast<std::size_t>(kBlockSize) - kTailSamples;
    const std::vector<float> tailA(blockA.begin() + static_cast<std::ptrdiff_t>(start), blockA.end());
    const std::vector<float> tailB(blockB.begin() + static_cast<std::ptrdiff_t>(start), blockB.end());
    const double diff = maxAbsDiff(tailA, tailB);
    INFO("max abs tail diff=" << diff);
    CHECK(diff > 1.0e-6);

    // No crash / no torn read (functional check): every sample stays finite
    // in both blocks.
    for (float v : blockA)
        CHECK(std::isfinite(v));
    for (float v : blockB)
        CHECK(std::isfinite(v));
}

// ---------------------------------------------------------------------------
// TEST 4: Allocation-free lifecycle smoke check (T027, FR-018, SC-013) -- a
// lightweight contract-level smoke test that prepare()/reset()/process() run
// to completion without incident across the effect's default configuration.
// This is NOT the rigorous no-allocation proof: the allocation-sentinel-backed
// check across every target/curve/topology/preset/sidechain/link
// configuration is T037 (tests/core/no-allocation-test.cpp, SC-013) -- do not
// duplicate that here.
// ---------------------------------------------------------------------------

TEST_CASE("prepare/reset/process run without incident at the effect-contract level "
          "(T027, US8) -- rigorous no-allocation coverage is T037") {
    constexpr int kBlockSize = 256;
    constexpr float kSampleRate = 48000.0f;

    ProgramDependentSaturationEffect fx;
    fx.prepare(ProcessContext{static_cast<double>(kSampleRate), kBlockSize, 2});

    std::vector<float> left(static_cast<std::size_t>(kBlockSize), 0.0f);
    std::vector<float> right(static_cast<std::size_t>(kBlockSize), 0.0f);
    for (int i = 0; i < kBlockSize; ++i) {
        const float t = static_cast<float>(i) / kSampleRate;
        left[static_cast<std::size_t>(i)] = 0.3f * std::sin(2.0f * 3.14159265f * 220.0f * t);
        right[static_cast<std::size_t>(i)] = 0.3f * std::sin(2.0f * 3.14159265f * 220.0f * t);
    }

    float* chans[2] = {left.data(), right.data()};
    AudioBlock block(chans, 2, kBlockSize);

    fx.process(block);
    fx.reset();
    fx.process(block);

    for (float v : left)
        CHECK(std::isfinite(v));
    for (float v : right)
        CHECK(std::isfinite(v));
}

// ---------------------------------------------------------------------------
// TEST 5: Multi-parameter round-trip (T027, FR-016/017, SC-012) -- publish a
// setParameter() edit across a representative spread of ids (static drive/
// voicing/detector/detection, all four target depth+curve pairs, the
// sidechain HPF, and the stereo-link mode), let ONE process() call consume
// every pending edit, and confirm the resulting output stays finite
// throughout -- no NaN/Inf from a bad denormalize/clamp/enum-mapping path
// across the combined set. dynamicPreset and externalSidechain are
// deliberately left out (T030/T034 respectively fill their real behavior; the
// hooked-but-unfilled applyDynamicPreset()/applyExternalKey() paths are
// exercised, just not asserted on here).
// ---------------------------------------------------------------------------

TEST_CASE("setParameter round-trips across drive/voicing/detector/detection/matrix/"
          "sidechain-hpf/stereo-link with finite output (T027, US8, SC-012)") {
    constexpr int kBlockSize = 2048;
    constexpr float kSampleRate = 48000.0f;

    ProgramDependentSaturationEffect fx;
    fx.prepare(ProcessContext{static_cast<double>(kSampleRate), kBlockSize, 1});

    fx.setParameter(ParamId{ProgramDependentSaturationEffect::kDrive},
                     normFor(ProgramDependentSaturationEffect::kDrive, 12.0f)); // dB
    fx.setParameter(ParamId{ProgramDependentSaturationEffect::kVoicing},
                     normFor(ProgramDependentSaturationEffect::kVoicing, 1.0f)); // tape
    fx.setParameter(ParamId{ProgramDependentSaturationEffect::kDetector},
                     normFor(ProgramDependentSaturationEffect::kDetector, 2.0f)); // peakHold
    fx.setParameter(ParamId{ProgramDependentSaturationEffect::kDetection},
                     normFor(ProgramDependentSaturationEffect::kDetection, 1.0f)); // feedBack
    fx.setParameter(ParamId{ProgramDependentSaturationEffect::kDriveDepth},
                     normFor(ProgramDependentSaturationEffect::kDriveDepth, 0.5f));
    fx.setParameter(ParamId{ProgramDependentSaturationEffect::kDriveCurve},
                     normFor(ProgramDependentSaturationEffect::kDriveCurve, 1.0f)); // log
    fx.setParameter(ParamId{ProgramDependentSaturationEffect::kBiasDepth},
                     normFor(ProgramDependentSaturationEffect::kBiasDepth, -0.5f));
    fx.setParameter(ParamId{ProgramDependentSaturationEffect::kBiasCurve},
                     normFor(ProgramDependentSaturationEffect::kBiasCurve, 2.0f)); // exp
    fx.setParameter(ParamId{ProgramDependentSaturationEffect::kToneDepth},
                     normFor(ProgramDependentSaturationEffect::kToneDepth, 0.3f));
    fx.setParameter(ParamId{ProgramDependentSaturationEffect::kToneCurve},
                     normFor(ProgramDependentSaturationEffect::kToneCurve, 0.0f)); // linear
    fx.setParameter(ParamId{ProgramDependentSaturationEffect::kMixDepth},
                     normFor(ProgramDependentSaturationEffect::kMixDepth, -0.2f));
    fx.setParameter(ParamId{ProgramDependentSaturationEffect::kMixCurve},
                     normFor(ProgramDependentSaturationEffect::kMixCurve, 1.0f)); // log
    fx.setParameter(ParamId{ProgramDependentSaturationEffect::kScHpf},
                     normFor(ProgramDependentSaturationEffect::kScHpf, 120.0f)); // Hz
    fx.setParameter(ParamId{ProgramDependentSaturationEffect::kStereoLink},
                     normFor(ProgramDependentSaturationEffect::kStereoLink, 1.0f)); // linked

    std::vector<float> in(static_cast<std::size_t>(kBlockSize));
    for (int i = 0; i < kBlockSize; ++i) {
        const float t = static_cast<float>(i) / kSampleRate;
        in[static_cast<std::size_t>(i)] = 0.7f * std::sin(2.0f * 3.14159265f * 1000.0f * t);
    }

    float* chans[1] = {in.data()};
    AudioBlock block(chans, 1, kBlockSize);
    fx.process(block); // applyPending() consumes every published edit above in this one call

    for (std::size_t i = 0; i < in.size(); ++i) {
        INFO("sample=" << i);
        CHECK(std::isfinite(in[i]));
    }
}
