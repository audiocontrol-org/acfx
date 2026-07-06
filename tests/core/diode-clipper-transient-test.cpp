#include <doctest/doctest.h>

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
// solver to steady state, then return the port voltage.
template <int N, int M, int D>
double settlePortDC(TransientClipper<N, M, D>& solver,
                    const Netlist<N, M>& nl, double dt, int steps) {
    solver.reset();
    for (int i = 0; i < steps; ++i) {
        solver.step(nl, dt);
    }
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
    const auto clip = symmetricShuntClipper(SymmetricShuntValues{R, Cf, d}, 5.0);

    const NewtonStatus s = starved.step(clip.netlist, dt);
    CHECK_FALSE(s.converged);
    CHECK(s.iterations == 1);
    CHECK(s.voltageResidual > starved.voltageTolerance());  // residual is surfaced
}
