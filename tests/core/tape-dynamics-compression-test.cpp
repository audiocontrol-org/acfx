#include <doctest/doctest.h>

#include <array>
#include <cmath>
#include <cstddef>

#include "effects/tape-dynamics/tape-dynamics-core.h"
#include "effects/tape-dynamics/tape-dynamics-effect.h"

// T021 -- User Story 4 / SC-003 / FR-012 acceptance: emergent dynamic
// compression is a PROPERTY of the saturating Jiles-Atherton magnetics, NOT a
// parameter (FR-012). T020's lab harness (core/labs/tape-dynamics/harness/
// tape-dynamics-harness.cpp) already measured this on TapeDynamicsCore<4> at
// trim OFF (the default): at 0 dB drive the response is near-linear/slightly
// EXPANSIVE (DRR ~= -0.58 dB); it becomes genuinely COMPRESSIVE only at
// higher drive (DRR ~= +2.81 dB at 18 dB drive). Compression manifests ABOVE
// A DRIVE THRESHOLD, not at unity -- this file's assertions are built around
// that measured reality (no assertion of compression at 0 dB drive).
//
// The DRR measurement methodology (settle N cycles, capture M cycles, RMS ->
// peak-equivalent dB) is reused VERBATIM in shape from the harness's own
// measureOutputLevelDb()/runDrrSweep() so this suite's figures are directly
// comparable to T020's printed evidence -- not a second, differently-biased
// measurement technique.
//
// Kept as its own file (not folded into tape-dynamics-effect-test.cpp, which
// is already ~410 lines) to stay under the Constitution VII ~300-500 line
// per-file budget.

using namespace acfx;

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kSampleRate = 48000.0;
constexpr double kToneHz = 200.0; // representative program-material tone (matches T020)
constexpr int kSettleCycles = 20;
constexpr int kCaptureCycles = 10;

// Configure a fresh TapeDynamicsCore<4> at the given drive, trim OFF (the
// explicit default -- T021's task brief requires trim disabled) and every
// other macro at its neutral default, exactly mirroring the harness's
// configureDrrCore().
void configureCore(TapeDynamicsCore<4>& core, float driveDb) {
    core.prepare(kSampleRate, 1);
    core.setDrive(driveDb);
    core.setSaturation(1.0f);
    core.setWidth(1.0f);
    core.setSolver(Solver::rk4);
    core.setMix(1.0f);
    core.setOutput(0.0f);
    core.setTrimEnabled(false); // explicit trim DISABLED (default) -- T021 requirement
}

// Settle kSettleCycles then capture kCaptureCycles of a kToneHz sinusoid at
// `inputLevelDb` (dBFS peak amplitude) through `core`, returning the
// RMS-based peak-equivalent output level in dB -- identical technique to the
// harness's measureOutputLevelDb() so input/output dB are on the same
// convention (peak-amplitude dB) and the two are directly comparable.
float measureOutputLevelDb(TapeDynamicsCore<4>& core, float inputLevelDb) {
    core.reset();
    const float amp = std::pow(10.0f, inputLevelDb / 20.0f);
    const double omega = 2.0 * kPi * kToneHz / kSampleRate;

    const int settleSamples = static_cast<int>(kSettleCycles * kSampleRate / kToneHz);
    const int captureSamples = static_cast<int>(kCaptureCycles * kSampleRate / kToneHz);

    int n = 0;
    for (int i = 0; i < settleSamples; ++i, ++n) {
        const float x = amp * static_cast<float>(std::sin(omega * n));
        static_cast<void>(core.processSample(x, 0));
    }

    double sumSq = 0.0;
    for (int i = 0; i < captureSamples; ++i, ++n) {
        const float x = amp * static_cast<float>(std::sin(omega * n));
        const float y = core.processSample(x, 0);
        sumSq += static_cast<double>(y) * static_cast<double>(y);
    }
    const double rms = std::sqrt(sumSq / static_cast<double>(captureSamples));
    const double peakEquivalent = rms * std::sqrt(2.0);
    // A silent/near-silent output has no meaningful dB level; floor rather
    // than emit -inf (defined behavior, not a hidden fallback). Named
    // tolerance: 1e-9 sits far below any level this suite's inputs (>= -24
    // dBFS) could plausibly ring down to.
    constexpr double kFloor = 1.0e-9;
    return static_cast<float>(
        20.0 * std::log10(peakEquivalent > kFloor ? peakEquivalent : kFloor));
}

