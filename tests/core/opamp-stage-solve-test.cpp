#include <doctest/doctest.h>

#include <cmath>
#include <variant>

#include "labs/opamp-stages/solver/opamp-clipper-solver.h"
#include "labs/opamp-stages/solver/opamp-stage-solver.h"
#include "primitives/circuit/components.h"
#include "primitives/circuit/models/diode.h"
#include "primitives/circuit/models/opamp.h"
#include "primitives/circuit/models/sources.h"
#include "primitives/circuit/netlist.h"
#include "primitives/circuit/node.h"
#include "primitives/circuit/opamp-stage/opamp-config.h"
#include "primitives/circuit/opamp-stage/opamp-stage.h"

// Tier-2 solver suite for the opamp-stage primitive (lab nullor MNA
// augmentation; US2, contracts/opamp-stage-solver.md; FR-011..FR-018;
// SC-002/SC-006/SC-007). Proves the NullorSolver exact FIRST on the linear
// closed-form stages (T016 analytic gains, T017 first-order response) before
// trusting the bounded charter (T019 tripwires) and the non-convergence
// contract (T020). The independent DC-limit bisection oracle for the
// op-amp+diode clipper (T018) is appended by a later task; see the marker at
// the end of this file.

using acfx::CurrentSource;
using acfx::Diode;
using acfx::DiodeSpec;
using acfx::Netlist;
using acfx::NodeId;
using acfx::OpAmp;
using acfx::Resistor;
using acfx::VoltageSource;
using acfx::kGround;

using acfx::ActiveFirstOrderBom;
using acfx::InvertingGainBom;
using acfx::NonInvertingGainBom;
using acfx::OpAmpDiodeClipperBom;
using acfx::activeFirstOrder;
using acfx::invertingGain;
using acfx::kMaxOpAmpClipperDiodes;
using acfx::nonInvertingGain;
using acfx::opAmpDiodeClipper;
using acfx::siliconSignalDiode;

using acfx::labs::opamp_stages::NewtonStatus;
using acfx::labs::opamp_stages::NullorSolver;
using acfx::labs::opamp_stages::OpAmpClipperSolver;

// ===========================================================================
// T016 (SC-002 rung a) — the nullor stamp is EXACT: the non-inverting and
// inverting resistive stages reproduce their analytic closed-loop gains
// (1 + Rf/Rg, -Rf/Rin) to ~1e-9 across representative bills of materials.
// ===========================================================================

TEST_CASE("NullorSolver - non-inverting gain matches 1+Rf/Rg to ~1e-9") {
    const double dt = 1.0e-5;  // any dt>0 is valid; the stage is purely resistive

    const struct { double Rf, Rg, vin; } boms[] = {
        {10000.0, 1000.0, 1.0},
        {47000.0, 4700.0, -0.5},
        {2200.0, 2200.0, 0.25},   // unity-ish, gain 2
        {100000.0, 1000.0, 0.1},  // large gain
    };

    for (const auto& b : boms) {
        const auto stage = nonInvertingGain(NonInvertingGainBom{b.Rf, b.Rg, b.vin});
        NullorSolver<4, 4, 1> solver;
        solver.reset();
        solver.solve(stage.netlist, dt);

        const double vin = solver.voltage(stage.inNode);
        const double vout = solver.voltage(stage.outNode);
        const double analyticGain = 1.0 + b.Rf / b.Rg;

        CHECK(std::fabs(vin - b.vin) < 1e-12);  // ideal source pins the input exactly
        CHECK(std::fabs(vout / vin - analyticGain) < 1e-9);
        CHECK(std::fabs(vout - analyticGain * b.vin) < 1e-9);
    }
}

TEST_CASE("NullorSolver - inverting gain matches -Rf/Rin to ~1e-9") {
    const double dt = 1.0e-5;

    const struct { double Rin, Rf, vin; } boms[] = {
        {10000.0, 47000.0, 1.0},
        {1000.0, 10000.0, -2.0},
        {2200.0, 2200.0, 0.5},   // unity inverting
        {4700.0, 100000.0, 0.05},
    };

    for (const auto& b : boms) {
        const auto stage = invertingGain(InvertingGainBom{b.Rin, b.Rf, b.vin});
        NullorSolver<4, 4, 1> solver;
        solver.reset();
        solver.solve(stage.netlist, dt);

        const double vin = solver.voltage(stage.inNode);
        const double vout = solver.voltage(stage.outNode);
        const double analyticGain = -b.Rf / b.Rin;

        CHECK(std::fabs(vin - b.vin) < 1e-12);
        CHECK(std::fabs(vout / vin - analyticGain) < 1e-9);
        CHECK(std::fabs(vout - analyticGain * b.vin) < 1e-9);
    }
}

