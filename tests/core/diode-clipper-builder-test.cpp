#include <doctest/doctest.h>

#include <variant>

#include "primitives/circuit/components.h"
#include "primitives/circuit/diode-clipper/clipper-config.h"
#include "primitives/circuit/diode-clipper/diode-clipper.h"

// Tier-1 builder-topology suite for the diode-clipper primitive (US1, SC-001;
// contracts/diode-clipper-builder.md). The three solver-neutral builders must
// return a prepare()-valid Netlist of FROZEN-vocabulary components with
// component/node counts matching each topology's bill of materials, the port
// nodes equal to the diode-string node pair, and descriptive throws on bad BOM
// input — WITH NO SOLVER.
//
// ISOLATION (FR-019) is enforced at compile time: this translation unit includes
// ONLY the diode-clipper primitive and the frozen circuit vocabulary; it includes
// nothing under core/labs/, and it links without any lab code. That the builders
// compile and run here is itself the isolation check (SC-007).

using acfx::AsymmetricShuntValues;
using acfx::asymmetricShuntClipper;
using acfx::Capacitor;
using acfx::Component;
using acfx::Diode;
using acfx::DiodeSpec;
using acfx::kGround;
using acfx::NodeId;
using acfx::Resistor;
using acfx::seriesClipper;
using acfx::SeriesValues;
using acfx::siliconSignalDiode;
using acfx::SymmetricShuntValues;
using acfx::symmetricShuntClipper;
using acfx::VoltageSource;

namespace {

// Every component must be a member of the frozen v1 clipper vocabulary:
// Resistor, Capacitor, Diode, or VoltageSource — no new element type (FR-004).
template <int N, int M>
bool onlyFrozenClipperVocabulary(const acfx::Netlist<N, M>& nl) {
    for (const Component& c : nl.components()) {
        const bool ok = std::holds_alternative<Resistor>(c) ||
                        std::holds_alternative<Capacitor>(c) ||
                        std::holds_alternative<Diode>(c) ||
                        std::holds_alternative<VoltageSource>(c);
        if (!ok) {
            return false;
        }
    }
    return true;
}

// Count the diodes in a netlist.
template <int N, int M>
int diodeCount(const acfx::Netlist<N, M>& nl) {
    int count = 0;
    for (const Component& c : nl.components()) {
        if (std::holds_alternative<Diode>(c)) {
            ++count;
        }
    }
    return count;
}

// Every diode must span the reported clipper port node pair (in either
// orientation) — the port IS the diode-string node pair (FR guarantees 3).
//
// This is the correct invariant for ALL three topologies, series included:
// FR-012 bounds the solver to a SINGLE nonlinearity LOCATION — one node pair
// carrying the whole diode string (up to MaxDiodes diodes summing at that pair).
// The series exemplar's "in series" means in the series SIGNAL PATH (ahead of
// the shunt-to-ground R), NOT a node-chain of diodes with intermediate nodes: a
// chain would place diodes on DISTINCT node pairs, i.e. a second interacting
// nonlinearity the transient solver deliberately refuses (a Phase-5 subject).
// So seriesCount > 1 stacks diodes across the same (portP, portN) pair, and this
// helper's same-pair requirement is exactly the in-scope guarantee, not a
// mistaken parallel-vs-series conflation.
template <int N, int M>
bool everyDiodeSpansPort(const acfx::Netlist<N, M>& nl, NodeId portP, NodeId portN) {
    for (const Component& c : nl.components()) {
        if (const auto* d = std::get_if<Diode>(&c)) {
            const bool fwd = d->anode == portP && d->cathode == portN;
            const bool rev = d->anode == portN && d->cathode == portP;
            if (!fwd && !rev) {
                return false;
            }
        }
    }
    return true;
}

SymmetricShuntValues symValues() {
    return SymmetricShuntValues{2200.0, 10.0e-9, siliconSignalDiode()};
}
AsymmetricShuntValues asymValues() {
    return AsymmetricShuntValues{2200.0, 10.0e-9, siliconSignalDiode(), 2, 1};
}
SeriesValues seriesValues(int seriesCount = 1) {
    return SeriesValues{47.0e-9, 4700.0, siliconSignalDiode(), seriesCount};
}

}  // namespace

