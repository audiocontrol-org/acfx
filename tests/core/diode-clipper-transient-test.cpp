#include <doctest/doctest.h>

#include <array>
#include <cmath>
#include <variant>

#include "labs/component-abstractions/solver/newton-clipper.h"
#include "labs/diode-clippers/solver/transient-clipper.h"
#include "primitives/circuit/components.h"
#include "primitives/circuit/diode-clipper/clipper-config.h"
#include "primitives/circuit/diode-clipper/diode-clipper.h"
#include "primitives/circuit/models/diode.h"
#include "primitives/circuit/netlist.h"
#include "primitives/circuit/node.h"

// Tier-2 transient-solver suite for the diode-clippers lab (US2/US3, SC-002..006;
// contracts/transient-clipper.h). Proves the TransientClipper exact FIRST on
// closed-form sanity networks (a linear RC step response, then each clipper's
// DC-limit against an independent bisection oracle + the static NewtonClipper),
// then validates the bounded-scope and non-convergence contracts (US2) and the
// assembled-clipper behavioral invariants (US3).

using acfx::AsymmetricShuntValues;
using acfx::asymmetricShuntClipper;
using acfx::Capacitor;
using acfx::Diode;
using acfx::DiodeSpec;
using acfx::kGround;
using acfx::Netlist;
using acfx::NodeId;
using acfx::Resistor;
using acfx::seriesClipper;
using acfx::SeriesValues;
using acfx::siliconSignalDiode;
using acfx::SymmetricShuntValues;
using acfx::symmetricShuntClipper;
using acfx::VoltageSource;
using acfx::labs::component_abstractions::NewtonClipper;
using acfx::labs::diode_clippers::NewtonStatus;
using acfx::labs::diode_clippers::TransientClipper;

namespace {

// Well-damped standalone bisection root-finder over a monotonically
// non-decreasing f with a root in [lo, hi]. INDEPENDENT of the solver under
// test (the genuine cross-check, not solver-vs-itself).
template <typename F>
double bisectRoot(F f, double lo, double hi, double tol = 1e-13) {
    while (hi - lo > tol) {
        const double mid = 0.5 * (lo + hi);
        if (f(mid) <= 0.0) {
            lo = mid;
        } else {
            hi = mid;
        }
    }
    return 0.5 * (lo + hi);
}

// Symmetric-shunt DC oracle (data-model.md): 0 = v + R*2*Is*sinh(v/nVt) - Vin.
double oracleSymmetric(double Vin, double R, const DiodeSpec& d) {
    const double nVt = d.n * d.Vt;
    return bisectRoot(
        [&](double v) { return v + R * 2.0 * d.Is * std::sinh(v / nVt) - Vin; },
        -5.0, 5.0);
}

// Asymmetric-shunt DC oracle (data-model.md):
// 0 = v + R*[up*Is*(exp(v/nVt)-1) - down*Is*(exp(-v/nVt)-1)] - Vin.
double oracleAsymmetric(double Vin, double R, const DiodeSpec& d, int up, int down) {
    const double nVt = d.n * d.Vt;
    return bisectRoot(
        [&](double v) {
            const double iUp = up * d.Is * (std::exp(v / nVt) - 1.0);
            const double iDown = down * d.Is * (std::exp(-v / nVt) - 1.0);
            return v + R * (iUp - iDown) - Vin;
        },
        -5.0, 5.0);
}

DiodeSpec diodeSpec() { return siliconSignalDiode(); }

}  // namespace

// ===========================================================================
// T012 (SC-002 part 1) — the transient solver is proven exact on a LINEAR-ONLY
// RC network before any nonlinearity is trusted: it must reproduce the
// closed-form backward-Euler recurrence v[n] = α·Vin + (1−α)·v[n−1].
// ===========================================================================

