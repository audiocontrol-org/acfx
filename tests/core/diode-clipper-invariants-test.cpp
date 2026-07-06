#include <doctest/doctest.h>

#include <array>
#include <cmath>

#include "core/diode-clipper-test-support.h"
#include "primitives/circuit/diode-clipper/clipper-config.h"
#include "primitives/circuit/diode-clipper/diode-clipper.h"

// Tier-2 US3 assembled-clipper invariants for the diode-clippers lab (SC-003/004/
// 005). Split from diode-clipper-transient-test.cpp (which proves the solver exact
// + the bounds/non-convergence contracts) to stay under the Constitution VII
// per-file line budget; the shared drive/measurement rig lives in
// diode-clipper-test-support.h. Each clipper is validated by the behavioral
// invariants its topology dictates, on the now-proven-exact solver.

using acfx::AsymmetricShuntValues;
using acfx::asymmetricShuntClipper;
using acfx::DiodeSpec;
using acfx::seriesClipper;
using acfx::SeriesValues;
using acfx::SymmetricShuntValues;
using acfx::symmetricShuntClipper;
using acfx::labs::diode_clippers::TransientClipper;

using namespace dcl_test;

// ---------------------------------------------------------------------------
// T016 (SC-003) — symmetry: the symmetric shunt clipper's transfer is odd
// (y(−x) = −y(x), no DC offset); the asymmetric clipper's is measurably NOT odd
// (a DC-offset / even-harmonic component distinguishes the two).
// ---------------------------------------------------------------------------

TEST_CASE("TransientClipper - symmetric shunt transfer is odd; asymmetric is not") {
    const double R = 2200.0;
    const double Cf = 10.0e-9;
    const DiodeSpec d = diodeSpec();
    constexpr int kSettle = 4000;

    TransientClipper<4, 8> sym;
    TransientClipper<4, 8> asym;

    double maxSymAsymmetry = 0.0;   // |y(+x) + y(-x)| for the symmetric clipper
    double maxAsymAsymmetry = 0.0;  // |y(+x) + y(-x)| for the asymmetric clipper

    for (double x = 0.25; x <= 2.0 + 1e-9; x += 0.25) {
        const auto sPos = symmetricShuntClipper(SymmetricShuntValues{R, Cf, d}, x);
        const auto sNeg = symmetricShuntClipper(SymmetricShuntValues{R, Cf, d}, -x);
        const double yPos = settlePortDC(sym, sPos.netlist, kDt, kSettle);
        const double yNeg = settlePortDC(sym, sNeg.netlist, kDt, kSettle);
        maxSymAsymmetry = std::max(maxSymAsymmetry, std::fabs(yPos + yNeg));

        const auto aPos = asymmetricShuntClipper(AsymmetricShuntValues{R, Cf, d, 2, 1}, x);
        const auto aNeg = asymmetricShuntClipper(AsymmetricShuntValues{R, Cf, d, 2, 1}, -x);
        const double ayPos = settlePortDC(asym, aPos.netlist, kDt, kSettle);
        const double ayNeg = settlePortDC(asym, aNeg.netlist, kDt, kSettle);
        maxAsymAsymmetry = std::max(maxAsymAsymmetry, std::fabs(ayPos + ayNeg));
    }

    // Symmetric: odd within tight tolerance.
    CHECK(maxSymAsymmetry < 1e-6);
    // Asymmetric: measurably non-odd — the two clippers are distinguishable.
    CHECK(maxAsymAsymmetry > 1e-3);
}

// ---------------------------------------------------------------------------
// T017 (SC-004) — forward saturation (output clamps near the diode drop under
// large drive) + passivity (output energy ≤ input energy over a bounded
// excitation; the network dissipates, never adds gain).
// ---------------------------------------------------------------------------