// ---------------------------------------------------------------------------
// symmetricShuntClipper — prepare()-valid, counts, frozen vocab, port (SC-001).
// ---------------------------------------------------------------------------

TEST_CASE("symmetricShuntClipper - prepare()-valid topology with matching counts") {
    // The builder calls prepare() internally; a throw here is a build failure.
    CHECK_NOTHROW(symmetricShuntClipper(symValues()));

    const auto clip = symmetricShuntClipper(symValues());
    // Nodes: ground(0), input(1), shunt(2) = 3 (ground implicit, counted).
    CHECK(clip.netlist.nodeCount() == 3);
    // Components: Vin, R, two antiparallel diodes, Cf = 5.
    CHECK(clip.netlist.componentCount() == 5);
    CHECK(diodeCount(clip.netlist) == 2);
    CHECK(onlyFrozenClipperVocabulary(clip.netlist));

    // Port is the diode-string node pair (shunt node, ground); output ≡ shunt.
    CHECK(clip.portP == clip.outNode);
    CHECK(clip.portN == kGround);
    CHECK(everyDiodeSpansPort(clip.netlist, clip.portP, clip.portN));
    CHECK(clip.inNode != clip.outNode);

    // The defining property of a SYMMETRIC clipper is that the pair is genuinely
    // ANTIPARALLEL (one anode-up, one anode-down) — not two same-orientation
    // diodes (a parallel half-wave rectifier), which everyDiodeSpansPort alone
    // would also accept. Assert exactly one diode has anode==portP and exactly
    // one has cathode==portP (mirrors the up/down count of the asymmetric case).
    int anodeAtPort = 0, cathodeAtPort = 0;
    for (const Component& c : clip.netlist.components()) {
        if (const auto* dd = std::get_if<Diode>(&c)) {
            if (dd->anode == clip.portP) {
                ++anodeAtPort;
            }
            if (dd->cathode == clip.portP) {
                ++cathodeAtPort;
            }
        }
    }
    CHECK(anodeAtPort == 1);
    CHECK(cathodeAtPort == 1);
}

// ---------------------------------------------------------------------------
// asymmetricShuntClipper — unequal population, DC-offset topology (SC-001).
// ---------------------------------------------------------------------------

TEST_CASE("asymmetricShuntClipper - unequal 2-up/1-down population, matching counts") {
    CHECK_NOTHROW(asymmetricShuntClipper(asymValues()));

    const auto clip = asymmetricShuntClipper(asymValues());
    CHECK(clip.netlist.nodeCount() == 3);
    // Components: Vin, R, three diodes (2 up + 1 down), Cf = 6.
    CHECK(clip.netlist.componentCount() == 6);
    CHECK(diodeCount(clip.netlist) == 3);
    CHECK(onlyFrozenClipperVocabulary(clip.netlist));

    CHECK(clip.portP == clip.outNode);
    CHECK(clip.portN == kGround);
    CHECK(everyDiodeSpansPort(clip.netlist, clip.portP, clip.portN));

    // The population is genuinely unequal: count each orientation.
    int up = 0, down = 0;
    for (const Component& c : clip.netlist.components()) {
        if (const auto* d = std::get_if<Diode>(&c)) {
            if (d->anode == clip.portP) {
                ++up;
            } else {
                ++down;
            }
        }
    }
    CHECK(up == 2);
    CHECK(down == 1);
}

// ---------------------------------------------------------------------------
// seriesClipper — inline diodes, coupling cap, distinct port pair (SC-001).
// ---------------------------------------------------------------------------

TEST_CASE("seriesClipper - inline topology, port is the diode node pair") {
    CHECK_NOTHROW(seriesClipper(seriesValues(1)));

    const auto clip = seriesClipper(seriesValues(1));
    // Nodes: ground(0), input(1), post-cap(2), post-diode/output(3) = 4.
    CHECK(clip.netlist.nodeCount() == 4);
    // Components: Vin, Cc, one diode, R = 4.
    CHECK(clip.netlist.componentCount() == 4);
    CHECK(diodeCount(clip.netlist) == 1);
    CHECK(onlyFrozenClipperVocabulary(clip.netlist));

    // Series port is the inline-diode node pair (n1, n2); output ≡ n2 (== portN).
    CHECK(clip.portP != clip.portN);
    CHECK(clip.outNode == clip.portN);
    CHECK(clip.portN != kGround);
    CHECK(everyDiodeSpansPort(clip.netlist, clip.portP, clip.portN));
}

