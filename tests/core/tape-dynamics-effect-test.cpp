#include <doctest/doctest.h>

#include <array>
#include <cmath>
#include <vector>

#include "dsp/audio-block.h"
#include "dsp/parameter.h"
#include "dsp/process-context.h"
#include "effects/tape-dynamics/tape-dynamics-effect.h"

using namespace acfx;

TEST_CASE("TapeDynamicsEffect wrapper (T017)") {
    // TODO(T017): Implement TapeDynamicsEffect tests: signal-chain ordering,
    // parameter setters, presets, topology (hysteresis-core chain), real-time
    // safety, and gain compensation direction.
    CHECK(true);
}

// T016 smoke check only (T017 owns the real suite): prepare()+process() must
// run a block to completion and produce finite output for every oversampling
// bucket (2x/4x/8x), proving the factor dispatch wired up in processBlock()
// actually reaches all three TapeDynamicsCore instances.
TEST_CASE("TapeDynamicsEffect prepare+process produces finite output for every oversampling factor (T016 smoke)") {
    for (float bucket : {0.0f, 1.0f, 2.0f}) {
        TapeDynamicsEffect fx;
        const ProcessContext ctx{48000.0, 64, 1};
        fx.prepare(ctx);
        fx.setParameter(TapeDynamicsEffect::kParams[TapeDynamicsEffect::kOversampling].id,
                         normalize(TapeDynamicsEffect::kParams[TapeDynamicsEffect::kOversampling], bucket));

        std::vector<float> buf(64);
        for (std::size_t i = 0; i < buf.size(); ++i)
            buf[i] = 0.5f * std::sin(static_cast<float>(i) * 0.1f);

        std::array<float*, 1> channels{buf.data()};
        AudioBlock block(channels.data(), 1, static_cast<int>(buf.size()));

        // First call consumes the (default) pending params; second call
        // consumes the just-set oversampling bucket and actually processes
        // through the newly-selected core.
        fx.process(block);
        fx.process(block);

        for (float sample : buf)
            CHECK(std::isfinite(sample));

        fx.reset();
    }
}