TEST_CASE("TransientClipper - forward saturation clamps near the diode drop") {
    const double R = 2200.0;
    const double Cf = 10.0e-9;
    const DiodeSpec d = diodeSpec();
    constexpr int kSettle = 4000;
    const double kDrive = 5.0;  // far past the ~0.5 V silicon forward drop

    // Shunt clippers clamp their DC output near the forward drop, well under drive.
    TransientClipper<4, 8> sym;
    const auto s = symmetricShuntClipper(SymmetricShuntValues{R, Cf, d}, kDrive);
    CHECK(std::fabs(settlePortDC(sym, s.netlist, kDt, kSettle)) < 0.8);

    TransientClipper<4, 8> asym;
    const auto a = asymmetricShuntClipper(AsymmetricShuntValues{R, Cf, d, 2, 1}, kDrive);
    CHECK(std::fabs(settlePortDC(asym, a.netlist, kDt, kSettle)) < 1.6);  // ~2 stacked drops

    // Series clipper saturates the AC signal: its steady-state output peak is
    // bounded near the diode drop, far under the drive peak.
    TransientClipper<5, 8> ser;
    std::array<double, kWindow> in{}, out{};
    driveSine(ser, [&](double vIn) { return seriesClipper(SeriesValues{47.0e-9, 4700.0, d, 1}, vIn); },
              kDrive, 1000.0, in, out);
    double peak = 0.0;
    for (double v : out) {
        peak = std::max(peak, std::fabs(v));
    }
    CHECK(peak < 0.9);
}

TEST_CASE("TransientClipper - passivity: output energy <= input energy") {
    const double R = 2200.0;
    const double Cf = 10.0e-9;
    const DiodeSpec d = diodeSpec();
    const double amp = 2.0;
    std::array<double, kWindow> in{}, out{};

    {
        TransientClipper<4, 8> sym;
        driveSine(sym, [&](double v) { return symmetricShuntClipper(SymmetricShuntValues{R, Cf, d}, v); },
                  amp, 1000.0, in, out);
        CHECK(energy(out) <= energy(in));
    }
    {
        TransientClipper<4, 8> asym;
        driveSine(asym, [&](double v) { return asymmetricShuntClipper(AsymmetricShuntValues{R, Cf, d, 2, 1}, v); },
                  amp, 1000.0, in, out);
        CHECK(energy(out) <= energy(in));
    }
    {
        TransientClipper<5, 8> ser;
        driveSine(ser, [&](double v) { return seriesClipper(SeriesValues{47.0e-9, 4700.0, d, 1}, v); },
                  amp, 1000.0, in, out);
        CHECK(energy(out) <= energy(in));
    }
}

// ---------------------------------------------------------------------------
// T018 (SC-005) — the reactive signature (the behavior a static transfer curve
// cannot represent): for EACH shunt clipper, a fixed 1 kHz sine driven into
// clipping, increasing the filter capacitor Cf strictly reduces the output
// spectral energy above 5 kHz at every step of an ascending sweep.
// ---------------------------------------------------------------------------

TEST_CASE("TransientClipper - reactive signature: larger Cf strictly cuts post-clip HF") {
    const double R = 2200.0;
    const DiodeSpec d = diodeSpec();
    const double amp = 2.0;  // into clipping
    const double fs = 1.0 / kDt;
    const std::array<double, 6> cfSweep{1.0e-9, 2.2e-9, 4.7e-9, 10.0e-9, 22.0e-9, 47.0e-9};

    std::array<double, kWindow> in{}, out{};

    SUBCASE("symmetric shunt") {
        TransientClipper<4, 8> sym;
        double prevHf = 1e300;
        for (double Cf : cfSweep) {
            driveSine(sym, [&](double v) { return symmetricShuntClipper(SymmetricShuntValues{R, Cf, d}, v); },
                      amp, 1000.0, in, out);
            const double hf = hfEnergyAbove(out, fs, 5000.0);
            CHECK(hf < prevHf);  // strictly decreasing across the ascending sweep
            prevHf = hf;
        }
    }

    SUBCASE("asymmetric shunt") {
        TransientClipper<4, 8> asym;
        double prevHf = 1e300;
        for (double Cf : cfSweep) {
            driveSine(asym, [&](double v) { return asymmetricShuntClipper(AsymmetricShuntValues{R, Cf, d, 2, 1}, v); },
                      amp, 1000.0, in, out);
            const double hf = hfEnergyAbove(out, fs, 5000.0);
            CHECK(hf < prevHf);
            prevHf = hf;
        }
    }
}