// Sweep input levels (dBFS peak) at a fixed drive, low to high -- the same
// 5-point sweep T020's harness used.
constexpr std::array<float, 5> kInputLevelsDb = {-24.0f, -18.0f, -12.0f, -6.0f, 0.0f};

// DRR = (input dB range) - (output dB range) over kInputLevelsDb, at a fixed
// drive. DRR ~= 0 for a linear (uncompressed) stage; DRR > 0 as the stage
// compresses the loudest inputs relative to the quietest -- identical
// definition to the harness's runDrrSweep().
double measureDrr(float driveDb, std::array<float, kInputLevelsDb.size()>* outLevelsOut = nullptr) {
    TapeDynamicsCore<4> core;
    configureCore(core, driveDb);

    std::array<float, kInputLevelsDb.size()> outputDb{};
    for (std::size_t i = 0; i < kInputLevelsDb.size(); ++i)
        outputDb[i] = measureOutputLevelDb(core, kInputLevelsDb[i]);

    if (outLevelsOut != nullptr)
        *outLevelsOut = outputDb;

    const float inputRangeDb = kInputLevelsDb.back() - kInputLevelsDb.front();
    const float outputRangeDb = outputDb.back() - outputDb.front();
    return static_cast<double>(inputRangeDb - outputRangeDb);
}

} // namespace

// ---------------------------------------------------------------------------
// CASE 1 -- SC-003: monotonic, compressive level curve at a FIXED high drive.
//
// kHighDriveDb=18 dB matches T020's own "genuinely saturating" high-drive
// setting (measured DRR ~= +2.81 dB there). At this drive the output-vs-input
// level curve must be monotonic non-decreasing, AND the amount of gain
// reduction (input-level-change minus output-level-change, per segment) must
// be LARGER in the top segment of the sweep than in the bottom segment --
// i.e. gain reduction grows as input rises, the defining shape of a
// compressive (not merely saturating/clipping) curve.
// ---------------------------------------------------------------------------

TEST_CASE("TapeDynamicsCore<4> level curve is monotonic and compressive at high drive (SC-003, T021)") {
    constexpr float kHighDriveDb = 18.0f; // T020's genuinely-saturating regime

    TapeDynamicsCore<4> core;
    configureCore(core, kHighDriveDb);

    std::array<float, kInputLevelsDb.size()> outputDb{};
    for (std::size_t i = 0; i < kInputLevelsDb.size(); ++i)
        outputDb[i] = measureOutputLevelDb(core, kInputLevelsDb[i]);

    // Monotonic non-decreasing: raising the input level must never LOWER the
    // measured output level. Named tolerance: 0.01 dB absorbs RMS-measurement
    // float noise across a 10-cycle capture window without masking a real
    // non-monotonic regression.
    constexpr float kMonotonicToleranceDb = 0.01f;
    for (std::size_t i = 0; i + 1 < outputDb.size(); ++i) {
        INFO("segment " << i << ": input " << kInputLevelsDb[i] << "->" << kInputLevelsDb[i + 1]
                         << " dB, output " << outputDb[i] << "->" << outputDb[i + 1] << " dB");
        CHECK(outputDb[i + 1] >= outputDb[i] - kMonotonicToleranceDb);
    }

    // Per-segment gain reduction: how much less the output grew than the
    // input, in dB, over each of the 4 segments between the 5 swept levels.
    std::array<float, kInputLevelsDb.size() - 1> gainReductionDb{};
    for (std::size_t i = 0; i + 1 < kInputLevelsDb.size(); ++i) {
        const float inputStep = kInputLevelsDb[i + 1] - kInputLevelsDb[i];
        const float outputStep = outputDb[i + 1] - outputDb[i];
        gainReductionDb[i] = inputStep - outputStep;
    }

    const float bottomSegmentGr = gainReductionDb.front(); // -24 -> -18 dBFS
    const float topSegmentGr = gainReductionDb.back();     // -6 -> 0 dBFS
    INFO("bottomSegmentGr=" << bottomSegmentGr << " topSegmentGr=" << topSegmentGr);

    // Genuinely compressive at the top of the sweep: named floor of 0.1 dB --
    // comfortably above float/measurement noise (the monotonic-tolerance bar
    // above is 0.01 dB), well below T020's measured ~2.81 dB DRR at this
    // drive, so a real regression to "no compression" fails this bar without
    // the test being fragile to tuning-pass parameter drift.
    constexpr float kCompressiveFloorDb = 0.1f;
    CHECK(topSegmentGr > kCompressiveFloorDb);

    // Gain reduction grows as input rises: the top segment must compress
    // measurably harder than the bottom segment. Named margin: 0.1 dB --
    // matches the compressive floor above, so this is not a stricter bar
    // than "is compressive at all", just an independent shape check.
    constexpr float kGrowthMarginDb = 0.1f;
    CHECK(topSegmentGr > bottomSegmentGr + kGrowthMarginDb);
}

