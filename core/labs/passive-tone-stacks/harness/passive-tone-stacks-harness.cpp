// passive-tone-stacks-harness.cpp
// Host-only lab harness: never included by portable code.
//
// Validation for the passive-tone-stacks lab AC solver (contracts/ac-solver.md;
// spec.md US3/SC-003/SC-004/SC-005). Drives `solveAC` directly (no effect layer,
// no DAW) and prints PASS/FAIL lines with measured-vs-expected numbers. Mirrors
// the doctest assertions in tests/core/tone-stack-ac-test.cpp so the same
// validation is runnable/readable outside the test framework. Exits nonzero if
// anything fails.
//
// Validates, in order: RC low-pass and resistive-divider SANITY networks
// (closed form), then the FMV and Baxandall assembled stacks — the exact DC
// resistive limit, passivity, and the monotonic musical invariants (HF up with
// treble, LF up with bass, mid scoop deepening as mid lowers; Baxandall
// bass/treble shelves). The AC solver is proven exact on the sanity networks, so
// the tone stacks are validated by these exact limits + invariants (the
// operator-agreed approach), not a transcribed rational.
//
// Include root: core/ (see the acfx_lab_passive_tone_stacks_harness CMake
// target). Compile with -std=c++20.

#include "labs/passive-tone-stacks/solver/ac-solver.h"
#include "primitives/circuit/components.h"
#include "primitives/circuit/netlist.h"
#include "primitives/circuit/node.h"
#include "primitives/circuit/tone-stack/tone-stack.h"

#include <cmath>
#include <complex>
#include <cstdio>

using acfx::BaxandallControls;
using acfx::BaxandallValues;
using acfx::Capacitor;
using acfx::FMVControls;
using acfx::FMVValues;
using acfx::kGround;
using acfx::Netlist;
using acfx::NodeId;
using acfx::Resistor;
using acfx::Taper;
using acfx::ToneStack;
using acfx::toneStackBaxandall;
using acfx::toneStackFMV;
using acfx::VoltageSource;
using acfx::wiper;
using acfx::labs::passive_tone_stacks::Complex;
using acfx::labs::passive_tone_stacks::solveAC;