TEST_CASE("TransientClipper - linear RC matches the analytic BE recurrence (~1e-9)") {
    Netlist<8, 8> nl;
    const NodeId n1 = nl.addNode();
    const NodeId n2 = nl.addNode();

    const double Vin = 5.0;
    const double R = 1000.0;
    const double C = 1.0e-6;
    const double dt = 1.0e-5;

    nl.add(VoltageSource{n1, kGround, Vin});
    nl.add(Resistor{n1, n2, R});
    nl.add(Capacitor{n2, kGround, C});
    nl.prepare();

    TransientClipper<8, 8> solver;
    solver.reset();  // explicit cold start (do not rely on construction-zeroes-history)

    const double alpha = dt / (dt + R * C);
    double vAnalytic = 0.0;
    double maxErr = 0.0;

    constexpr int kSteps = 2000;  // ~20 RC time constants
    for (int step = 0; step < kSteps; ++step) {
        const NewtonStatus s = solver.step(nl, dt);
        CHECK(s.converged);  // a diode-free linear solve always "converges"
        vAnalytic = alpha * Vin + (1.0 - alpha) * vAnalytic;
        maxErr = std::max(maxErr, std::fabs(solver.voltage(n2) - vAnalytic));
    }

    CHECK(maxErr < 1e-9);
    CHECK(std::fabs(solver.voltage(n2) - Vin) < 1e-6);  // DC steady state → Vin
}

// ===========================================================================
// T013 (SC-002 part 2) — each clipper's settled DC port voltage matches the
// INDEPENDENT bisection oracle to ~1e-6, and (where the static solver can
// represent the topology) the existing NewtonClipper curve.
// ===========================================================================

namespace {

// Settle a built clipper to DC under a constant input by stepping the transient
// solver to steady state, then return the port voltage. The measured answer is
// trustworthy only if the solve CONVERGED (a non-converged final iterate is not
// a physical answer, FR-011) — so assert the settled step converged rather than
// silently reading a stale iterate that could satisfy a loose invariant bound.
template <int N, int M, int D>
double settlePortDC(TransientClipper<N, M, D>& solver,
                    const Netlist<N, M>& nl, double dt, int steps) {
    solver.reset();
    NewtonStatus st;
    for (int i = 0; i < steps; ++i) {
        st = solver.step(nl, dt);
    }
    CHECK(st.converged);  // the measured steady state must be a converged solve
    return solver.clipperVoltage();
}

}  // namespace

TEST_CASE("TransientClipper - symmetric shunt DC matches oracle + NewtonClipper (~1e-6)") {
    const double R = 2200.0;
    const double Cf = 10.0e-9;
    const DiodeSpec d = diodeSpec();
    const double dt = 1.0e-5;
    constexpr int kSettle = 4000;

    TransientClipper<4, 8> solver;
    NewtonClipper<4, 8> staticClipper;  // static R+diode-pair reference (no cap)

    for (double Vin = -2.0; Vin <= 2.0 + 1e-9; Vin += 0.25) {
        const auto clip = symmetricShuntClipper(SymmetricShuntValues{R, Cf, d}, Vin);
        const double vDC = settlePortDC(solver, clip.netlist, dt, kSettle);

        const double vOracle = oracleSymmetric(Vin, R, d);
        CHECK(std::fabs(vDC - vOracle) < 1e-6);

        // Static NewtonClipper on the Cf-less R + antiparallel-pair network:
        // its DC solution must agree (the cap is open at DC).
        Netlist<4, 8> ref;
        const NodeId rn1 = ref.addNode();
        const NodeId rn2 = ref.addNode();
        ref.add(VoltageSource{rn1, kGround, Vin});
        ref.add(Resistor{rn1, rn2, R});
        ref.add(Diode{rn2, kGround, d.Is, d.n, d.Vt});
        ref.add(Diode{kGround, rn2, d.Is, d.n, d.Vt});
        ref.prepare();
        staticClipper.reset();
        const auto st = staticClipper.solve(ref, dt);  // component_abstractions status
        CHECK(st.converged);
        CHECK(std::fabs(vDC - staticClipper.clipperVoltage()) < 1e-6);
    }
}

TEST_CASE("TransientClipper - asymmetric shunt DC matches oracle, all 3 diodes present") {
    const double R = 2200.0;
    const double Cf = 10.0e-9;
    const DiodeSpec d = diodeSpec();
    const int up = 2, down = 1;
    const double dt = 1.0e-5;
    constexpr int kSettle = 4000;

    TransientClipper<4, 8> solver;

    for (double Vin = -2.0; Vin <= 2.0 + 1e-9; Vin += 0.25) {
        const auto clip =
            asymmetricShuntClipper(AsymmetricShuntValues{R, Cf, d, up, down}, Vin);
        // All up+down diodes are present in the netlist (never dropped, FR-012).
        int diodes = 0;
        for (const auto& c : clip.netlist.components()) {
            if (std::holds_alternative<Diode>(c)) {
                ++diodes;
            }
        }
        CHECK(diodes == up + down);

        const double vDC = settlePortDC(solver, clip.netlist, dt, kSettle);
        const double vOracle = oracleAsymmetric(Vin, R, d, up, down);
        CHECK(std::fabs(vDC - vOracle) < 1e-6);
    }
}