// ===========================================================================
// T017 (SC-002 rung b) — the active first-order (inverting low-pass) stage:
// nullor + reactive companion together reproduce the analytic backward-Euler
// recurrence EXACTLY (~1e-9) at every sample, and the DC steady state matches
// -Rf/Rin.
//
// Derivation: the virtual short pins V(inMinus) = V(inPlus) = 0. KCL at
// inMinus (zero input current into the nullor) over the Rin/Rf/Cf branches:
//   Vin/Rin + Vout/Rf + Cf*dVout/dt = 0
// Backward-Euler discretization of dVout/dt = (Vout[n]-Vout[n-1])/dt gives
//   Vout[n] = (Vout[n-1] - dt*Vin/(Rin*Cf)) / (1 + dt/(Rf*Cf))
// whose steady state (dVout/dt -> 0) is Vout_ss = -(Rf/Rin)*Vin.
// ===========================================================================

TEST_CASE("NullorSolver - active first-order matches analytic BE recurrence (~1e-9)") {
    const double Rin = 10000.0;
    const double Rf = 47000.0;
    const double Cf = 10.0e-9;
    const double vin = 1.0;
    const double dt = 1.0e-5;

    const auto stage = activeFirstOrder(ActiveFirstOrderBom{Rin, Rf, Cf, vin});
    NullorSolver<4, 5, 1> solver;
    solver.reset();

    const double alpha = dt / (Rf * Cf);
    const double drive = dt * vin / (Rin * Cf);

    double vAnalytic = 0.0;
    double maxErr = 0.0;

    constexpr int kSteps = 2000;  // ~42 time constants (Rf*Cf / dt ~= 47) — full settle
    for (int step = 0; step < kSteps; ++step) {
        solver.solve(stage.netlist, dt);
        vAnalytic = (vAnalytic - drive) / (1.0 + alpha);
        maxErr = std::max(maxErr, std::fabs(solver.voltage(stage.outNode) - vAnalytic));
    }

    CHECK(maxErr < 1e-9);

    // DC steady-state gain: -Rf/Rin.
    const double dcGain = -Rf / Rin;
    CHECK(std::fabs(solver.voltage(stage.outNode) - dcGain * vin) < 1e-6);
}

// ===========================================================================
// T019 (FR-015 / SC-007) — the bounded charter's three checkable tripwires,
// each a descriptive throw (never a silent wrong answer or a generalized
// solve):
//   (i)   a non-OpAmp element needing branch-current augmentation — the
//         canonical trigger is a FLOATING (non-grounded) ideal VoltageSource
//         (NullorSolver::imposeVoltageSources).
//   (ii)  >=2 interacting nonlinearities at distinct node pairs — enforced by
//         OpAmpClipperSolver::collectPort's orientation scan.
//   (iii) an op-amp population beyond the instantiated MaxOpAmps capacity —
//         NullorSolver::countOpAmps.
// ===========================================================================

TEST_CASE("NullorSolver - floating (non-grounded) VoltageSource throws (tripwire i)") {
    // Two non-ground nodes, each individually grounded through a resistor (so
    // prepare()'s floating-node pre-filter passes), bridged by a FLOATING
    // ideal voltage source — the "becoming general MNA" signal the bounded
    // nullor-only solver refuses.
    Netlist<3, 3> nl;
    const NodeId n1 = nl.addNode();
    const NodeId n2 = nl.addNode();
    nl.add(Resistor{n1, kGround, 1000.0});
    nl.add(Resistor{n2, kGround, 1000.0});
    nl.add(VoltageSource{n1, n2, 5.0});  // floating: neither terminal grounded
    nl.prepare();

    NullorSolver<3, 3, 1> solver;
    CHECK_THROWS_AS(solver.solve(nl, 1.0e-5), std::runtime_error);
}

