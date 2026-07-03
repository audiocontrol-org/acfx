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
#include "effects/compressor/compressor-effect.h"
#include "effects/compressor/compressor-parameters.h"

// T025 -- User Story 13 suite: CompressorEffect (the host-facing Effect
// contract wrapping CompressorCore). Mirrors the shipped
// tests/core/saturation-effect-test.cpp / saturation-effect-rt-test.cpp idiom
// exactly: the Effect-concept static_assert, AudioBlock/ProcessContext
// construction, setParameter(ParamId, normalized) calls, and the
// pending-atomic cross-thread handoff -- but scoped to the WRAPPER contract
// only (concept satisfaction, param handoff, descriptor validity). DSP
// correctness (curve shape, ballistics timing, sidechain routing, topology)
// is covered by the sibling suites (gain-computer-test.cpp,
// compressor-topology-test.cpp, compressor-sidechain-test.cpp,
// compressor-lookahead-test.cpp, compressor-makeup-link-test.cpp).
//
// References: specs/compressors/tasks.md T025 (SC-011); contracts/
// compressor-effect-api.md "CompressorEffect"; FR-018..021 (parameter
// handoff / descriptor table / latency reporting).

using namespace acfx;

namespace {

// Convert a desired PLAIN-units value for a CompressorEffect parameter into
// the normalized 0..1 value setParameter() expects, via the shared
// descriptor table -- mirrors saturation-effect-test-support.h's normFor.
// Never hand-roll the normalize math here: the descriptor table (min/max/
// skew) is the single source of truth (FR-019).
float normFor(CompressorEffect::Param p, float plainValue) {
    return normalize(CompressorEffect::kParams[p], plainValue);
}

// RMS over a tail window -- used to compare steady-state gain reduction
// across two parameter settings without asserting an exact DSP curve value
// (that belongs to gain-computer-test.cpp / compressor-topology-test.cpp).
double rms(const std::vector<float>& buf, std::size_t tailSamples) {
    const std::size_t n = buf.size();
    const std::size_t start = (tailSamples < n) ? (n - tailSamples) : 0;
    double sumSq = 0.0;
    std::size_t count = 0;
    for (std::size_t i = start; i < n; ++i) {
        sumSq += static_cast<double>(buf[i]) * static_cast<double>(buf[i]);
        ++count;
    }
    return count > 0 ? std::sqrt(sumSq / static_cast<double>(count)) : 0.0;
}

} // namespace

// ---------------------------------------------------------------------------
// TEST 1: Effect concept (SC-011) -- CompressorEffect satisfies the
// compile-time Effect contract (prepare/process/reset/parameters/
// setParameter, no base class/vtable). The test target compiles as C++20
// (tests/CMakeLists.txt: target_compile_features(... cxx_std_20)), so the
// named `acfx::Effect` concept (core/dsp/effect.h) is available; on a C++17
// toolchain (Teensy) the same member signatures are enforced by plain
// template instantiation (duck typing) plus the best-effort `is_effect_v`
// trait -- mirrors tests/core/modulated-delay-test.cpp's precedent exactly.
// ---------------------------------------------------------------------------

TEST_CASE("CompressorEffect satisfies the Effect concept (T025, SC-011)") {
    static_assert(acfx::Effect<CompressorEffect>,
                  "CompressorEffect must satisfy the Effect contract "
                  "(prepare/process/reset/parameters/setParameter)");
    // Nothing to check at runtime here -- the static_assert above is the
    // whole assertion; a failing concept check is a BUILD error, not a test
    // failure, so this TEST_CASE only exists to give the compile-time check
    // a place to live and a name in the doctest run log.
    CHECK(true);
}

// ---------------------------------------------------------------------------
// TEST 2: Descriptor-table invariants (FR-019, SC-011) -- the runtime mirror
// of CompressorEffect::kParams's compile-time static_assert (contract:
// "isValidDescriptor over the table fails compilation, not the audio path",
// compressor-effect.h). Confirms the table this test file's own build just
// compiled against is exactly 17 rows, densely indexed 0..16 (Param enum
// order), and that every row independently passes isValidDescriptor at
// runtime too -- not just at the header's static_assert.
// ---------------------------------------------------------------------------

TEST_CASE("CompressorEffect::parameters() returns exactly 17 valid, densely-indexed descriptors (T025, SC-011)") {
    const span<const ParameterDescriptor> params = CompressorEffect::parameters();
    REQUIRE(params.size() == 17);

    for (std::size_t i = 0; i < params.size(); ++i) {
        const ParameterDescriptor& d = params[i];
        INFO("index=" << i << " name=" << d.name);
        CHECK(isValidDescriptor(d));
        // Dense ids 0..16 in Param-enum order (compressor-effect.h's leading
        // comment: "the leading ParamId index on each row is that same dense
        // id").
        CHECK(d.id.value == static_cast<std::uint8_t>(i));
    }

    // Same table via the CompressorEffect::kParams alias -- confirms the
    // alias and parameters() agree (both are the SAME constexpr object, so
    // this is also a check that the alias wasn't accidentally shadowed).
    CHECK(&CompressorEffect::kParams[0] == params.data());
}

