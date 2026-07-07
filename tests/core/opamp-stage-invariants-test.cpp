#include <doctest/doctest.h>

#include <array>
#include <cmath>
#include <cstddef>

#include "core/opamp-stage-test-support.h"
#include "primitives/circuit/opamp-stage/opamp-config.h"
#include "primitives/circuit/opamp-stage/opamp-stage.h"

// Tier-2 US3 assembled-stage invariants for the opamp-stages lab (T021-T023;
// SC-004/SC-005; FR-021/FR-022). Split from opamp-stage-solve-test.cpp (which
// proves the nullor-augmented solver exact on the linear stages, the
// independent DC-limit bisection oracle, and the bounded-charter tripwires /
// non-convergence contract — T016-T020) to stay under the Constitution VII
// per-file line budget; the shared sine-drive / DC-settle / energy /
// spectral-energy measurement rig lives in opamp-stage-test-support.h (also
// reused by the lab harness, opamp-stages-harness.cpp — mirrors the
// diode-clippers split, tests/core/diode-clipper-{test-support,invariants-test}.h/.cpp).
// Each invariant runs on the now-proven-exact solver (T016-T020).

using acfx::ActiveFirstOrderBom;
using acfx::opamp_stage::DiodeSpec;
using acfx::OpAmpDiodeClipperBom;
using acfx::activeFirstOrder;
using acfx::kMaxOpAmpClipperDiodes;
using acfx::opAmpDiodeClipper;
using acfx::opamp_stage::siliconSignalDiode;

using acfx::labs::opamp_stages::NullorSolver;
using acfx::labs::opamp_stages::OpAmpClipperSolver;

using namespace opst_test;

namespace {

// Shared BOM values across all three invariants below: a representative
// TS808-scale clean-gain floor (Rf/Rin ~ 4.7), matching the values already
// proven against the DC-limit oracle (T018) and the tripwire tests
// (T019/T020) in opamp-stage-solve-test.cpp.
constexpr double kRin = 10000.0;
constexpr double kRf = 47000.0;
constexpr double kCf = 10.0e-9;
constexpr double kDt = 1.0e-5;  // 100 kHz (FR-022's pinned lab convention)
constexpr int kWarmup = 3000;   // 30 ms -> steady state before measuring
constexpr int kWindow = 1000;   // 10 periods of a 1 kHz drive

using ClipperSolver =
    OpAmpClipperSolver<4, 5 + kMaxOpAmpClipperDiodes, kMaxOpAmpClipperDiodes, 1>;

DiodeSpec diodeSpec() { return siliconSignalDiode(); }

}  // namespace

// ===========================================================================
// T021 part a (SC-004; FR-021) — saturation: driven far past the feedback-
// diode threshold, the clipper's settled DC output clamps near the diode
// forward drop (bounded) rather than tracking the input linearly. The linear
// prediction -Rf/Rin*vin grows without bound as |vin| grows; the actual
// output does not.
// ===========================================================================

TEST_CASE("OpAmpClipperSolver - clipper saturates near the diode drop under large drive (SC-004)") {
    const DiodeSpec d = diodeSpec();
    const double linearGainMag = kRf / kRin;  // magnitude of the undiode -Rf/Rin gain

    const double drives[] = {2.0, 5.0, 10.0, -5.0, -10.0};
    for (double vin : drives) {
        CAPTURE(vin);
        bool ok = true;
        ClipperSolver solver;
        const double vout = settleDc(
            solver,
            [&](double v) {
                return opAmpDiodeClipper(OpAmpDiodeClipperBom{kRin, kRf, kCf, d, 1, 1, v});
            },
            vin, kDt, 20000, ok);
        REQUIRE(ok);

        const double linearPrediction = -linearGainMag * vin;
        CHECK(std::fabs(vout) < 1.0);                    // bounded near the diode drop
        CHECK(std::fabs(vout / linearPrediction) < 0.2);  // NOT tracking the input linearly
    }
}

// ===========================================================================
// T021 part b (SC-004; FR-021) — symmetry: a SYMMETRIC feedback-diode
// population (nUp==nDown) yields an odd-symmetric transfer y(-x)=-y(x) (no DC
// offset); an ASYMMETRIC population (nUp!=nDown) yields a measurable DC
// offset — the two populations are distinguishable by the same measurement.
// ===========================================================================

