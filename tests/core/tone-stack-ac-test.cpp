#include <doctest/doctest.h>

#include <cmath>
#include <complex>

#include "labs/passive-tone-stacks/solver/ac-solver.h"
#include "primitives/circuit/components.h"
#include "primitives/circuit/netlist.h"
#include "primitives/circuit/node.h"

// US3 Tier-2 (spec.md US3, SC-003; contracts/ac-solver.md): validation of the
// lab complex `.ac` solver. This file begins with the SANITY block — networks
// with exact closed-form transfer functions (RC low-pass, resistive divider,
// series RLC) — so `solveAC` is proven before it is ever trusted on an
// assembled tone stack. The FMV / Baxandall analytic-match blocks (SC-004/005)
// are added on top once the builders and the analytic reference land.

using acfx::Capacitor;
using acfx::Inductor;
using acfx::kGround;
using acfx::Netlist;
using acfx::NodeId;
using acfx::Resistor;
using acfx::VoltageSource;
using acfx::labs::passive_tone_stacks::Complex;
using acfx::labs::passive_tone_stacks::solveAC;

namespace {
constexpr double kPi = 3.14159265358979323846;
double magDb(Complex h) { return 20.0 * std::log10(std::abs(h)); }
}  // namespace

// ---------------------------------------------------------------------------
// Resistive divider — flat H = R2/(R1+R2) at every frequency (SC-003).
// ---------------------------------------------------------------------------

TEST_CASE("solveAC - resistive divider is flat at R2/(R1+R2)") {
    const double R1 = 1000.0;
    const double R2 = 2000.0;
    Netlist<4, 4> nl;
    const NodeId nIn = nl.addNode();
    const NodeId nOut = nl.addNode();
    nl.add(VoltageSource{nIn, kGround, 1.0});
    nl.add(Resistor{nIn, nOut, R1});
    nl.add(Resistor{nOut, kGround, R2});
    nl.prepare();

    const double expected = R2 / (R1 + R2);
    for (double f : {20.0, 200.0, 2000.0, 20000.0}) {
        const Complex h = solveAC(nl, 2.0 * kPi * f, nIn, nOut);
        CHECK(h.real() == doctest::Approx(expected).epsilon(1e-9));
        CHECK(std::abs(h.imag()) == doctest::Approx(0.0).epsilon(1e-9));
    }
}

// ---------------------------------------------------------------------------
// RC low-pass — H = 1/(1 + jωRC): magnitude, phase, and the −20 dB/decade
// slope and −90° phase asymptote (SC-003).
// ---------------------------------------------------------------------------

TEST_CASE("solveAC - RC low-pass matches 1/(1 + jωRC) exactly") {
    const double R = 10000.0;
    const double C = 10.0e-9;  // fc = 1/(2πRC) ≈ 1591.5 Hz
    Netlist<4, 4> nl;
    const NodeId nIn = nl.addNode();
    const NodeId nOut = nl.addNode();
    nl.add(VoltageSource{nIn, kGround, 1.0});
    nl.add(Resistor{nIn, nOut, R});
    nl.add(Capacitor{nOut, kGround, C});
    nl.prepare();

    for (double f : {50.0, 500.0, 1591.5, 5000.0, 20000.0}) {
        const double w = 2.0 * kPi * f;
        const Complex expected = 1.0 / Complex(1.0, w * R * C);
        const Complex h = solveAC(nl, w, nIn, nOut);
        CHECK(h.real() == doctest::Approx(expected.real()).epsilon(1e-9));
        CHECK(h.imag() == doctest::Approx(expected.imag()).epsilon(1e-9));
    }
}

TEST_CASE("solveAC - RC low-pass has a −20 dB/decade slope and −90° asymptote") {
    const double R = 10000.0;
    const double C = 10.0e-9;
    Netlist<4, 4> nl;
    const NodeId nIn = nl.addNode();
    const NodeId nOut = nl.addNode();
    nl.add(VoltageSource{nIn, kGround, 1.0});
    nl.add(Resistor{nIn, nOut, R});
    nl.add(Capacitor{nOut, kGround, C});
    nl.prepare();

    // Two frequencies a decade apart, well above the corner (~1591 Hz).
    const Complex hHi = solveAC(nl, 2.0 * kPi * 20000.0, nIn, nOut);
    const Complex hHi10 = solveAC(nl, 2.0 * kPi * 200000.0, nIn, nOut);
    CHECK((magDb(hHi) - magDb(hHi10)) == doctest::Approx(20.0).epsilon(0.02));

    // Phase approaches −90° far above the corner.
    const double phaseDeg = std::arg(hHi10) * 180.0 / kPi;
    CHECK(phaseDeg == doctest::Approx(-90.0).epsilon(0.02));
}

// ---------------------------------------------------------------------------
// Series RLC — a second-order response with a known resonant magnitude peak
// (SC-003). Output across the capacitor: H = 1/(1 − ω²LC + jωRC).
// ---------------------------------------------------------------------------

TEST_CASE("solveAC - series RLC matches its second-order transfer function") {
    const double R = 100.0;
    const double L = 1.0e-3;
    const double C = 1.0e-6;  // f0 = 1/(2π√(LC)) ≈ 5033 Hz
    Netlist<5, 5> nl;
    const NodeId nIn = nl.addNode();
    const NodeId nMid = nl.addNode();
    const NodeId nOut = nl.addNode();
    nl.add(VoltageSource{nIn, kGround, 1.0});
    nl.add(Resistor{nIn, nMid, R});
    nl.add(Inductor{nMid, nOut, L});
    nl.add(Capacitor{nOut, kGround, C});
    nl.prepare();

    for (double f : {500.0, 5033.0, 20000.0}) {
        const double w = 2.0 * kPi * f;
        const Complex expected =
            1.0 / Complex(1.0 - w * w * L * C, w * R * C);
        const Complex h = solveAC(nl, w, nIn, nOut);
        CHECK(h.real() == doctest::Approx(expected.real()).epsilon(1e-9));
        CHECK(h.imag() == doctest::Approx(expected.imag()).epsilon(1e-9));
    }
}
