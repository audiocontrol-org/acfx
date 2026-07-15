#include "doctest/doctest.h"
#include "svf-web-abi.h"
#include "dsp/audio-block.h"
#include "dsp/param-id.h"
#include "dsp/process-context.h"
#include "effects/svf/svf-effect.h"
#include <vector>

// The ABI must produce bit-identical output to calling acfx::SvfEffect directly
// with the same prepare/param/process sequence (the ABI adds no DSP of its own).
TEST_CASE("svf ABI matches direct SvfEffect output") {
    constexpr double sr = 48000.0;
    constexpr int n = 256;
    std::vector<float> in(n);
    for (int i = 0; i < n; ++i) in[i] = 0.5f * static_cast<float>((i % 32) - 16) / 16.0f;

    // Direct reference.
    std::vector<float> direct = in;
    acfx::SvfEffect fx;
    fx.prepare(acfx::ProcessContext{sr, n, 1});
    fx.setParameter(acfx::ParamId{0}, 0.5f);   // cutoff mid
    fx.setParameter(acfx::ParamId{1}, 0.3f);   // resonance
    fx.setParameter(acfx::ParamId{2}, 0.0f);   // lowpass
    {
        float* chans[1] = {direct.data()};
        acfx::AudioBlock io(chans, 1, n);
        fx.process(io);
    }

    // Through the ABI.
    std::vector<float> viaAbi = in;
    SvfHandle* h = svf_create();
    svf_prepare(h, sr, n, 1);
    svf_set_param(h, 0, 0.5f);
    svf_set_param(h, 1, 0.3f);
    svf_set_param(h, 2, 0.0f);
    svf_process(h, viaAbi.data(), n);
    svf_destroy(h);

    // doctest::Approx(x).epsilon(0.0f) is unusable here: Approx's equality is a
    // strict "<" (fabs(lhs-rhs) < epsilon*(scale+max(|lhs|,|rhs|))), so an
    // epsilon of exactly 0 makes the RHS 0 and the check can never pass, even
    // for bit-identical values. The ABI adds no DSP of its own, so plain float
    // equality is the correct (and passable) assertion of "bit-identical".
    for (int i = 0; i < n; ++i) CHECK(viaAbi[i] == direct[i]);
}