// ---------------------------------------------------------------------------
// CASE 2 -- SC-003: DRR rises with drive.
//
// kLowDriveDb=0 dB and kHighDriveDb=18 dB are T020's own low/high drive
// settings (measured DRR ~= -0.58 dB and ~= +2.81 dB respectively -- a ~3.4
// dB swing). Do NOT assert compression AT the low-drive point (T020 found it
// near-linear/slightly expansive there) -- only that DRR(high) > DRR(low),
// and that the high-drive point is genuinely in the compressive regime.
// ---------------------------------------------------------------------------

TEST_CASE("TapeDynamicsCore<4> dynamic-range reduction (DRR) rises with drive (SC-003, T021)") {
    constexpr float kLowDriveDb = 0.0f;   // T020: DRR ~= -0.58 dB (near-linear/expansive)
    constexpr float kHighDriveDb = 18.0f; // T020: DRR ~= +2.81 dB (genuinely compressive)

    const double drrLow = measureDrr(kLowDriveDb);
    const double drrHigh = measureDrr(kHighDriveDb);
    INFO("drrLow=" << drrLow << " dB (drive=" << kLowDriveDb << " dB), drrHigh=" << drrHigh
                    << " dB (drive=" << kHighDriveDb << " dB)");

    // DRR must rise with drive. Named margin: 1.0 dB -- well under T020's
    // measured ~3.4 dB swing between these two drive settings, comfortably
    // above any RMS-measurement float noise (the per-segment monotonic
    // tolerance in CASE 1 is 0.01 dB), so this fails loudly on a real
    // regression without being fragile to tuning-pass parameter drift.
    constexpr double kDrrGrowthMarginDb = 1.0;
    CHECK(drrHigh > drrLow + kDrrGrowthMarginDb);

    // The high-drive point must be genuinely compressive (DRR > 0), not just
    // "less expansive than the low-drive point" -- named floor of 1.0 dB,
    // comfortably under T020's measured ~2.81 dB at this same drive.
    constexpr double kHighDriveDrrFloorDb = 1.0;
    CHECK(drrHigh > kHighDriveDrrFloorDb);
}

// ---------------------------------------------------------------------------
// CASE 3 -- FR-012: emergent compression is NOT a parameter.
//
// The parameter set is EXACTLY {drive, saturation, width, solver,
// oversampling, trim.enabled, trim.attack, trim.release, trim.amount, mix,
// output} (data-model.md "Entity — TapeDynamicsParameters"); there is no
// "compression"/"ratio"/"threshold" control anywhere in the table. Asserted
// BOTH by an exact-name check against the documented 11-row order and by a
// substring scan ruling out any compression-flavored id sneaking in under a
// different position -- both compile-time (static_assert), so a future edit
// that added such a parameter would fail the BUILD, not just this test run.
// ---------------------------------------------------------------------------