TEST_CASE("OpAmpClipperSolver - symmetric population is odd-symmetric; asymmetric shows DC offset (SC-004)") {
    const DiodeSpec d = diodeSpec();

    double maxSymOffset = 0.0;
    double maxAsymOffset = 0.0;
    for (double x = 0.25; x <= 3.0 + 1e-9; x += 0.25) {
        CAPTURE(x);
        ClipperSolver symP, symN, asymP, asymN;

        bool okYP = true;
        const double yP = settleDc(
            symP, [&](double v) { return opAmpDiodeClipper(OpAmpDiodeClipperBom{kRin, kRf, kCf, d, 1, 1, v}); },
            x, kDt, 20000, okYP);
        bool okYN = true;
        const double yN = settleDc(
            symN, [&](double v) { return opAmpDiodeClipper(OpAmpDiodeClipperBom{kRin, kRf, kCf, d, 1, 1, v}); },
            -x, kDt, 20000, okYN);
        maxSymOffset = std::max(maxSymOffset, std::fabs(yP + yN));

        bool okZP = true;
        const double zP = settleDc(
            asymP, [&](double v) { return opAmpDiodeClipper(OpAmpDiodeClipperBom{kRin, kRf, kCf, d, 2, 1, v}); },
            x, kDt, 20000, okZP);
        bool okZN = true;
        const double zN = settleDc(
            asymN, [&](double v) { return opAmpDiodeClipper(OpAmpDiodeClipperBom{kRin, kRf, kCf, d, 2, 1, v}); },
            -x, kDt, 20000, okZN);
        maxAsymOffset = std::max(maxAsymOffset, std::fabs(zP + zN));

        REQUIRE((okYP && okYN && okZP && okZN));
    }

    CHECK(maxSymOffset < 1e-6);   // odd symmetry: y(-x) = -y(x)
    CHECK(maxAsymOffset > 1e-3);  // asymmetric: a measurable DC offset
}

// ===========================================================================
// T022 (SC-004; FR-021) — passivity of the passive feedback sub-network.
// Holding the op-amp's linear feedback network fixed (identical Rin/Rf/Cf/vin
// in both stages), the ONLY difference between activeFirstOrder (Rf || Cf, no
// diodes) and opAmpDiodeClipper (the SAME Rf || Cf plus the feedback diode
// string) is the added passive diode population. Diodes are passive,
// positive-conductance elements that shunt current away from Rf, so their
// presence can only REDUCE (never increase) the output signal's energy
// relative to the undiode reference — the op-amp's active gain (identical
// feedback R/C in both stages) is held fixed, isolating exactly the passive
// sub-network's own dissipation, per FR-021's "accounted separately" framing.
// ===========================================================================

TEST_CASE("OpAmpClipperSolver - passive feedback network dissipates: clipper energy <= undiode reference (SC-004)") {
    const DiodeSpec d = diodeSpec();
    bool ok = true;

    std::array<double, kWindow> clipIn{}, clipOut{}, refIn{}, refOut{};

    ClipperSolver clipSolver;
    driveSine(
        clipSolver, [&](double v) { return opAmpDiodeClipper(OpAmpDiodeClipperBom{kRin, kRf, kCf, d, 1, 1, v}); },
        2.0, 1000.0, kDt, kWarmup, clipIn, clipOut, ok);
    REQUIRE(ok);

    // The undiode reference is purely linear + reactive (Rf || Cf only): the
    // NullorSolver, no Newton iteration, so there is no convergence flag.
    NullorSolver<4, 5, 1> refSolver;
    refSolver.reset();
    const int total = kWarmup + kWindow;
    for (int step = 0; step < total; ++step) {
        const double vIn = 2.0 * std::sin(2.0 * kPi * 1000.0 * static_cast<double>(step) * kDt);
        const auto stage = activeFirstOrder(ActiveFirstOrderBom{kRin, kRf, kCf, vIn});
        refSolver.solve(stage.netlist, kDt);
        if (step >= kWarmup) {
            const std::size_t idx = static_cast<std::size_t>(step - kWarmup);
            refIn[idx] = vIn;
            refOut[idx] = refSolver.voltage(stage.outNode);
        }
    }

    const double eClip = energy(clipOut);
    const double eRef = energy(refOut);
    CHECK(eClip < eRef);  // the diode string dissipates: strictly less energy
                          // than the same feedback network without it
}

// ===========================================================================
// T023 (SC-005; FR-022) — the reactive signature: a 1 kHz sine driven into
// clipping at a fixed drive, solved at dt=1e-5 s (100 kHz — the OQ3-pinned
// parameters), the output spectral energy above a 5 kHz cutoff strictly
// decreases at each step of an ascending feedback-capacitor Cf sweep — the TS
// "soft" clipping band a static transfer curve cannot represent.
// ===========================================================================

TEST_CASE("OpAmpClipperSolver - post-clip HF energy strictly decreases across ascending Cf sweep (SC-005)") {
    const DiodeSpec d = diodeSpec();
    const double fs = 1.0 / kDt;
    const double drive = 2.0;  // fixed drive, well into clipping (see T022)

    const std::array<double, 6> cfSweep{1.0e-9, 2.2e-9, 4.7e-9, 10.0e-9, 22.0e-9, 47.0e-9};
    std::array<double, kWindow> in{}, out{};

    double prevHf = 1e300;
    bool strictlyDecreasing = true;
    for (double Cf : cfSweep) {
        CAPTURE(Cf);
        bool ok = true;
        ClipperSolver solver;
        driveSine(
            solver, [&](double v) { return opAmpDiodeClipper(OpAmpDiodeClipperBom{kRin, kRf, Cf, d, 1, 1, v}); },
            drive, 1000.0, kDt, kWarmup, in, out, ok);
        REQUIRE(ok);

        const double hf = hfEnergyAbove(out, fs, 5000.0);
        if (!(hf < prevHf)) {
            strictlyDecreasing = false;
        }
        prevHf = hf;
    }

    CHECK(strictlyDecreasing);
}