TEST_CASE("OpAmpClipperSolver - a second nonlinearity at a distinct node pair throws (tripwire ii)") {
    const DiodeSpec d = siliconSignalDiode();

    // The op-amp-clipper shape (Vin -> Rin -> inMinus; OpAmp; Rf feedback;
    // ONE feedback diode at (out, inMinus)) plus a SECOND, unrelated diode at
    // a distinct node pair (vinNode, ground) — two interacting nonlinearity
    // locations, out of the bounded single-port scope.
    Netlist<4, 6> nl;
    const NodeId vinNode = nl.addNode();
    const NodeId inMinus = nl.addNode();
    const NodeId out = nl.addNode();
    nl.add(VoltageSource{vinNode, kGround, 1.0});
    nl.add(Resistor{vinNode, inMinus, 10000.0});
    nl.add(OpAmp{kGround, inMinus, out});
    nl.add(Resistor{out, inMinus, 47000.0});
    nl.add(Diode{out, inMinus, d.Is, d.n, d.Vt});      // the clipper's own port
    nl.add(Diode{vinNode, kGround, d.Is, d.n, d.Vt});  // a distinct nonlinearity location
    nl.prepare();

    OpAmpClipperSolver<4, 6, 4, 1> clipper;
    CHECK_THROWS_AS(clipper.step(nl, 1.0e-5), std::runtime_error);
}

TEST_CASE("NullorSolver - op-amp population beyond MaxOpAmps throws (tripwire iii)") {
    // Two independent, well-posed op-amp stages (each op-amp's output has its
    // own conductive feedback path, so prepare() passes) solved by a solver
    // instantiated for only ONE op-amp — the fixed bordered-dimension ceiling
    // (no dynamic growth) is exceeded.
    Netlist<5, 6> nl;
    const NodeId a = nl.addNode();  // inMinus of opamp 1
    const NodeId b = nl.addNode();  // out of opamp 1
    const NodeId c = nl.addNode();  // inMinus of opamp 2
    const NodeId e = nl.addNode();  // out of opamp 2
    nl.add(Resistor{a, kGround, 1000.0});
    nl.add(OpAmp{kGround, a, b});
    nl.add(Resistor{b, a, 1000.0});
    nl.add(OpAmp{kGround, c, e});
    nl.add(Resistor{c, kGround, 1000.0});
    nl.add(Resistor{e, c, 1000.0});
    nl.prepare();

    NullorSolver<5, 6, 1> solver;  // MaxOpAmps = 1, but nl carries 2 op-amps
    CHECK_THROWS_AS(solver.solve(nl, 1.0e-5), std::runtime_error);
}

// ===========================================================================
// T020 (SC-006) — non-convergence is SURFACED, never faked: a starved
// iteration budget against a stiff (deep-clipping) excitation returns
// converged == false with a residual above the tolerance, and a
// normally-budgeted solve on the same stage converges — the contrast that
// proves the starved case is a budget failure, not a broken solver.
// ===========================================================================

TEST_CASE("OpAmpClipperSolver - starved iteration budget surfaces non-convergence") {
    const double Rin = 10000.0;
    const double Rf = 47000.0;
    const double Cf = 100.0e-9;
    const DiodeSpec d = siliconSignalDiode();
    const double dt = 1.0e-5;

    // A large drive (5 V into a stage whose diode drop is a few hundred mV)
    // from a cold start (warm-start voltage 0) is a stiff single-step jump
    // into deep clipping.
    const auto clip =
        opAmpDiodeClipper(OpAmpDiodeClipperBom{Rin, Rf, Cf, d, /*nUp=*/1, /*nDown=*/1, /*vin=*/5.0});

    OpAmpClipperSolver<4, 5 + kMaxOpAmpClipperDiodes, kMaxOpAmpClipperDiodes, 1> starved(
        /*maxIterations=*/1);
    starved.reset();

    const NewtonStatus starvedStatus = starved.step(clip.netlist, dt);
    CHECK_FALSE(starvedStatus.converged);
    CHECK(starvedStatus.iterations == 1);
    CHECK(starvedStatus.voltageResidual > starved.voltageTolerance());

    // Contrast: the same stage, well-budgeted (defaults), converges cleanly.
    OpAmpClipperSolver<4, 5 + kMaxOpAmpClipperDiodes, kMaxOpAmpClipperDiodes, 1> normal;
    normal.reset();
    const NewtonStatus normalStatus = normal.step(clip.netlist, dt);
    CHECK(normalStatus.converged);
    CHECK(normalStatus.voltageResidual < normal.voltageTolerance());
}

// --- DC-limit bisection oracle test (T018) appended below ---