namespace {

// Case-insensitive EXACT equality, constexpr-friendly (std::string_view's
// comparison/indexing operations are constexpr since C++17). Deliberately
// NOT a substring search: a naive substring scan for "ratio" false-positives
// on the legitimate "saturation" parameter name (satu-RATIO-n contains
// "ratio" as a literal substring), so this checks whole dot-separated tokens
// for equality instead (see tokenEqualsAnyForbidden below).
constexpr bool equalsCaseInsensitive(std::string_view a, std::string_view b) noexcept {
    if (a.size() != b.size())
        return false;
    for (std::size_t i = 0; i < a.size(); ++i) {
        char ca = a[i];
        char cb = b[i];
        if (ca >= 'A' && ca <= 'Z')
            ca = static_cast<char>(ca - 'A' + 'a');
        if (cb >= 'A' && cb <= 'Z')
            cb = static_cast<char>(cb - 'A' + 'a');
        if (ca != cb)
            return false;
    }
    return true;
}

// A parameter name is either a bare token ("drive") or dot-separated
// ("trim.enabled"). Check EACH token against the forbidden list by exact
// match, so "saturation" (which happens to contain "ratio" as a raw
// substring) is correctly NOT flagged, while a hypothetical "gain.ratio" or
// bare "threshold" parameter WOULD be.
constexpr bool tokenEqualsAnyForbidden(std::string_view token) noexcept {
    return equalsCaseInsensitive(token, "compression") ||
           equalsCaseInsensitive(token, "compress") ||
           equalsCaseInsensitive(token, "ratio") ||
           equalsCaseInsensitive(token, "threshold");
}

constexpr bool nameHasForbiddenToken(std::string_view name) noexcept {
    std::size_t start = 0;
    for (std::size_t i = 0; i <= name.size(); ++i) {
        if (i == name.size() || name[i] == '.') {
            if (tokenEqualsAnyForbidden(name.substr(start, i - start)))
                return true;
            start = i + 1;
        }
    }
    return false;
}

// The documented parameter set, in row order (data-model.md, mirrored by
// TapeDynamicsEffect::Param). No "compression"/"ratio"/"threshold" entry --
// dynamic compression is emergent from the magnetics, never exposed as a
// tunable macro (FR-012).
constexpr std::array<std::string_view, 11> kExpectedParamNames = {{
    "drive",
    "saturation",
    "width",
    "solver",
    "oversampling",
    "trim.enabled",
    "trim.attack",
    "trim.release",
    "trim.amount",
    "mix",
    "output",
}};

constexpr bool paramNamesMatchExactly() noexcept {
    if (kTapeDynamicsParams.size() != kExpectedParamNames.size())
        return false;
    for (std::size_t i = 0; i < kExpectedParamNames.size(); ++i)
        if (kTapeDynamicsParams[i].name != kExpectedParamNames[i])
            return false;
    return true;
}

constexpr bool noCompressionFlavoredParamName() noexcept {
    for (const ParameterDescriptor& d : kTapeDynamicsParams) {
        if (nameHasForbiddenToken(d.name))
            return false;
    }
    return true;
}

} // namespace

static_assert(TapeDynamicsEffect::kNumParams == 11,
              "TapeDynamicsEffect parameter count must stay at exactly 11 (FR-012: no "
              "compression parameter has been added to the documented set)");
static_assert(paramNamesMatchExactly(),
              "kTapeDynamicsParams must be EXACTLY {drive, saturation, width, solver, "
              "oversampling, trim.enabled, trim.attack, trim.release, trim.amount, mix, "
              "output} in that order (data-model.md) -- FR-012: no extra parameter (e.g. a "
              "compression ratio/threshold) may be introduced");
static_assert(noCompressionFlavoredParamName(),
              "no TapeDynamicsParameters entry may be named after compression/ratio/"
              "threshold -- FR-012: dynamic compression is EMERGENT from the saturating "
              "magnetics, never an explicit tunable parameter");

TEST_CASE("TapeDynamicsParameters has no compression/ratio/threshold parameter (FR-012, T021)") {
    // Runtime companion to the static_asserts above -- doctest needs at least
    // one TEST_CASE to report this file's coverage; the actual guard already
    // ran at compile time.
    CHECK(TapeDynamicsEffect::kNumParams == 11);
    CHECK(paramNamesMatchExactly());
    CHECK(noCompressionFlavoredParamName());
}