TEST_CASE("CompressorEffect discrete descriptors carry the documented label counts (T025, FR-019)") {
    // mode: {compress, limit, expand, gate} -- 4 choices.
    const auto& mode = CompressorEffect::kParams[CompressorEffect::kMode];
    CHECK(mode.kind == ParamKind::discrete);
    REQUIRE(mode.discreteCount == 4);
    REQUIRE(mode.choices.size() == 4);

    // detection, detector, ballisticsSite, autoMakeup, stereoLink -- all
    // binary discrete params (2 choices each).
    constexpr std::array<CompressorEffect::Param, 5> kBinaryDiscrete = {
        CompressorEffect::kDetection, CompressorEffect::kDetector,
        CompressorEffect::kBallisticsSite, CompressorEffect::kAutoMakeup,
        CompressorEffect::kStereoLink};
    for (const auto p : kBinaryDiscrete) {
        const auto& d = CompressorEffect::kParams[p];
        INFO("param index=" << static_cast<int>(p) << " name=" << d.name);
        CHECK(isValidDescriptor(d));
        CHECK(d.kind == ParamKind::discrete);
        REQUIRE(d.discreteCount == 2);
        REQUIRE(d.choices.size() == 2);
    }

    // The remaining 11 params (threshold, ratio, knee, attack, release,
    // range, scHpf, lookahead, makeup, mix, output) are continuous with
    // max > min -- isValidDescriptor already checks this per-row in TEST 2
    // above; spot-check a couple of the ones this file's other TEST_CASEs
    // exercise functionally.
    CHECK(CompressorEffect::kParams[CompressorEffect::kThreshold].kind == ParamKind::continuous);
    CHECK(CompressorEffect::kParams[CompressorEffect::kRatio].kind == ParamKind::continuous);
    CHECK(CompressorEffect::kParams[CompressorEffect::kMix].kind == ParamKind::continuous);
}

// ---------------------------------------------------------------------------
// TEST 3: Denormalization sanity (T025) -- normalized 0.0 and 1.0 map to a
// descriptor's documented min/max via denormalize() (the SAME function
// CompressorEffect::applyPending() uses internally to turn a published
// normalized value into the real plain-units value before forwarding it to
// CompressorCore). This is the wrapper's actual denormalization path, not a
// hand-rolled reimplementation of it.
// ---------------------------------------------------------------------------

TEST_CASE("normalized 0.0 and 1.0 denormalize to each continuous parameter's descriptor min/max (T025)") {
    constexpr std::array<CompressorEffect::Param, 11> kContinuous = {
        CompressorEffect::kThreshold, CompressorEffect::kRatio, CompressorEffect::kKnee,
        CompressorEffect::kAttack,    CompressorEffect::kRelease, CompressorEffect::kRange,
        CompressorEffect::kScHpf,     CompressorEffect::kLookahead, CompressorEffect::kMakeup,
        CompressorEffect::kMix,       CompressorEffect::kOutput};

    for (const auto p : kContinuous) {
        const auto& d = CompressorEffect::kParams[p];
        INFO("param index=" << static_cast<int>(p) << " name=" << d.name);
        CHECK(denormalize(d, 0.0f) == doctest::Approx(d.min));
        CHECK(denormalize(d, 1.0f) == doctest::Approx(d.max));
        // defaultValue is always within [min, max] (isValidDescriptor doesn't
        // check this, so assert it here directly against the descriptor).
        CHECK(d.defaultValue >= d.min);
        CHECK(d.defaultValue <= d.max);
    }

    // threshold/ratio's exact bounds are normative per compressor-parameters.h's
    // documented table (not just "max>min").
    const auto& threshold = CompressorEffect::kParams[CompressorEffect::kThreshold];
    CHECK(threshold.min == doctest::Approx(-60.0f));
    CHECK(threshold.max == doctest::Approx(0.0f));

    const auto& ratio = CompressorEffect::kParams[CompressorEffect::kRatio];
    CHECK(ratio.min == doctest::Approx(1.0f));
    CHECK(ratio.max == doctest::Approx(20.0f));
}