// ===========================================================================
// T018 (SC-002 rung c) — an INDEPENDENT DC-limit bisection oracle cross-checks
// the OpAmpClipperSolver's settled steady-state output. The oracle never calls
// the solver: it is derived straight from the Shockley diode law + KCL at the
// op-amp's virtual-ground summing node and solved by its own bisection root
// find. Agreeing to ~1e-6 across symmetric/asymmetric diode populations and a
// span of drives (both polarities, into deep clipping) is what makes the
// solver's diode-clipper answer trustworthy rather than merely self-consistent.
//
// PHYSICS OF THE ORACLE. The ideal op-amp (inPlus grounded) holds the inverting
// input inMinus at virtual ground (0 V) and draws no input current, so ALL of
// the input current returns through the feedback network. At DC the feedback
// capacitor Cf is an open circuit and drops out. KCL at inMinus, taking every
// current as flowing FROM its source node INTO inMinus:
//
//     vin/Rin  +  Ifeedback(Vout)  =  0
//
// where Ifeedback is the total current from the output node (at Vout) back to
// the virtual ground through the parallel feedback legs Rf and the antiparallel
// diode string (nUp forward diodes {out->inMinus}, nDown reverse {inMinus->out}
// — each obeying the Shockley law I = Is*(exp(vAK/(n*Vt)) - 1)):
//
//     Ifeedback(Vout) = Vout/Rf
//                     + nUp   * Is*(exp( Vout/(n*Vt)) - 1)   // forward diodes, vAK = +Vout
//                     - nDown * Is*(exp(-Vout/(n*Vt)) - 1)   // reverse diodes, vAK = -Vout
//
// f(Vout) = vin/Rin + Ifeedback(Vout) is strictly increasing in Vout (Rf > 0
// and every diode conductance is positive), so it has a unique root that a
// sign-bracketed bisection converges to unconditionally — no derivative, no
// Newton, nothing shared with the solver under test.
namespace opamp_dc_oracle {

// The scalar DC problem the oracle solves — deliberately a flat value struct so
// the oracle math below reads as literal physics, independent of any builder or
// solver type.
struct ClipperDc {
    double Rin;   // input resistor (ohm)
    double Rf;    // feedback resistor (ohm)
    double vin;   // DC drive amplitude (V)
    double Is;    // diode reverse saturation current (A)
    double n;     // diode ideality factor
    double Vt;    // thermal voltage (V)
    int nUp;      // forward feedback diodes {out -> inMinus}
    int nDown;    // reverse feedback diodes {inMinus -> out}
};

// Total feedback current from the output node (at vout) back to virtual ground,
// straight from the Shockley law — the independent restatement of the physics
// the solver stamps as Norton companions. Never calls the solver.
inline double feedbackCurrent(const ClipperDc& p, double vout) {
    const double vte = p.n * p.Vt;  // junction scaling voltage n*Vt
    const double resistive = vout / p.Rf;
    const double forward = p.nUp * p.Is * (std::exp(vout / vte) - 1.0);
    const double reverse = p.nDown * p.Is * (std::exp(-vout / vte) - 1.0);
    return resistive + forward - reverse;
}

// The DC KCL residual f(vout) = vin/Rin + Ifeedback(vout). Its unique root is
// the settled output voltage.
inline double residual(const ClipperDc& p, double vout) {
    return p.vin / p.Rin + feedbackCurrent(p, vout);
}

// Independent bisection root find. f is strictly increasing, so a bracket wide
// enough to sign-straddle the root (physical outputs sit well inside +/-1 V for
// silicon; +/-10 V keeps every exp argument finite in double — exp(10/0.02585)
// = exp(~387) is far below the exp(~709) overflow edge) always contains it.
inline double solve(const ClipperDc& p) {
    double lo = -10.0;
    double hi = 10.0;
    const double flo = residual(p, lo);
    const double fhi = residual(p, hi);
    REQUIRE(flo < 0.0);  // monotone increasing: f(lo) < 0 < f(hi) brackets the root
    REQUIRE(fhi > 0.0);
    // ~50 halvings drives the bracket from 20 V to ~2e-14 V — far tighter than
    // the 1e-6 cross-check tolerance and below the solver's 1e-9 voltage gate.
    for (int i = 0; i < 200; ++i) {
        const double mid = 0.5 * (lo + hi);
        if (hi - lo < 1e-13) {
            return mid;
        }
        if (residual(p, mid) > 0.0) {
            hi = mid;  // root is left of mid
        } else {
            lo = mid;  // root is right of mid
        }
    }
    return 0.5 * (lo + hi);
}

}  // namespace opamp_dc_oracle

