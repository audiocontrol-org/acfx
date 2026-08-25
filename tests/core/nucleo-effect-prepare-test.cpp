#include <doctest/doctest.h>

#include "dsp/process-context.h"
#include "sample-format.h"

// T034 prepare-parameter contract (FR-036, FR-036a, FR-036b / D28).
//
// adapters/nucleo/effect-instance.h itself cannot be included here: it names
// ACFX_EFFECT_TYPE/ACFX_EFFECT_HEADER, which only exist as compile
// definitions on the firmware targets (acfx_add_effect_nucleo), not on this
// host test binary. What CAN be verified on the host, and what this file
// verifies, is the one number that matters: the constant nucleo-main.cpp's
// PrepareEffect() actually uses for maxBlockSize is kBlockFrames (48), and it
// is a DIFFERENT constant from kMaxPacketFrames (49) — the exact distinction
// FR-036b/D28 turns on. A regression that collapses the two constants, or
// that swaps which one feeds ProcessContext::maxBlockSize, changes nothing
// visible in a firmware link (both are plain ints); this is the test that
// would actually catch it.

using namespace acfx::nucleo;

TEST_CASE("T034: the effect prepare context uses kBlockFrames, not kMaxPacketFrames") {
    // FR-036/FR-036a: 48 kHz, 48 frames, stereo — mirrors effect-instance.h's
    // PrepareEffect() construction exactly.
    const acfx::ProcessContext ctx{48000.0, kBlockFrames, kChannels};

    CHECK(ctx.sampleRate == doctest::Approx(48000.0));
    CHECK(ctx.maxBlockSize == 48);
    CHECK(ctx.numChannels == 2);

    // FR-036b/D28: the two constants remain distinct. If a future edit ever
    // makes them equal, the guard below (and the firmware's own
    // static_asserts in effect-instance.h) both need re-examination against
    // D28, not silent deletion.
    CHECK(kBlockFrames != kMaxPacketFrames);
    CHECK(ctx.maxBlockSize != kMaxPacketFrames);
}
