// opamp-stages-harness.cpp
// Host-only lab harness: never included by portable code.
//
// Assembled-op-amp-stage validation for the bounded NULLOR MNA-augmentation
// solver (T024; US2/US3; contracts/opamp-stage-solver.md; FR-023). Mirrors
// (and stays behavior-identical to) the doctest assertions in
// tests/core/opamp-stage-solve-test.cpp and tests/core/opamp-stage-invariants-
// test.cpp; this file exists so the same validation is runnable/readable
// outside the test framework as a small educational CI check (exits nonzero
// if anything fails). The sine-drive / DC-settle / energy measurement rig is
// SHARED with the doctest suite via tests/core/opamp-stage-test-support.h
// (that header is deliberately doctest-free, so a plain int main() can
// include it without linking a test framework — see the CMakeLists.txt
// target_include_directories comment on acfx_lab_opamp_stages_harness).
//
// Validates, per quickstart.md's nine checks, in order: (1) the analytic
// gains 1+Rf/Rg / -Rf/Rin (the nullor stamp is exact); (2) the active
// first-order stage's analytic backward-Euler recurrence (nullor+reactive
// exact before any nonlinearity); (3) the clipper's DC limit against an
// INDEPENDENT bisection oracle; (4) forward saturation; (5) symmetry /
// DC-offset; (6) passivity of the passive feedback sub-network; (7) the
// reactive signature (larger Cf -> less post-clip HF); (8) an explicit
// non-convergence check (a starved budget surfaces converged == false); and
// (9) the bounded charter's tripwires (>=2 nonlinearities refuse; the
// augmentation touches only OpAmp branches).
//
// Include roots: core/ (acfx_core) for the primitive/lab headers, and tests/
// for the shared measurement rig (see CMakeLists.txt). Compile with -std=c++20.

#include "core/opamp-stage-test-support.h"

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

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <stdexcept>

using acfx::ActiveFirstOrderBom;
using acfx::Diode;
using acfx::DiodeSpec;
using acfx::InvertingGainBom;
using acfx::kGround;
using acfx::kMaxOpAmpClipperDiodes;
using acfx::Netlist;
using acfx::NodeId;
using acfx::NonInvertingGainBom;
using acfx::OpAmp;
using acfx::OpAmpDiodeClipperBom;
using acfx::Resistor;
using acfx::siliconSignalDiode;
using acfx::VoltageSource;
using acfx::activeFirstOrder;
using acfx::invertingGain;
using acfx::nonInvertingGain;
using acfx::opAmpDiodeClipper;

using acfx::labs::opamp_stages::NewtonStatus;
using acfx::labs::opamp_stages::NullorSolver;
using acfx::labs::opamp_stages::OpAmpClipperSolver;

using opst_test::driveSine;
using opst_test::energy;
using opst_test::hfEnergyAbove;
using opst_test::kPi;
using opst_test::settleDc;