TEST_CASE("OpAmpClipperSolver - settled DC output matches independent bisection oracle (~1e-6)") {
    using Solver =
        OpAmpClipperSolver<4, 5 + kMaxOpAmpClipperDiodes, kMaxOpAmpClipperDiodes, 1>;

    const DiodeSpec d = siliconSignalDiode();  // Is = 1e-14, n = 1, Vt = 25.85 mV
    const double dt = 1.0e-5;

    // A small feedback cap keeps the settling time constant short (Rf*Cf) so a
    // bounded step budget reaches the DC limit; its VALUE is irrelevant to the
    // steady state (Cf is open at DC) — only the fixed point matters here.
    const double Cf = 1.0e-9;

    // Symmetric and asymmetric diode populations across a span of drives: both
    // polarities, from sub-threshold (0.05 V, barely clipping) through the knee
    // to deep clipping (up to 10 V, hundreds of diode-drops of overdrive).
    const struct {
        double Rin, Rf, vin;
        int nUp, nDown;
    } cases[] = {
        // Symmetric 1:1 — the canonical TS808 pair, both polarities, into deep clip.
        {10000.0, 47000.0, 0.05, 1, 1},
        {10000.0, 47000.0, -0.05, 1, 1},
        {10000.0, 47000.0, 0.5, 1, 1},
        {10000.0, 47000.0, -0.5, 1, 1},
        {10000.0, 47000.0, 2.0, 1, 1},
        {10000.0, 47000.0, -2.0, 1, 1},
        {10000.0, 47000.0, 10.0, 1, 1},
        {10000.0, 47000.0, -10.0, 1, 1},
        // Symmetric 2:2 — a lower clamp, still balanced.
        {4700.0, 100000.0, 3.0, 2, 2},
        {4700.0, 100000.0, -3.0, 2, 2},
        // Asymmetric 2:1 — DC-offset / even-harmonic populations, both polarities.
        {10000.0, 47000.0, 1.0, 2, 1},
        {10000.0, 47000.0, -1.0, 2, 1},
        {10000.0, 47000.0, 5.0, 2, 1},
        {10000.0, 47000.0, -5.0, 2, 1},
        // Asymmetric 1:3 — the opposite skew.
        {10000.0, 47000.0, 4.0, 1, 3},
        {10000.0, 47000.0, -4.0, 1, 3},
    };

    double maxErr = 0.0;

    for (const auto& c : cases) {
        CAPTURE(c.vin);
        CAPTURE(c.nUp);
        CAPTURE(c.nDown);

        const auto clip = opAmpDiodeClipper(
            OpAmpDiodeClipperBom{c.Rin, c.Rf, Cf, d, c.nUp, c.nDown, c.vin});

        // Drive the solver to DC steady state: step until the output stops
        // moving (successive settled outputs agree to 1e-13) or the budget is
        // spent. Every step must converge — a non-converged step would make the
        // "settled" reading meaningless.
        Solver solver;
        solver.reset();

        double vout = 0.0;
        double prev = 0.0;
        bool settled = false;
        constexpr int kMaxSteps = 20000;
        for (int step = 0; step < kMaxSteps; ++step) {
            const NewtonStatus status = solver.step(clip.netlist, dt);
            REQUIRE(status.converged);
            vout = solver.voltage(clip.outNode);
            if (step > 0 && std::fabs(vout - prev) < 1e-13) {
                settled = true;
                break;
            }
            prev = vout;
        }
        REQUIRE(settled);  // the DC limit was actually reached, not truncated

        // Independent oracle for the SAME bill of materials — Shockley + KCL,
        // solved by bisection, no solver involvement.
        const opamp_dc_oracle::ClipperDc dc{c.Rin, c.Rf, c.vin, d.Is, d.n, d.Vt,
                                            c.nUp, c.nDown};
        const double oracleVout = opamp_dc_oracle::solve(dc);

        const double err = std::fabs(vout - oracleVout);
        maxErr = std::max(maxErr, err);
        CHECK(err < 1e-6);
    }

    // A single guard on the worst case across the whole sweep, so a regression
    // anywhere in the table trips this even if the per-case CHECKs were relaxed.
    CHECK(maxErr < 1e-6);
}