// ---------------------------------------------------------------------------
// TEST 4: mix normalized 0.0 -> exact dry passthrough at the EFFECT level
// (denormalization sanity, functional). mix's descriptor is {min=0, max=1},
// and CompressorCore::applyGain() computes
// y = mix*comp + (1-mix)*x, then *outputGainLin_ -- so mix denormalized to
// exactly 0.0 must yield y == x bit-for-bit (up to float rounding) with
// output/makeup left at their 0 dB defaults, REGARDLESS of threshold/ratio/
// mode, since the wet path is fully excluded from the blend. This is the
// most direct, unambiguous proof that a normalized boundary value (0.0) maps
// through the wrapper's applyPending() -> denormalize() -> CompressorCore
// path to its documented plain-units effect.
// ---------------------------------------------------------------------------

TEST_CASE("mix normalized 0.0 denormalizes to exact dry passthrough at the effect level (T025)") {
    constexpr int kBlockSize = 64;
    constexpr float kSampleRate = 48000.0f;

    CompressorEffect fx;
    fx.prepare(ProcessContext{static_cast<double>(kSampleRate), kBlockSize, 1});

    // Push threshold/ratio to the most aggressive corner of the range so a
    // bug that ignored mix would be loudly visible -- if mix were NOT
    // actually wired to 0.0, this input would come back heavily gain-reduced
    // instead of dry.
    fx.setParameter(ParamId{CompressorEffect::kThreshold}, normFor(CompressorEffect::kThreshold, -60.0f));
    fx.setParameter(ParamId{CompressorEffect::kRatio}, normFor(CompressorEffect::kRatio, 20.0f));
    fx.setParameter(ParamId{CompressorEffect::kMix}, 0.0f); // -> denormalize(..) == d.min == 0.0

    std::vector<float> in(static_cast<std::size_t>(kBlockSize));
    for (int i = 0; i < kBlockSize; ++i) {
        const float t = static_cast<float>(i) / kSampleRate;
        in[static_cast<std::size_t>(i)] = 0.8f * std::sin(2.0f * 3.14159265f * 1000.0f * t);
    }

    std::vector<float> out = in;
    float* chans[1] = {out.data()};
    AudioBlock block(chans, 1, kBlockSize);
    fx.process(block); // applyPending() consumes threshold/ratio/mix here, on this call

    for (int i = 0; i < kBlockSize; ++i) {
        INFO("sample=" << i);
        CHECK(out[static_cast<std::size_t>(i)] == doctest::Approx(in[static_cast<std::size_t>(i)]));
    }
}

// ---------------------------------------------------------------------------
// TEST 5: Cross-thread parameter handoff (FR-020, SC-011) -- a setParameter()
// published "from a non-audio thread" (a bare call here, single-threaded in
// the test but semantically identical to FR-020's requirement: publish never
// mutates audio-thread state directly) takes effect ONLY at the NEXT
// process() call, not before it and not retroactively. Mirrors
// saturation-effect-rt-test.cpp TEST 2 exactly: block A is captured under
// the OLD settings, snapshotted, THEN the new settings are published, THEN
// block B is processed and shown to diverge, THEN block A is re-checked to
// prove the later publish/process never reached back into already-produced
// output.
// ---------------------------------------------------------------------------

