#include <doctest/doctest.h>

#include <variant>

#include "primitives/circuit/components.h"
#include "primitives/circuit/tone-stack/tone-stack.h"

// US1 (spec.md US1, SC-001; contracts/tone-stack-builder.md): the solver-neutral
// tone-stack builders produce a prepare()-valid Netlist of FROZEN-vocabulary
// components across the full control range, with counts matching the bill of
// materials — WITH NO SOLVER. Isolation (FR-016) is enforced at compile time:
// this translation unit includes only the tone-stack primitive and the frozen
// circuit vocabulary; it includes nothing under core/labs/, and it links without
// any lab code. That the builders compile here is the isolation check.

using acfx::BaxandallControls;
using acfx::BaxandallValues;
using acfx::Capacitor;
using acfx::Component;
using acfx::FMVControls;
using acfx::FMVValues;
using acfx::Resistor;
using acfx::Taper;
using acfx::toneStackBaxandall;
using acfx::toneStackFMV;
using acfx::VoltageSource;

namespace {

// Representative FMV bill of materials (Fender-style values; exact vendor BOM
// fidelity is the later fender-tone-stack feature).
FMVValues fmvValues() {
    return FMVValues{/*r1=*/56000.0,   /*c1=*/250e-12, /*c2=*/0.1e-6,
                     /*rTreble=*/250000.0, /*rBass=*/250000.0,
                     /*rMid=*/25000.0, /*rLoad=*/1.0e6};
}

BaxandallValues baxValues() {
    return BaxandallValues{/*rBass=*/100000.0, /*cBass=*/0.022e-6,
                           /*rBassOut=*/10000.0, /*cTreble=*/0.022e-6,
                           /*rTreble=*/100000.0, /*rTrebleOut=*/10000.0,
                           /*rLoad=*/1.0e6};
}

// Every component in the netlist must be a member of the frozen v1 vocabulary
// used by a passive stack: Resistor, Capacitor, or VoltageSource.
template <int N, int M>
bool onlyFrozenPassiveVocabulary(const acfx::Netlist<N, M>& nl) {
    for (const Component& c : nl.components()) {
        const bool ok = std::holds_alternative<Resistor>(c) ||
                        std::holds_alternative<Capacitor>(c) ||
                        std::holds_alternative<VoltageSource>(c);
        if (!ok) {
            return false;
        }
    }
    return true;
}

}  // namespace

// ---------------------------------------------------------------------------
// FMV — prepare()-valid across the full control range (SC-001).
// ---------------------------------------------------------------------------

TEST_CASE("toneStackFMV - prepare() succeeds across the full control range") {
    const FMVValues v = fmvValues();
    // toneStackFMV calls prepare() internally; a throw here is a build failure.
    for (Taper taper : {Taper::Linear, Taper::Log}) {
        CHECK_NOTHROW(toneStackFMV(v, FMVControls{0.0, 0.0, 0.0}, taper));
        CHECK_NOTHROW(toneStackFMV(v, FMVControls{1.0, 1.0, 1.0}, taper));
        CHECK_NOTHROW(toneStackFMV(v, FMVControls{0.2, 0.8, 0.4}, taper));
        CHECK_NOTHROW(toneStackFMV(v, FMVControls{0.5, 0.5, 0.5}, taper));
    }
}

TEST_CASE("toneStackFMV - counts match the bill of materials, frozen vocabulary") {
    const auto ts = toneStackFMV(fmvValues(), FMVControls{0.5, 0.5, 0.5},
                                 Taper::Linear);
    // 9 components: source + slope + 2 caps + treble divider (2) + bass + mid + load.
    CHECK(ts.netlist.componentCount() == 9);
    // 7 nodes including ground (6 non-ground added).
    CHECK(ts.netlist.nodeCount() == 7);
    CHECK(onlyFrozenPassiveVocabulary(ts.netlist));
    // The output tap is distinct from the driven input.
    CHECK(ts.inNode != ts.outNode);
}

// ---------------------------------------------------------------------------
// Baxandall — prepare()-valid across the full control range (SC-001).
// ---------------------------------------------------------------------------

TEST_CASE("toneStackBaxandall - prepare() succeeds across the full control range") {
    const BaxandallValues v = baxValues();
    for (Taper taper : {Taper::Linear, Taper::Log}) {
        CHECK_NOTHROW(toneStackBaxandall(v, BaxandallControls{0.0, 0.0}, taper));
        CHECK_NOTHROW(toneStackBaxandall(v, BaxandallControls{1.0, 1.0}, taper));
        CHECK_NOTHROW(toneStackBaxandall(v, BaxandallControls{0.3, 0.7}, taper));
        CHECK_NOTHROW(toneStackBaxandall(v, BaxandallControls{0.5, 0.5}, taper));
    }
}

TEST_CASE("toneStackBaxandall - counts match the bill of materials, frozen vocab") {
    const auto ts = toneStackBaxandall(baxValues(), BaxandallControls{0.5, 0.5},
                                       Taper::Linear);
    // 10 components: source + bass divider (2) + bass cap + bass-out + treble cap
    // + treble divider (2) + treble-out + load.
    CHECK(ts.netlist.componentCount() == 10);
    // 6 nodes including ground (5 non-ground added).
    CHECK(ts.netlist.nodeCount() == 6);
    CHECK(onlyFrozenPassiveVocabulary(ts.netlist));
    CHECK(ts.inNode != ts.outNode);
}

// ---------------------------------------------------------------------------
// Fail-loud validation — bad BOM / controls throw (FR-010).
// ---------------------------------------------------------------------------

TEST_CASE("toneStackFMV - non-positive BOM value throws std::invalid_argument") {
    FMVValues v = fmvValues();
    v.rLoad = 0.0;
    CHECK_THROWS_AS(toneStackFMV(v, FMVControls{0.5, 0.5, 0.5}, Taper::Linear),
                    std::invalid_argument);
    v = fmvValues();
    v.c1 = -1.0;
    CHECK_THROWS_AS(toneStackFMV(v, FMVControls{0.5, 0.5, 0.5}, Taper::Linear),
                    std::invalid_argument);
}

TEST_CASE("toneStackFMV - control outside [0,1] throws std::invalid_argument") {
    CHECK_THROWS_AS(toneStackFMV(fmvValues(), FMVControls{0.5, 1.5, 0.5}, Taper::Linear),
                    std::invalid_argument);
    CHECK_THROWS_AS(toneStackFMV(fmvValues(), FMVControls{-0.1, 0.5, 0.5}, Taper::Linear),
                    std::invalid_argument);
}

TEST_CASE("toneStackBaxandall - bad BOM / control throw std::invalid_argument") {
    BaxandallValues v = baxValues();
    v.rTreble = -5.0;
    CHECK_THROWS_AS(toneStackBaxandall(v, BaxandallControls{0.5, 0.5}, Taper::Linear),
                    std::invalid_argument);
    CHECK_THROWS_AS(
        toneStackBaxandall(baxValues(), BaxandallControls{0.5, 2.0}, Taper::Linear),
        std::invalid_argument);
}
