#include <doctest/doctest.h>

// Regression guard (review round 2): the CompressorEffect and
// ProgramDependentSaturationEffect headers MUST coexist in a single translation
// unit with no namespace-scope name collision. Both dynamics effects define
// analogous enums (e.g. a feedforward/feedback detection topology); if either
// re-introduces an `acfx`-scope enum the other also defines, an enum
// REDEFINITION is a hard compile error and THIS FILE fails to build.
//
// The prior collision: both cores defined `acfx::Detection`. Resolved by
// renaming the program-dependent-saturation enum to `acfx::PdsDetection`
// (StereoLink/DynamicPreset were already nested in the effect for the same
// reason). Including both headers below is the actual assertion — it must
// compile. The runtime CHECK only anchors a doctest case around it.
#include "effects/compressor/compressor-effect.h"
#include "effects/program-dependent-saturation/program-dependent-saturation-effect.h"

TEST_CASE("CompressorEffect and ProgramDependentSaturationEffect coexist in one TU "
          "(no acfx-scope enum redefinition)") {
    acfx::CompressorEffect compressor;
    acfx::ProgramDependentSaturationEffect pds;
    (void)compressor;
    (void)pds;
    // Distinct, non-colliding detection enums live in each effect's own scope.
    CHECK(static_cast<int>(acfx::PdsDetection::feedBack) ==
          static_cast<int>(acfx::Detection::feedBack));
}
