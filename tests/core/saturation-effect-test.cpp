#include <doctest/doctest.h>

#include <array>

#include "dsp/parameter.h"
#include "dsp/span.h"
#include "effects/saturation/saturation-core.h"    // SaturationVoicing / SaturationQuality
#include "effects/saturation/saturation-effect.h"  // SaturationEffect (T015/T016)

// T014 -- User Story 3 suite: SaturationEffect (the host-facing Effect
// contract wrapping SaturationCore). Written TEST-FIRST against
// specs/saturation/contracts/saturation-api.md's "SaturationEffect" section
// while the header itself (core/effects/saturation/saturation-effect.h) is
// authored in parallel by T015/T016 from the SAME contract. This file
// mirrors the shipped SvfEffect idiom (core/effects/svf/svf-effect.h,
// tests/core/svf-test.cpp, tests/core/no-allocation-test.cpp) exactly:
// AudioBlock/ProcessContext construction, setParameter(ParamId, normalized)
// calls, and the pending-atomic cross-thread handoff.
//
// References: FR-009 (single parameter descriptor table), FR-010 (cross-
// thread setParameter, applied at a block boundary, no allocation/lock on
// publish), FR-011 (prepare/reset/process/setParameter, no base class/
// vtable), FR-012 (mix blend law); spec.md User Story 3 Acceptance Scenarios
// 1-3, SC-005.
//
// SCOPE (post-split): this file holds only the STATIC / descriptor-surface
// cases (FR-009: parameters() count/validity, discrete voicing/quality label
// counts, continuous-param ranges). The runtime/RT behavior cases (cross-
// thread handoff, no-allocation-on-process, mix blend law, user-bias
// harmonics) moved to tests/core/saturation-effect-rt-test.cpp -- both files
// were originally one, split for the per-file governance byte envelope. The
// shared anonymous-namespace stimulus/driver helpers the RT file needs live
// in tests/core/saturation-effect-test-support.h; this file needs none of
// them (every case here reads only the compile-time descriptor table).

using namespace acfx;

// ---------------------------------------------------------------------------
// TEST 1a/1b/1c: Descriptor-table invariants (FR-009) -- the runtime mirror
// of SaturationEffect::kParams's compile-time static_assert (contract:
// "isValidDescriptor over the table fails compilation, not the audio path").
// ---------------------------------------------------------------------------

TEST_CASE("SaturationEffect::parameters() returns exactly seven valid descriptors (FR-009)") {
    const span<const ParameterDescriptor> params = SaturationEffect::parameters();
    REQUIRE(params.size() == 7);
    for (const ParameterDescriptor& d : params)
        CHECK(isValidDescriptor(d));
}

TEST_CASE("SaturationEffect voicing and quality descriptors are discrete with correct label counts (FR-009)") {
    const auto& voicingDesc = SaturationEffect::kParams[SaturationEffect::kVoicing];
    CHECK(isValidDescriptor(voicingDesc));
    CHECK(voicingDesc.kind == ParamKind::discrete);
    REQUIRE(voicingDesc.discreteCount == 4);
    REQUIRE(voicingDesc.choices.size() == 4);
    for (std::size_t i = 0; i < 4; ++i)
        CHECK(voicingDesc.choices[i] == SaturationEffect::kVoicingLabels[i]);

    const auto& qualityDesc = SaturationEffect::kParams[SaturationEffect::kQuality];
    CHECK(isValidDescriptor(qualityDesc));
    CHECK(qualityDesc.kind == ParamKind::discrete);
    REQUIRE(qualityDesc.discreteCount == 2);
    REQUIRE(qualityDesc.choices.size() == 2);
    for (std::size_t i = 0; i < 2; ++i)
        CHECK(qualityDesc.choices[i] == SaturationEffect::kQualityLabels[i]);
}

TEST_CASE("SaturationEffect continuous parameters have sane, documented ranges (FR-009)") {
    constexpr std::array<SaturationEffect::Param, 5> kContinuous = {
        SaturationEffect::kDrive, SaturationEffect::kTone, SaturationEffect::kMix,
        SaturationEffect::kOutput, SaturationEffect::kBias};

    for (const auto p : kContinuous) {
        const auto& d = SaturationEffect::kParams[p];
        INFO("param index=" << static_cast<int>(p) << " name=" << d.name);
        CHECK(isValidDescriptor(d));
        CHECK(d.kind == ParamKind::continuous);
        CHECK(d.max > d.min);
        CHECK(d.defaultValue >= d.min);
        CHECK(d.defaultValue <= d.max);
    }

    // data-model.md fixes tone/mix/bias exactly (no "e.g." qualifier, unlike
    // drive/output which are an open tuning decision): tone/bias span
    // -1..+1, mix spans the meaningful dry/wet range 0..1.
    const auto& tone = SaturationEffect::kParams[SaturationEffect::kTone];
    CHECK(tone.unit == ParamUnit::none);
    CHECK(tone.min == doctest::Approx(-1.0f));
    CHECK(tone.max == doctest::Approx(1.0f));

    const auto& bias = SaturationEffect::kParams[SaturationEffect::kBias];
    CHECK(bias.unit == ParamUnit::none);
    CHECK(bias.min == doctest::Approx(-1.0f));
    CHECK(bias.max == doctest::Approx(1.0f));

    const auto& mix = SaturationEffect::kParams[SaturationEffect::kMix];
    CHECK(mix.unit == ParamUnit::none);
    CHECK(mix.min == doctest::Approx(0.0f));
    CHECK(mix.max == doctest::Approx(1.0f));

    // drive/output are dB-unit, open-range ("e.g.") per data-model.md --
    // only assert the unit, not the exact bounds.
    CHECK(SaturationEffect::kParams[SaturationEffect::kDrive].unit == ParamUnit::decibels);
    CHECK(SaturationEffect::kParams[SaturationEffect::kOutput].unit == ParamUnit::decibels);
}