TEST_CASE("setParameter published off the audio thread takes effect only at the next process() call (T025, FR-020, SC-011)") {
    constexpr int kBlockSize = 4800; // 100 ms at 48 kHz -- long enough for attack ballistics to engage
    constexpr float kSampleRate = 48000.0f;
    // Tail window over which steady-state gain reduction is compared -- the
    // last 20 ms, well past the default 10 ms attack time constant.
    constexpr std::size_t kTailSamples = 960;

    CompressorEffect fx;
    fx.prepare(ProcessContext{static_cast<double>(kSampleRate), kBlockSize, 1});

    // Establish a known, mild baseline explicitly (threshold/ratio at their
    // documented defaults, published as an explicit setParameter() call --
    // not relying on the constructor's implicit defaults -- so this test
    // exercises the SAME publish -> applyPending() -> denormalize() path
    // TEST 4 does, just with a mild setting instead of an extreme one).
    // 0 dBFS input at -18 dB threshold, 4:1 ratio: ~13.5 dB gain reduction.
    fx.setParameter(ParamId{CompressorEffect::kThreshold}, normFor(CompressorEffect::kThreshold, -18.0f));
    fx.setParameter(ParamId{CompressorEffect::kRatio}, normFor(CompressorEffect::kRatio, 4.0f));
    fx.setParameter(ParamId{CompressorEffect::kMix}, normFor(CompressorEffect::kMix, 1.0f)); // fully wet

    std::vector<float> in(static_cast<std::size_t>(kBlockSize));
    for (int i = 0; i < kBlockSize; ++i) {
        const float t = static_cast<float>(i) / kSampleRate;
        in[static_cast<std::size_t>(i)] = std::sin(2.0f * 3.14159265f * 1000.0f * t); // 0 dBFS, 1 kHz
    }

    // Block A: the FIRST process() call applies every pending edit above
    // (mild threshold/ratio/mix).
    std::vector<float> blockA = in;
    {
        float* chans[1] = {blockA.data()};
        AudioBlock block(chans, 1, kBlockSize);
        fx.process(block);
    }
    const double rmsA = rms(blockA, kTailSamples);

    // Snapshot block A's ALREADY-PRODUCED output BEFORE any later publish or
    // later process() call -- the ground truth the immutability check below
    // is compared against.
    const std::vector<float> blockASnapshot = blockA;

    // Publish an extreme threshold/ratio edit "from a non-audio thread". No
    // process() call has consumed it yet -- per FR-020 this must NOT mutate
    // any audio-thread-visible state until the next process() call runs.
    fx.setParameter(ParamId{CompressorEffect::kThreshold}, normFor(CompressorEffect::kThreshold, -60.0f));
    fx.setParameter(ParamId{CompressorEffect::kRatio}, normFor(CompressorEffect::kRatio, 20.0f));

    // Block B: the NEXT process() call is where the pending threshold/ratio
    // edit is consumed (applyPending() runs at the top of process(), per the
    // SaturationEffect/SvfEffect idiom) -- its output now reflects a MUCH
    // deeper gain reduction. Processing this LATER block on the SAME input
    // is exactly the event that could, if the wrapper were buggy,
    // retroactively touch block A's buffer.
    std::vector<float> blockB = in;
    {
        float* chans[1] = {blockB.data()};
        AudioBlock block(chans, 1, kBlockSize);
        fx.process(block);
    }
    const double rmsB = rms(blockB, kTailSamples);

    // GENUINE past-output-immutability check: after the later publish AND
    // the later process() call, block A's already-produced output is
    // byte-for-byte what it was before -- the pending edit and the
    // subsequent block never reached back into already-emitted output.
    REQUIRE(blockA.size() == blockASnapshot.size());
    for (std::size_t i = 0; i < blockA.size(); ++i)
        CHECK(blockA[i] == doctest::Approx(blockASnapshot[i]));

    // Functional round-trip: the NEW (much more aggressive) threshold/ratio
    // setting was actually consumed -- block B's steady-state tail RMS is
    // markedly lower than block A's, proving the published normalized value
    // was denormalized and forwarded to CompressorCore's setters, not
    // silently dropped or applied a block late.
    INFO("rmsA=" << rmsA << " rmsB=" << rmsB);
    CHECK(rmsB < rmsA * 0.5);

    // Both stay finite/bounded -- a sanity floor, not a DSP-correctness claim.
    for (float v : blockB)
        CHECK(std::isfinite(v));
}

// ---------------------------------------------------------------------------
// TEST 6: latencySamples() reflects the lookahead parameter (FR-021) --
// a lightweight wrapper-contract check: publishing a nonzero lookahead value
// and letting process()'s applyPending() consume it must round
// (lookaheadSeconds * sampleRate) into latencySamples(). NOTE: prepare()'s
// applyAll() re-applies the current MEMBER state (lookaheadSeconds_), not the
// pending atomics -- only applyPending() (run at the top of process())
// consumes a setParameter() publish, so a lookahead edit only takes effect
// after the NEXT process() call, exactly like TEST 5's handoff. Not a
// DSP-correctness claim about the lookahead delay line itself
// (compressor-lookahead-test.cpp owns that); this only proves the reported
// host latency tracks the descriptor-table denormalization once consumed.
// ---------------------------------------------------------------------------

TEST_CASE("latencySamples() reports round(lookaheadSeconds * sampleRate) after the edit is consumed by process() (T025, FR-021)") {
    constexpr float kSampleRate = 48000.0f;

    CompressorEffect fxDefault;
    fxDefault.prepare(ProcessContext{static_cast<double>(kSampleRate), 64, 1});
    // Default lookahead is 0 seconds (compressor-parameters.h) -> 0 latency,
    // established by prepare()'s applyAll() with no process() call needed.
    CHECK(fxDefault.latencySamples() == 0);

    CompressorEffect fx;
    fx.prepare(ProcessContext{static_cast<double>(kSampleRate), 64, 1});
    fx.setParameter(ParamId{CompressorEffect::kLookahead}, normFor(CompressorEffect::kLookahead, 0.010f)); // 10 ms
    // Still 0 immediately after publish -- the pending edit has not been
    // consumed yet (no process() call has run since the publish).
    CHECK(fx.latencySamples() == 0);

    std::vector<float> buf(64, 0.1f);
    float* chans[1] = {buf.data()};
    AudioBlock block(chans, 1, 64);
    fx.process(block); // applyPending() consumes the lookahead edit here

    CHECK(fx.latencySamples() == static_cast<int>(std::lround(0.010f * kSampleRate)));
    for (float v : buf)
        CHECK(std::isfinite(v));
}