namespace {

constexpr double kRin = 10000.0;
constexpr double kRf = 47000.0;
constexpr double kCf = 10.0e-9;
constexpr double kDt = 1.0e-5;  // 100 kHz (FR-022's pinned lab convention)
constexpr int kWarmup = 3000;   // 30 ms -> steady state before measuring
constexpr int kWindow = 1000;   // 10 periods of a 1 kHz drive

using ClipperSolver =
    OpAmpClipperSolver<4, 5 + kMaxOpAmpClipperDiodes, kMaxOpAmpClipperDiodes, 1>;

void report(bool ok, const char* label, double measured, double expected, bool& allPassed) {
    std::printf("[%s] %-64s measured=% .12g  expected=% .12g\n",
                ok ? "PASS" : "FAIL", label, measured, expected);
    if (!ok) {
        allPassed = false;
    }
}

DiodeSpec diodeSpec() { return siliconSignalDiode(); }

// Independent DC-limit bisection oracle (NOT the solver under test) — a
// harness-local restatement of the physics in
// tests/core/opamp-stage-solve-test.cpp's opamp_dc_oracle (T018): Shockley +
// KCL at the op-amp's virtual-ground summing node, solved by its own
// bisection root-find, sharing nothing with the solver.
struct ClipperDc {
    double Rin, Rf, vin, Is, n, Vt;
    int nUp, nDown;
};

double feedbackCurrent(const ClipperDc& p, double vout) {
    const double vte = p.n * p.Vt;
    const double resistive = vout / p.Rf;
    const double forward = p.nUp * p.Is * (std::exp(vout / vte) - 1.0);
    const double reverse = p.nDown * p.Is * (std::exp(-vout / vte) - 1.0);
    return resistive + forward - reverse;
}

double dcOracle(const ClipperDc& p) {
    double lo = -10.0;
    double hi = 10.0;
    for (int i = 0; i < 200; ++i) {
        if (hi - lo < 1e-13) {
            break;
        }
        const double mid = 0.5 * (lo + hi);
        const double f = p.vin / p.Rin + feedbackCurrent(p, mid);
        if (f > 0.0) {
            hi = mid;
        } else {
            lo = mid;
        }
    }
    return 0.5 * (lo + hi);
}

// --- 1. Analytic gains (SC-002 rung a) -------------------------------------
bool runAnalyticGains() {
    std::puts("-- Analytic gains: 1+Rf/Rg (non-inverting), -Rf/Rin (inverting) --");
    bool ok = true;

    const double Rf1 = 10000.0, Rg1 = 1000.0, vin1 = 1.0;
    const auto ni = nonInvertingGain(NonInvertingGainBom{Rf1, Rg1, vin1});
    NullorSolver<4, 4, 1> niSolver;
    niSolver.reset();
    niSolver.solve(ni.netlist, kDt);
    const double niGain = niSolver.voltage(ni.outNode) / niSolver.voltage(ni.inNode);
    report(std::fabs(niGain - (1.0 + Rf1 / Rg1)) < 1e-9,
           "non-inverting: Vout/Vin == 1+Rf/Rg (~1e-9)", niGain, 1.0 + Rf1 / Rg1, ok);

    const double Rin2 = 10000.0, Rf2 = 47000.0, vin2 = 1.0;
    const auto inv = invertingGain(InvertingGainBom{Rin2, Rf2, vin2});
    NullorSolver<4, 4, 1> invSolver;
    invSolver.reset();
    invSolver.solve(inv.netlist, kDt);
    const double invGain = invSolver.voltage(inv.outNode) / invSolver.voltage(inv.inNode);
    report(std::fabs(invGain - (-Rf2 / Rin2)) < 1e-9,
           "inverting: Vout/Vin == -Rf/Rin (~1e-9)", invGain, -Rf2 / Rin2, ok);
    return ok;
}

// --- 2. First-order response (SC-002 rung b) -------------------------------
bool runFirstOrderResponse() {
    std::puts("-- First-order response: active low-pass vs analytic BE recurrence --");
    bool ok = true;

    const auto stage = activeFirstOrder(ActiveFirstOrderBom{kRin, kRf, kCf, 1.0});
    NullorSolver<4, 5, 1> solver;
    solver.reset();
    const double alpha = kDt / (kRf * kCf);
    const double drive = kDt * 1.0 / (kRin * kCf);
    double vAnalytic = 0.0, maxErr = 0.0;
    for (int step = 0; step < 2000; ++step) {
        solver.solve(stage.netlist, kDt);
        vAnalytic = (vAnalytic - drive) / (1.0 + alpha);
        maxErr = std::max(maxErr, std::fabs(solver.voltage(stage.outNode) - vAnalytic));
    }
    report(maxErr < 1e-9, "active first-order: max |solver - BE recurrence| over 2000 steps < 1e-9",
           maxErr, 1e-9, ok);
    return ok;
}

// --- 3. DC-limit oracle (SC-002 rung c / SC-003) ---------------------------
bool runDcLimitOracle() {
    std::puts("-- Clipper DC limit vs independent bisection oracle --");
    bool ok = true;
    const DiodeSpec d = diodeSpec();

    ClipperSolver solver;
    double maxErr = 0.0;
    for (double vin = -2.0; vin <= 2.0 + 1e-9; vin += 0.5) {
        bool converged = true;
        const double vout = settleDc(
            solver,
            [&](double v) { return opAmpDiodeClipper(OpAmpDiodeClipperBom{kRin, kRf, 1.0e-9, d, 1, 1, v}); },
            vin, kDt, 20000, converged);
        if (!converged) {
            ok = false;
        }
        const double oracleVout = dcOracle(ClipperDc{kRin, kRf, vin, d.Is, d.n, d.Vt, 1, 1});
        maxErr = std::max(maxErr, std::fabs(vout - oracleVout));
    }
    report(maxErr < 1e-6, "clipper: settled DC vs bisection oracle, max err over sweep", maxErr, 1e-6, ok);
    return ok;
}

// --- 4. Saturation (SC-004) -------------------------------------------------
bool runSaturation() {
    std::puts("-- Saturation: clipper clamps near the diode drop under large drive --");
    bool ok = true;
    const DiodeSpec d = diodeSpec();
    const double linearGainMag = kRf / kRin;

    ClipperSolver solver;
    bool converged = true;
    const double vout = settleDc(
        solver, [&](double v) { return opAmpDiodeClipper(OpAmpDiodeClipperBom{kRin, kRf, kCf, d, 1, 1, v}); },
        5.0, kDt, 20000, converged);
    if (!converged) {
        ok = false;
    }
    report(std::fabs(vout) < 1.0, "clipper: |Vout| bounded well under the 5V-drive linear prediction",
           std::fabs(vout), 1.0, ok);
    report(std::fabs(vout) / (linearGainMag * 5.0) < 0.2,
           "clipper: |Vout|/|linear prediction| << 1 (not tracking input linearly)",
           std::fabs(vout) / (linearGainMag * 5.0), 0.2, ok);
    return ok;
}

// --- 5. Symmetry (SC-004) ---------------------------------------------------
bool runSymmetry() {
    std::puts("-- Symmetry: symmetric population odd; asymmetric shows DC offset --");
    bool ok = true;
    const DiodeSpec d = diodeSpec();
    bool converged = true;

    ClipperSolver symP, symN, asymP, asymN;
    const double yP = settleDc(
        symP, [&](double v) { return opAmpDiodeClipper(OpAmpDiodeClipperBom{kRin, kRf, kCf, d, 1, 1, v}); },
        1.0, kDt, 20000, converged);
    const double yN = settleDc(
        symN, [&](double v) { return opAmpDiodeClipper(OpAmpDiodeClipperBom{kRin, kRf, kCf, d, 1, 1, v}); },
        -1.0, kDt, 20000, converged);
    const double symOffset = std::fabs(yP + yN);
    report(symOffset < 1e-6, "symmetric (1:1): |y(+1)+y(-1)| ~ 0 (odd, no DC offset)", symOffset, 1e-6, ok);

    const double zP = settleDc(
        asymP, [&](double v) { return opAmpDiodeClipper(OpAmpDiodeClipperBom{kRin, kRf, kCf, d, 2, 1, v}); },
        1.0, kDt, 20000, converged);
    const double zN = settleDc(
        asymN, [&](double v) { return opAmpDiodeClipper(OpAmpDiodeClipperBom{kRin, kRf, kCf, d, 2, 1, v}); },
        -1.0, kDt, 20000, converged);
    const double asymOffset = std::fabs(zP + zN);
    report(asymOffset > 1e-3, "asymmetric (2:1): |y(+1)+y(-1)| measurably > 0 (DC offset)", asymOffset, 1e-3, ok);

    if (!converged) {
        ok = false;
    }
    return ok;
}

// --- 6. Passivity (SC-004) --------------------------------------------------
bool runPassivity() {
    std::puts("-- Passivity: passive feedback network dissipates (clipper energy <= undiode reference) --");
    bool ok = true;
    const DiodeSpec d = diodeSpec();
    std::array<double, kWindow> clipIn{}, clipOut{}, refIn{}, refOut{};
    bool converged = true;

    ClipperSolver clipSolver;
    driveSine(
        clipSolver, [&](double v) { return opAmpDiodeClipper(OpAmpDiodeClipperBom{kRin, kRf, kCf, d, 1, 1, v}); },
        2.0, 1000.0, kDt, kWarmup, clipIn, clipOut, converged);
    if (!converged) {
        ok = false;
    }

    // The undiode reference is purely linear + reactive (Rf || Cf only, same
    // Rin/Rf/Cf as the clipper): the NullorSolver, no Newton iteration, so no
    // convergence flag to check.
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
    report(eClip < eRef, "passive network: clipper output energy < undiode-reference output energy",
           eClip, eRef, ok);
    return ok;
}

// --- 7. Reactive signature (SC-005) -----------------------------------------
bool runReactiveSignature() {
    std::puts("-- Reactive signature: larger Cf strictly cuts post-clip HF (SC-005) --");
    bool ok = true;
    const DiodeSpec d = diodeSpec();
    const double fs = 1.0 / kDt;
    const std::array<double, 6> cfSweep{1.0e-9, 2.2e-9, 4.7e-9, 10.0e-9, 22.0e-9, 47.0e-9};
    std::array<double, kWindow> in{}, out{};
    bool converged = true;

    double prev = 1e300;
    bool mono = true;
    for (double Cf : cfSweep) {
        ClipperSolver solver;
        driveSine(
            solver, [&](double v) { return opAmpDiodeClipper(OpAmpDiodeClipperBom{kRin, kRf, Cf, d, 1, 1, v}); },
            2.0, 1000.0, kDt, kWarmup, in, out, converged);
        const double hf = hfEnergyAbove(out, fs, 5000.0);
        if (!(hf < prev)) {
            mono = false;
        }
        prev = hf;
    }
    report(mono, "clipper: HF(>5kHz) strictly decreasing across ascending Cf sweep", mono ? 1.0 : 0.0, 1.0, ok);
    if (!converged) {
        ok = false;
    }
    return ok;
}

// --- 8. Non-convergence surfaced (SC-006) -----------------------------------
bool runNonConvergence() {
    std::puts("-- Non-convergence surfaced (SC-006) --");
    bool ok = true;
    const DiodeSpec d = diodeSpec();

    const auto clip = opAmpDiodeClipper(OpAmpDiodeClipperBom{kRin, kRf, kCf, d, 1, 1, 5.0});
    ClipperSolver starved(/*maxIterations=*/1);
    starved.reset();
    const NewtonStatus status = starved.step(clip.netlist, kDt);
    report(!status.converged, "starved budget (1 iteration): converged == false is reported",
           status.converged ? 1.0 : 0.0, 0.0, ok);
    report(status.voltageResidual > starved.voltageTolerance(),
           "starved budget: the residual is surfaced (> tolerance)",
           status.voltageResidual, starved.voltageTolerance(), ok);
    return ok;
}

// --- 9. Bounded charter refusal (SC-007) ------------------------------------
bool runBoundedCharterRefusal() {
    std::puts("-- Bounded charter: >=2 nonlinearities refuse; augmentation is OpAmp-only (SC-007) --");
    bool ok = true;
    const DiodeSpec d = diodeSpec();

    // Tripwire (ii): a second nonlinearity at a distinct node pair — the
    // clipper's own feedback-diode port PLUS an unrelated diode elsewhere.
    Netlist<4, 6> nl;
    const NodeId vinNode = nl.addNode();
    const NodeId inMinus = nl.addNode();
    const NodeId out = nl.addNode();
    nl.add(VoltageSource{vinNode, kGround, 1.0});
    nl.add(Resistor{vinNode, inMinus, kRin});
    nl.add(OpAmp{kGround, inMinus, out});
    nl.add(Resistor{out, inMinus, kRf});
    nl.add(Diode{out, inMinus, d.Is, d.n, d.Vt});      // the clipper's own port
    nl.add(Diode{vinNode, kGround, d.Is, d.n, d.Vt});  // a distinct nonlinearity location
    nl.prepare();

    OpAmpClipperSolver<4, 6, 4, 1> clipper;
    bool threw = false;
    try {
        clipper.step(nl, kDt);
    } catch (const std::runtime_error&) {
        threw = true;
    }
    report(threw, ">=2 interacting nonlinearities: out-of-scope error raised (tripwire ii)",
           threw ? 1.0 : 0.0, 1.0, ok);

    // Tripwire (i): a floating (non-grounded) ideal VoltageSource — the
    // "becoming general MNA" signal; only OpAmp branches are augmented.
    Netlist<3, 3> floatingNl;
    const NodeId n1 = floatingNl.addNode();
    const NodeId n2 = floatingNl.addNode();
    floatingNl.add(Resistor{n1, kGround, 1000.0});
    floatingNl.add(Resistor{n2, kGround, 1000.0});
    floatingNl.add(VoltageSource{n1, n2, 5.0});  // floating: neither terminal grounded
    floatingNl.prepare();

    NullorSolver<3, 3, 1> nullorSolver;
    bool floatingThrew = false;
    try {
        nullorSolver.solve(floatingNl, kDt);
    } catch (const std::runtime_error&) {
        floatingThrew = true;
    }
    report(floatingThrew, "floating (non-grounded) VoltageSource: augmentation stays OpAmp-only (tripwire i)",
           floatingThrew ? 1.0 : 0.0, 1.0, ok);

    // Tripwire (iii): an op-amp population beyond the instantiated MaxOpAmps
    // capacity — the fixed bordered dimension has no dynamic growth. Two
    // independent, well-posed op-amp stages solved by a MaxOpAmps=1 solver.
    Netlist<5, 6> capNl;
    const NodeId a = capNl.addNode();
    const NodeId b = capNl.addNode();
    const NodeId c = capNl.addNode();
    const NodeId e = capNl.addNode();
    capNl.add(Resistor{a, kGround, 1000.0});
    capNl.add(OpAmp{kGround, a, b});
    capNl.add(Resistor{b, a, 1000.0});
    capNl.add(OpAmp{kGround, c, e});
    capNl.add(Resistor{c, kGround, 1000.0});
    capNl.add(Resistor{e, c, 1000.0});
    capNl.prepare();

    NullorSolver<5, 6, 1> capSolver;  // MaxOpAmps = 1, but capNl carries 2 op-amps
    bool capThrew = false;
    try {
        capSolver.solve(capNl, kDt);
    } catch (const std::runtime_error&) {
        capThrew = true;
    }
    report(capThrew, "op-amp population beyond MaxOpAmps: fixed bordered dimension refuses (tripwire iii)",
           capThrew ? 1.0 : 0.0, 1.0, ok);
    return ok;
}

}  // namespace

int main() {
    bool allPassed = true;
    allPassed = runAnalyticGains() && allPassed;
    allPassed = runFirstOrderResponse() && allPassed;
    allPassed = runDcLimitOracle() && allPassed;
    allPassed = runSaturation() && allPassed;
    allPassed = runSymmetry() && allPassed;
    allPassed = runPassivity() && allPassed;
    allPassed = runReactiveSignature() && allPassed;
    allPassed = runNonConvergence() && allPassed;
    allPassed = runBoundedCharterRefusal() && allPassed;

    std::puts(allPassed ? "\nopamp-stages-harness: ALL CHECKS PASSED"
                        : "\nopamp-stages-harness: FAILURES DETECTED");
    return allPassed ? 0 : 1;
}