TEST_CASE("TransientClipper - series clipper coupling cap blocks DC (output → 0)") {
    const DiodeSpec d = diodeSpec();
    const double dt = 1.0e-5;
    // Cc large + R modest so the DC transient settles within the step budget.
    const auto clip = seriesClipper(SeriesValues{1.0e-6, 4700.0, d, 1}, 1.5);

    TransientClipper<5, 8> solver;
    for (int i = 0; i < 200000; ++i) {
        solver.step(clip.netlist, dt);
    }
    // The coupling cap blocks DC: the output node settles toward 0 V.
    CHECK(std::fabs(solver.voltage(clip.outNode)) < 1e-3);
}

// ===========================================================================
// T014 (SC — bounds, FR-012) — the bounded single-port scope: a diode string up
// to MaxDiodes solves with all diodes present; a second nonlinearity at a
// DISTINCT node pair is refused with a descriptive error.
// ===========================================================================

TEST_CASE("TransientClipper - second nonlinearity at a distinct node pair is refused") {
    const DiodeSpec d = diodeSpec();
    const double dt = 1.0e-5;

    // A network with two diodes on DIFFERENT node pairs (n1-gnd and n2-gnd):
    // two interacting nonlinearity locations — out of the bounded scope.
    Netlist<5, 8> nl;
    const NodeId n1 = nl.addNode();
    const NodeId n2 = nl.addNode();
    nl.add(VoltageSource{n1, kGround, 1.0});
    nl.add(Resistor{n1, n2, 1000.0});
    nl.add(Diode{n1, kGround, d.Is, d.n, d.Vt});
    nl.add(Diode{n2, kGround, d.Is, d.n, d.Vt});
    nl.prepare();

    TransientClipper<5, 8> solver;
    CHECK_THROWS_AS(solver.step(nl, dt), std::runtime_error);
}

TEST_CASE("TransientClipper - reactive-heavy netlist beyond augmented capacity throws") {
    // The augmented capacity (MaxComponents + 2*MaxDiodes) is sized for the
    // diode-clipper builder outputs (one reactive cluster + <= MaxDiodes port
    // diodes). A GENERIC reactive-heavy netlist expands past it (each reactive
    // element and diode → a 2-component companion): here MaxComponents=12,
    // MaxDiodes=2 (capacity 16), driven with 10 shunt capacitors → projected
    // augmented = 12 + 10 + 0 = 22 > 16. The solver must fail LOUD and
    // descriptively at the capacity guard, not overflow silently.
    Netlist<8, 12> nl;
    const NodeId n1 = nl.addNode();
    const NodeId n2 = nl.addNode();
    nl.add(VoltageSource{n1, kGround, 1.0});
    nl.add(Resistor{n1, n2, 1000.0});
    for (int i = 0; i < 10; ++i) {
        nl.add(Capacitor{n2, kGround, 1.0e-9});
    }
    nl.prepare();  // conductive (caps + R to ground) — a valid netlist

    TransientClipper<8, 12, 2> solver;
    CHECK_THROWS_AS(solver.step(nl, 1.0e-5), std::runtime_error);
}

TEST_CASE("TransientClipper - dt <= 0 throws std::invalid_argument") {
    const auto clip = symmetricShuntClipper(
        SymmetricShuntValues{2200.0, 10.0e-9, diodeSpec()}, 1.0);
    TransientClipper<4, 8> solver;
    CHECK_THROWS_AS(solver.step(clip.netlist, 0.0), std::invalid_argument);
    CHECK_THROWS_AS(solver.step(clip.netlist, -1.0e-5), std::invalid_argument);
}