namespace {

constexpr double kPi = 3.14159265358979323846;

void report(bool ok, const char* label, double measured, double expected, bool& allPassed) {
    std::printf("[%s] %-56s measured=% .10g  expected=% .10g\n",
                ok ? "PASS" : "FAIL", label, measured, expected);
    if (!ok) {
        allPassed = false;
    }
}

void reportCmp(bool ok, const char* label, double a, double b, bool& allPassed) {
    std::printf("[%s] %-56s a=% .10g  b=% .10g\n", ok ? "PASS" : "FAIL", label, a, b);
    if (!ok) {
        allPassed = false;
    }
}

FMVValues fmvValues() {
    return FMVValues{56000.0, 250e-12, 0.1e-6, 250000.0, 250000.0, 25000.0, 1.0e6};
}
BaxandallValues baxValues() {
    return BaxandallValues{100000.0, 0.022e-6, 10000.0, 0.022e-6, 100000.0, 10000.0, 1.0e6};
}

template <int N, int M>
double magAt(const ToneStack<N, M>& ts, double f) {
    return std::abs(solveAC(ts.netlist, 2.0 * kPi * f, ts.inNode, ts.outNode));
}

// --- Sanity: RC low-pass + resistive divider (closed form) -----------------
bool runSanity() {
    std::puts("-- AC solver sanity (SC-003) --");
    bool ok = true;

    // Resistive divider: flat R2/(R1+R2).
    {
        const double R1 = 1000.0, R2 = 2000.0;
        Netlist<4, 4> nl;
        const NodeId n1 = nl.addNode();
        const NodeId n2 = nl.addNode();
        nl.add(VoltageSource{n1, kGround, 1.0});
        nl.add(Resistor{n1, n2, R1});
        nl.add(Resistor{n2, kGround, R2});
        nl.prepare();
        const double m = std::abs(solveAC(nl, 2.0 * kPi * 1000.0, n1, n2));
        report(std::fabs(m - R2 / (R1 + R2)) < 1e-9, "resistive divider flat", m, R2 / (R1 + R2), ok);
    }
    // RC low-pass at the corner: |H| = 1/sqrt(2).
    {
        const double R = 10000.0, C = 10.0e-9;
        Netlist<4, 4> nl;
        const NodeId n1 = nl.addNode();
        const NodeId n2 = nl.addNode();
        nl.add(VoltageSource{n1, kGround, 1.0});
        nl.add(Resistor{n1, n2, R});
        nl.add(Capacitor{n2, kGround, C});
        nl.prepare();
        const double fc = 1.0 / (2.0 * kPi * R * C);
        const double m = std::abs(solveAC(nl, 2.0 * kPi * fc, n1, n2));
        report(std::fabs(m - 1.0 / std::sqrt(2.0)) < 1e-9, "RC low-pass |H| at corner", m,
               1.0 / std::sqrt(2.0), ok);
    }
    return ok;
}

// --- FMV stack (SC-004) ----------------------------------------------------
bool runFmv() {
    std::puts("-- FMV tone stack (SC-004) --");
    bool ok = true;
    const FMVValues v = fmvValues();

    // Exact DC limit.
    {
        const auto ts = toneStackFMV(v, FMVControls{0.5, 0.5, 0.4}, Taper::Linear);
        const double rBot = wiper(v.rTreble, 0.4, Taper::Linear).rBottom;
        const double expected = v.rLoad / (v.r1 + rBot + v.rLoad);
        const double m = magAt(ts, 1.0e-4);
        report(std::fabs(m - expected) < 1e-6 * expected, "FMV exact DC limit", m, expected, ok);
    }
    // Passivity across the band.
    {
        const auto ts = toneStackFMV(v, FMVControls{0.7, 0.5, 0.7}, Taper::Linear);
        double worst = 0.0;
        for (int i = 0; i <= 30; ++i) {
            const double f = 20.0 * std::pow(10.0, i / 10.0);
            if (f > 20000.0) break;
            worst = std::max(worst, magAt(ts, f));
        }
        report(worst <= 1.0 + 1e-9, "FMV passive |H| <= 1", worst, 1.0, ok);
    }
    // Treble raises HF.
    {
        const double hf = 8000.0;
        const double lo = magAt(toneStackFMV(v, FMVControls{0.5, 0.5, 0.0}, Taper::Linear), hf);
        const double hi = magAt(toneStackFMV(v, FMVControls{0.5, 0.5, 1.0}, Taper::Linear), hf);
        reportCmp(hi > lo, "FMV treble raises HF (hi>lo)", hi, lo, ok);
    }
    // Bass raises LF.
    {
        const double lf = 80.0;
        const double lo = magAt(toneStackFMV(v, FMVControls{0.0, 0.5, 0.5}, Taper::Linear), lf);
        const double hi = magAt(toneStackFMV(v, FMVControls{1.0, 0.5, 0.5}, Taper::Linear), lf);
        reportCmp(hi > lo, "FMV bass raises LF (hi>lo)", hi, lo, ok);
    }
    // Mid scoop deepens as mid lowers.
    {
        const double mf = 400.0;
        const double midLow = magAt(toneStackFMV(v, FMVControls{0.5, 0.0, 0.5}, Taper::Linear), mf);
        const double midHigh = magAt(toneStackFMV(v, FMVControls{0.5, 1.0, 0.5}, Taper::Linear), mf);
        reportCmp(midLow < midHigh, "FMV mid scoop deepens (low<high)", midLow, midHigh, ok);
    }
    return ok;
}

// --- Baxandall stack (SC-005) ----------------------------------------------
bool runBaxandall() {
    std::puts("-- Baxandall (passive James) 2-band (SC-005) --");
    bool ok = true;
    const BaxandallValues v = baxValues();

    {
        const auto ts = toneStackBaxandall(v, BaxandallControls{0.5, 0.5}, Taper::Linear);
        double worst = 0.0;
        for (int i = 0; i <= 30; ++i) {
            const double f = 20.0 * std::pow(10.0, i / 10.0);
            if (f > 20000.0) break;
            worst = std::max(worst, magAt(ts, f));
        }
        report(worst <= 1.0 + 1e-9, "Baxandall passive |H| <= 1", worst, 1.0, ok);
    }
    {
        const double lf = 50.0;
        const double lo = magAt(toneStackBaxandall(v, BaxandallControls{0.0, 0.5}, Taper::Linear), lf);
        const double hi = magAt(toneStackBaxandall(v, BaxandallControls{1.0, 0.5}, Taper::Linear), lf);
        reportCmp(hi > lo, "Baxandall bass shelf rises (hi>lo)", hi, lo, ok);
    }
    {
        const double hf = 10000.0;
        const double lo = magAt(toneStackBaxandall(v, BaxandallControls{0.5, 0.0}, Taper::Linear), hf);
        const double hi = magAt(toneStackBaxandall(v, BaxandallControls{0.5, 1.0}, Taper::Linear), hf);
        reportCmp(hi > lo, "Baxandall treble shelf rises (hi>lo)", hi, lo, ok);
    }
    return ok;
}

}  // namespace

int main() {
    std::puts("passive-tone-stacks lab harness");
    bool allPassed = true;
    allPassed &= runSanity();
    allPassed &= runFmv();
    allPassed &= runBaxandall();
    std::printf("\n%s\n", allPassed ? "ALL PASS" : "FAILURES PRESENT");
    return allPassed ? 0 : 1;
}