TEST_CASE("seriesClipper - seriesCount=2 stacks a second diode on the SAME port pair") {
    const auto clip = seriesClipper(seriesValues(2));
    CHECK(clip.netlist.componentCount() == 5);  // Vin, Cc, 2 diodes, R
    CHECK(diodeCount(clip.netlist) == 2);
    // Both diodes span the SAME (portP, portN) pair — no intermediate node. This
    // is the FR-012 single-nonlinearity-location scope, not a node-chain: the two
    // diodes sum their current at the one clipper port (as the antiparallel pair
    // does), which is exactly what the transient solver's single-port Newton
    // handles. A true series node-chain is out of scope (see everyDiodeSpansPort).
    CHECK(everyDiodeSpansPort(clip.netlist, clip.portP, clip.portN));
    CHECK(clip.netlist.nodeCount() == 4);  // ground, in, n1, n2 — no extra node added
}

// ---------------------------------------------------------------------------
// Fail-loud BOM validation (FR-007 / T009): descriptive std::invalid_argument,
// no silent clamp.
// ---------------------------------------------------------------------------

TEST_CASE("diode-clipper builders - invalid BOM input throws std::invalid_argument") {
    const DiodeSpec good = siliconSignalDiode();

    SUBCASE("non-positive resistor / capacitor") {
        CHECK_THROWS_AS(symmetricShuntClipper(SymmetricShuntValues{-1.0, 1e-9, good}),
                        std::invalid_argument);
        CHECK_THROWS_AS(symmetricShuntClipper(SymmetricShuntValues{1000.0, 0.0, good}),
                        std::invalid_argument);
        CHECK_THROWS_AS(seriesClipper(SeriesValues{0.0, 1000.0, good, 1}),
                        std::invalid_argument);
        CHECK_THROWS_AS(seriesClipper(SeriesValues{1e-8, -5.0, good, 1}),
                        std::invalid_argument);
    }

    SUBCASE("non-positive diode parameter") {
        CHECK_THROWS_AS(symmetricShuntClipper(
                            SymmetricShuntValues{1000.0, 1e-9, DiodeSpec{0.0, 1.0, 0.02585}}),
                        std::invalid_argument);
        CHECK_THROWS_AS(symmetricShuntClipper(
                            SymmetricShuntValues{1000.0, 1e-9, DiodeSpec{1e-14, -1.0, 0.02585}}),
                        std::invalid_argument);
        CHECK_THROWS_AS(symmetricShuntClipper(
                            SymmetricShuntValues{1000.0, 1e-9, DiodeSpec{1e-14, 1.0, 0.0}}),
                        std::invalid_argument);
    }

    SUBCASE("asymmetric population equal or out of range") {
        // upCount == downCount is the symmetric case, refused.
        CHECK_THROWS_AS(asymmetricShuntClipper(AsymmetricShuntValues{1000.0, 1e-9, good, 2, 2}),
                        std::invalid_argument);
        // Total population exceeds kMaxClipperDiodes (= 4).
        CHECK_THROWS_AS(asymmetricShuntClipper(AsymmetricShuntValues{1000.0, 1e-9, good, 4, 1}),
                        std::invalid_argument);
        // Zero total population.
        CHECK_THROWS_AS(asymmetricShuntClipper(AsymmetricShuntValues{1000.0, 1e-9, good, 0, 0}),
                        std::invalid_argument);
    }

    SUBCASE("series inline-diode count out of range") {
        CHECK_THROWS_AS(seriesClipper(SeriesValues{1e-8, 1000.0, good, 0}),
                        std::invalid_argument);
        CHECK_THROWS_AS(seriesClipper(SeriesValues{1e-8, 1000.0, good, 5}),
                        std::invalid_argument);
    }
}
