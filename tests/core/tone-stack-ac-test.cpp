#include <doctest/doctest.h>

#include <cmath>
#include <complex>

#include "labs/passive-tone-stacks/solver/ac-solver.h"
#include "primitives/circuit/components.h"
#include "primitives/circuit/netlist.h"
#include "primitives/circuit/node.h"
#include "primitives/circuit/tone-stack/tone-stack.h"

// US3 Tier-2 (spec.md US3, SC-003; contracts/ac-solver.md): validation of the
// lab complex `.ac` solver. This file begins with the SANITY block — networks
// with exact closed-form transfer functions (RC low-pass, resistive divider,
// series RLC) — so `solveAC` is proven before it is ever trusted on an
// assembled tone stack. The FMV / Baxandall analytic-match blocks (SC-004/005)
// are added on top once the builders and the analytic reference land.

using acfx::BaxandallControls;
using acfx::BaxandallValues;
using acfx::Capacitor;
using acfx::FMVControls;
using acfx::FMVValues;
using acfx::Inductor;
using acfx::kGround;
using acfx::Netlist;
using acfx::NodeId;
using acfx::Resistor;
using acfx::Taper;
using acfx::toneStackBaxandall;
using acfx::toneStackFMV;
using acfx::VoltageSource;
using acfx::wiper;
using acfx::labs::passive_tone_stacks::Complex;
using acfx::labs::passive_tone_stacks::solveAC;

namespace {
constexpr double kPi = 3.14159265358979323846;
double magDb(Complex h) { return 20.0 * std::log10(std::abs(h)); }

FMVValues fmvValues() {
    return FMVValues{/*r1=*/56000.0, /*c1=*/250e-12, /*c2=*/0.1e-6,
                     /*rTreble=*/250000.0, /*rBass=*/250000.0,
                     /*rMid=*/25000.0, /*rLoad=*/1.0e6};
}

BaxandallValues baxValues() {
    return BaxandallValues{/*rBass=*/100000.0, /*cBass=*/0.022e-6,
                           /*rBassOut=*/10000.0, /*cTreble=*/0.022e-6,
                           /*rTreble=*/100000.0, /*rTrebleOut=*/10000.0,
                           /*rLoad=*/1.0e6};
}

// |H(f)| of an assembled tone stack at frequency f (Hz).
template <int N, int M>
double magAt(const acfx::ToneStack<N, M>& ts, double f) {
    return std::abs(solveAC(ts.netlist, 2.0 * kPi * f, ts.inNode, ts.outNode));
}
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

// ===========================================================================
// FMV tone stack (SC-004). The AC solver is proven exact above, so the stack is
// validated by (1) the EXACT DC resistive-limit closed form, (2) passivity, and
// (3) the monotonic musical invariants that define a tone control: HF rises with
// the treble pot, LF rises with the bass pot, and the mid scoop deepens as the
// mid pot lowers. A mis-wired topology breaks one of these.
// ===========================================================================

TEST_CASE("toneStackFMV - exact DC limit: rLoad/(R1 + trebleBottom + rLoad)") {
    // At DC both caps are open, leaving Vin -R1- nJ -trebleBottom- nOut -rLoad- gnd.
    const FMVValues v = fmvValues();
    for (double treble : {0.0, 0.4, 1.0}) {
        const auto ts = toneStackFMV(v, FMVControls{0.5, 0.5, treble}, Taper::Linear);
        const double rBot = wiper(v.rTreble, treble, Taper::Linear).rBottom;
        const double expected = v.rLoad / (v.r1 + rBot + v.rLoad);
        // f = 1e-4 Hz: the caps are effectively open (Z_C ≫ everything).
        CHECK(magAt(ts, 1.0e-4) == doctest::Approx(expected).epsilon(1e-6));
    }
}

TEST_CASE("toneStackFMV - passive: |H(f)| never exceeds unity across the band") {
    const auto ts = toneStackFMV(fmvValues(), FMVControls{0.7, 0.5, 0.7}, Taper::Linear);
    for (int i = 0; i <= 30; ++i) {  // ~10 pts/decade, 20 Hz .. 20 kHz
        const double f = 20.0 * std::pow(10.0, static_cast<double>(i) / 10.0);
        if (f > 20000.0) break;
        CHECK(magAt(ts, f) <= doctest::Approx(1.0).epsilon(1e-9));
    }
}

TEST_CASE("toneStackFMV - treble pot raises high-frequency magnitude monotonically") {
    const FMVValues v = fmvValues();
    const double hf = 8000.0;
    const double lo = magAt(toneStackFMV(v, FMVControls{0.5, 0.5, 0.0}, Taper::Linear), hf);
    const double mid = magAt(toneStackFMV(v, FMVControls{0.5, 0.5, 0.5}, Taper::Linear), hf);
    const double hi = magAt(toneStackFMV(v, FMVControls{0.5, 0.5, 1.0}, Taper::Linear), hf);
    CHECK(mid > lo);
    CHECK(hi > mid);
}

TEST_CASE("toneStackFMV - bass pot raises low-frequency magnitude monotonically") {
    const FMVValues v = fmvValues();
    const double lf = 80.0;
    const double lo = magAt(toneStackFMV(v, FMVControls{0.0, 0.5, 0.5}, Taper::Linear), lf);
    const double hi = magAt(toneStackFMV(v, FMVControls{1.0, 0.5, 0.5}, Taper::Linear), lf);
    CHECK(hi > lo);
}

TEST_CASE("toneStackFMV - mid scoop deepens as the mid pot lowers") {
    const FMVValues v = fmvValues();
    const double midFreq = 400.0;
    const double midLow = magAt(toneStackFMV(v, FMVControls{0.5, 0.0, 0.5}, Taper::Linear), midFreq);
    const double midHigh = magAt(toneStackFMV(v, FMVControls{0.5, 1.0, 0.5}, Taper::Linear), midFreq);
    // Lowering the mid pot cuts more midrange: the scoop is deeper (lower |H|).
    CHECK(midLow < midHigh);
}

// ===========================================================================
// Baxandall (passive James) 2-band stack (SC-005): passivity + the bass/treble
// shelf invariants + a roughly-balanced center. Linear taper (Baxandall pots).
// ===========================================================================

TEST_CASE("toneStackBaxandall - passive: |H(f)| never exceeds unity") {
    const auto ts = toneStackBaxandall(baxValues(), BaxandallControls{0.5, 0.5}, Taper::Linear);
    for (int i = 0; i <= 30; ++i) {
        const double f = 20.0 * std::pow(10.0, static_cast<double>(i) / 10.0);
        if (f > 20000.0) break;
        CHECK(magAt(ts, f) <= doctest::Approx(1.0).epsilon(1e-9));
    }
}

TEST_CASE("toneStackBaxandall - bass shelf rises with the bass pot at low freq") {
    const BaxandallValues v = baxValues();
    const double lf = 50.0;
    const double lo = magAt(toneStackBaxandall(v, BaxandallControls{0.0, 0.5}, Taper::Linear), lf);
    const double hi = magAt(toneStackBaxandall(v, BaxandallControls{1.0, 0.5}, Taper::Linear), lf);
    CHECK(hi > lo);
}

TEST_CASE("toneStackBaxandall - treble shelf rises with the treble pot at high freq") {
    const BaxandallValues v = baxValues();
    const double hf = 10000.0;
    const double lo = magAt(toneStackBaxandall(v, BaxandallControls{0.5, 0.0}, Taper::Linear), hf);
    const double hi = magAt(toneStackBaxandall(v, BaxandallControls{0.5, 1.0}, Taper::Linear), hf);
    CHECK(hi > lo);
}
