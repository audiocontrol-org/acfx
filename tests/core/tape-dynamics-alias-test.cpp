#include <doctest/doctest.h>

#include "effects/tape-dynamics/tape-dynamics-effect.h"

using namespace acfx;

TEST_CASE("TapeDynamicsEffect aliasing (T025)") {
    // TODO(T025): Implement TapeDynamicsEffect aliasing characterization:
    // harmonic-content stability across sample-rate/drive range, low-rate
    // artefacts (foldback), oversampling impact, and aliasing-energy bounds.
    CHECK(true);
}
