#include <doctest/doctest.h>

#include <cmath>
#include <limits>
#include <string_view>

#include "dsp/param-id.h"
#include "dsp/parameter.h"
#include "effects/modulated-delay/modulated-delay-effect.h"
#include "effects/svf/svf-effect.h"

// T014 — parameter scaling/skew: assert linear / logarithmic / discrete mapping
// at min/mid/max plus out-of-range clamping. Fails until parameter.h (T008) is
// correct. DaisySP-independent — exercises the pure mapping only.

using namespace acfx;

namespace {
constexpr ParameterDescriptor linearRes{ParamId{1}, "resonance", ParamUnit::none,
                                         0.0f,       1.0f,        0.0f,
                                         ParamSkew::linear,       ParamKind::continuous,
                                         0};

constexpr ParameterDescriptor logCutoff{ParamId{0}, "cutoff", ParamUnit::hz,
                                         20.0f,      20000.0f, 1000.0f,
                                         ParamSkew::logarithmic, ParamKind::continuous,
                                         0};

constexpr ParameterDescriptor discreteMode{ParamId{2}, "mode", ParamUnit::none,
                                            0.0f,       2.0f,   0.0f,
                                            ParamSkew::linear,  ParamKind::discrete,
                                            3};
} // namespace

TEST_CASE("linear mapping at min/mid/max") {
    CHECK(denormalize(linearRes, 0.0f) == doctest::Approx(0.0f));
    CHECK(denormalize(linearRes, 0.5f) == doctest::Approx(0.5f));
    CHECK(denormalize(linearRes, 1.0f) == doctest::Approx(1.0f));

    CHECK(normalize(linearRes, 0.0f) == doctest::Approx(0.0f));
    CHECK(normalize(linearRes, 0.5f) == doctest::Approx(0.5f));
    CHECK(normalize(linearRes, 1.0f) == doctest::Approx(1.0f));
}

TEST_CASE("logarithmic mapping spans min/max with geometric midpoint") {
    CHECK(denormalize(logCutoff, 0.0f) == doctest::Approx(20.0f));
    CHECK(denormalize(logCutoff, 1.0f) == doctest::Approx(20000.0f));
    // log midpoint is the geometric mean: sqrt(20 * 20000) ~= 632.4555
    CHECK(denormalize(logCutoff, 0.5f) == doctest::Approx(632.4555f).epsilon(0.001));

    // round-trips
    for (float n : {0.0f, 0.25f, 0.5f, 0.75f, 1.0f}) {
        const float plain = denormalize(logCutoff, n);
        CHECK(normalize(logCutoff, plain) == doctest::Approx(n).epsilon(0.0005));
    }
}

TEST_CASE("discrete mapping quantizes to buckets and round-trips by index") {
    CHECK(denormalize(discreteMode, 0.0f) == doctest::Approx(0.0f));
    CHECK(denormalize(discreteMode, 0.5f) == doctest::Approx(1.0f)); // floor(0.5*3)=1
    CHECK(denormalize(discreteMode, 0.99f) == doctest::Approx(2.0f));
    CHECK(denormalize(discreteMode, 1.0f) == doctest::Approx(2.0f)); // clamped to count-1

    for (int idx = 0; idx < 3; ++idx) {
        const float norm = normalize(discreteMode, static_cast<float>(idx));
        CHECK(static_cast<int>(denormalize(discreteMode, norm)) == idx);
    }
}

TEST_CASE("out-of-range normalized inputs clamp to the parameter bounds") {
    CHECK(denormalize(linearRes, -0.5f) == doctest::Approx(0.0f));
    CHECK(denormalize(linearRes, 1.5f) == doctest::Approx(1.0f));
    CHECK(denormalize(logCutoff, -1.0f) == doctest::Approx(20.0f));
    CHECK(denormalize(logCutoff, 2.0f) == doctest::Approx(20000.0f));
}

TEST_CASE("non-finite normalized inputs are neutralized, never propagated") {
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float inf = std::numeric_limits<float>::infinity();

    // NaN must map to the minimum (0 normalized), not pass through and poison the
    // filter state. +inf -> max, -inf -> min. All results must be finite.
    for (const ParameterDescriptor& d : {linearRes, logCutoff, discreteMode}) {
        CHECK(std::isfinite(denormalize(d, nan)));
        CHECK(std::isfinite(denormalize(d, inf)));
        CHECK(std::isfinite(denormalize(d, -inf)));
    }
    CHECK(denormalize(logCutoff, nan) == doctest::Approx(20.0f));
    CHECK(denormalize(logCutoff, inf) == doctest::Approx(20000.0f));
    CHECK(denormalize(logCutoff, -inf) == doctest::Approx(20.0f));
}

TEST_CASE("isValidDescriptor rejects discrete descriptor with mismatched choices count") {
    // discreteMode has choices defaulting to empty (size 0) but discreteCount 3.
    CHECK(!isValidDescriptor(discreteMode));

    // Explicit mismatch: discreteCount 4 but only 3 labels.
    static constexpr std::array<std::string_view, 3> threeLabels = {{"a", "b", "c"}};
    constexpr ParameterDescriptor mismatch{ParamId{9}, "mismatch", ParamUnit::none,
                                           0.0f, 3.0f, 0.0f,
                                           ParamSkew::linear, ParamKind::discrete,
                                           4, threeLabels};
    CHECK(!isValidDescriptor(mismatch));
}

TEST_CASE("SvfEffect mode descriptor carries correct option labels") {
    const auto& d = SvfEffect::kParams[SvfEffect::kMode];
    CHECK(isValidDescriptor(d));
    REQUIRE(d.choices.size() == 3);
    CHECK(d.choices[0] == std::string_view{"lowpass"});
    CHECK(d.choices[1] == std::string_view{"highpass"});
    CHECK(d.choices[2] == std::string_view{"bandpass"});
}

TEST_CASE("ModulatedDelayEffect fb_mode descriptor carries correct option labels") {
    const auto& d = ModulatedDelayEffect::kParams[ModulatedDelayEffect::kMode];
    CHECK(isValidDescriptor(d));
    REQUIRE(d.choices.size() == 3);
    CHECK(d.choices[0] == std::string_view{"lowpass"});
    CHECK(d.choices[1] == std::string_view{"highpass"});
    CHECK(d.choices[2] == std::string_view{"bandpass"});
}

TEST_CASE("ModulatedDelayEffect LFO shape descriptors carry correct option labels") {
    constexpr std::array<ModulatedDelayEffect::Param, 3> shapeParams = {{
        ModulatedDelayEffect::kDelayModShape,
        ModulatedDelayEffect::kCutoffModShape,
        ModulatedDelayEffect::kResModShape,
    }};
    for (const auto p : shapeParams) {
        const auto& d = ModulatedDelayEffect::kParams[p];
        CHECK(isValidDescriptor(d));
        REQUIRE(d.choices.size() == 4);
        CHECK(d.choices[0] == std::string_view{"sine"});
        CHECK(d.choices[1] == std::string_view{"triangle"});
        CHECK(d.choices[2] == std::string_view{"saw"});
        CHECK(d.choices[3] == std::string_view{"random"});
    }
}