TEST_CASE("TransientClipper - invalid constructor config throws") {
    CHECK_THROWS_AS((TransientClipper<4, 8>(0)), std::invalid_argument);
    CHECK_THROWS_AS((TransientClipper<4, 8>(50, 0.0)), std::invalid_argument);
    CHECK_THROWS_AS((TransientClipper<4, 8>(50, 1e-9, -1.0)), std::invalid_argument);
}

// ===========================================================================
// T015 (SC-006) — non-convergence is SURFACED, never faked: a starved iteration
// budget against a stiff excitation yields converged == false with the residual.
// ===========================================================================

TEST_CASE("TransientClipper - starved iteration budget surfaces non-convergence") {
    const double R = 2200.0;
    const double Cf = 10.0e-9;
    const DiodeSpec d = diodeSpec();
    const double dt = 1.0e-5;

    // A single-iteration budget against a hard cold-start step into deep
    // clipping cannot settle |Δv| < voltageTol in one step.
    TransientClipper<4, 8> starved(/*maxIterations=*/1);
    starved.reset();  // explicit cold start (do not rely on construction-zeroes-history)
    const auto clip = symmetricShuntClipper(SymmetricShuntValues{R, Cf, d}, 5.0);

    const NewtonStatus s = starved.step(clip.netlist, dt);
    CHECK_FALSE(s.converged);
    CHECK(s.iterations == 1);
    CHECK(s.voltageResidual > starved.voltageTolerance());  // residual is surfaced
}

// ===========================================================================
// US3 assembled-clipper invariants (SC-003/004/005). The solver is proven exact
// above, so each clipper is validated by the behavioral invariants its topology
// dictates.
// ===========================================================================

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kDt = 1.0e-5;     // 100 kHz (FR-017 lab convention)
constexpr int kWindow = 1000;      // 10 periods of 1 kHz — harmonics land on bins
constexpr int kWarmup = 3000;      // 30 ms → steady state before measuring

// Drive a periodic sine into a clipper (rebuilt per sample with the new input —
// the sanctioned control-rate-rebuild drive) and collect a steady-state window
// of input and output samples. `build(vIn)` returns the clipper for input vIn.
template <int N, int M, int D, typename BuildFn>
void driveSine(TransientClipper<N, M, D>& solver, BuildFn build, double amp,
               double freqHz, std::array<double, kWindow>& in,
               std::array<double, kWindow>& out) {
    solver.reset();
    const int total = kWarmup + kWindow;
    bool measuredAllConverged = true;
    for (int nStep = 0; nStep < total; ++nStep) {
        const double vIn = amp * std::sin(2.0 * kPi * freqHz * nStep * kDt);
        const auto clip = build(vIn);
        const NewtonStatus st = solver.step(clip.netlist, kDt);
        if (nStep >= kWarmup) {
            if (!st.converged) {
                measuredAllConverged = false;
            }
            in[static_cast<std::size_t>(nStep - kWarmup)] = vIn;
            out[static_cast<std::size_t>(nStep - kWarmup)] = solver.voltage(clip.outNode);
        }
    }
    // Every sample in the measured window must be a converged solve — otherwise
    // the invariants below would be measuring stale, non-physical iterates that
    // could slide under their (deliberately loose) bounds (FR-011).
    CHECK(measuredAllConverged);
}

// Sum of squares (energy proxy) over a window.
double energy(const std::array<double, kWindow>& x) {
    double e = 0.0;
    for (double v : x) {
        e += v * v;
    }
    return e;
}

// Direct-DFT spectral energy strictly above `cutoff` (Hz) over a window sampled
// at fs. O(N * bins); fine for a host test. The measurement window is an integer
// number of the drive period, so the transform is leakage-clean.
double hfEnergyAbove(const std::array<double, kWindow>& x, double fs, double cutoff) {
    double total = 0.0;
    for (int k = 1; k <= kWindow / 2; ++k) {
        const double fk = static_cast<double>(k) * fs / kWindow;
        if (fk <= cutoff) {
            continue;
        }
        double re = 0.0, im = 0.0;
        for (int nn = 0; nn < kWindow; ++nn) {
            const double ph = -2.0 * kPi * k * nn / kWindow;
            re += x[static_cast<std::size_t>(nn)] * std::cos(ph);
            im += x[static_cast<std::size_t>(nn)] * std::sin(ph);
        }
        total += re * re + im * im;
    }
    return total;
}

}  // namespace

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
