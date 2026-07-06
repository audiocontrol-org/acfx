#include <doctest/doctest.h>

#include <stdexcept>
#include <utility>
#include <variant>

#include "primitives/circuit/components.h"
#include "primitives/circuit/netlist.h"
#include "primitives/circuit/node.h"
#include "primitives/circuit/opamp-stage/opamp-config.h"
#include "primitives/circuit/opamp-stage/opamp-stage.h"

// Tier-1 suite for the opamp-stage primitive.
//
// T004 — the OpAmp nullor vocabulary element: the one sanctioned extension of
// the frozen circuit/ vocabulary (opamp-element.md; research R1). These tests
// pin the additive element down at the vocabulary layer — construction, the
// three classifiers, terminalsOf's input-pair span, and participation in a
// Netlist — before any builder or solver is written. Builder/topology tests are
// appended by later tasks (see the marker at the end of this file).

using acfx::Capacitor;
using acfx::Component;
using acfx::Diode;
using acfx::opamp_stage::DiodeSpec;
using acfx::Netlist;
using acfx::NodeId;
using acfx::OpAmp;
using acfx::Resistor;
using acfx::VoltageSource;
using acfx::contributesConductivePath;
using acfx::kGround;
using acfx::isLinear;
using acfx::isNonlinear;
using acfx::isReactive;
using acfx::terminalsOf;

using acfx::ActiveFirstOrderBom;
using acfx::InvertingGainBom;
using acfx::NonInvertingGainBom;
using acfx::OpAmpDiodeClipperBom;
using acfx::activeFirstOrder;
using acfx::invertingGain;
using acfx::kMaxOpAmpClipperDiodes;
using acfx::nonInvertingGain;
using acfx::opAmpDiodeClipper;
using acfx::opamp_stage::siliconSignalDiode;

// ---------------------------------------------------------------------------
// T004 — the OpAmp element is constructible, classifies as linear, and reports
// its input pair as its constraint span (opamp-element.md guarantees 1-4).
// ---------------------------------------------------------------------------

TEST_CASE("opamp element - construction, classifiers, and terminals") {
    // An OpAmp can be constructed and held in a Component variant.
    const OpAmp op{1, 2, 3}; // inPlus=1, inMinus=2, out=3
    const Component c = op;
    CHECK(std::holds_alternative<OpAmp>(c));

    // Classifiers: the ideal op-amp is linear — neither nonlinear nor reactive
    // (it lands in the same routing bucket as the sources).
    CHECK(isLinear(c));
    CHECK_FALSE(isNonlinear(c));
    CHECK_FALSE(isReactive(c));

    // terminalsOf reports the INPUT pair (the virtual-short constraint span);
    // the norator-driven output is NOT surfaced as a passive two-terminal edge.
    const std::pair<NodeId, NodeId> t = terminalsOf(c);
    CHECK(t.first == op.inPlus);
    CHECK(t.second == op.inMinus);
    CHECK(t.first != op.out);
    CHECK(t.second != op.out);

    // The op-amp output is EXCLUDED from the conductive-path pre-filter
    // (mirrors CurrentSource/Diode; the feedback network provides reachability).
    CHECK_FALSE(contributesConductivePath(c));
}

// ---------------------------------------------------------------------------
// T004 — an OpAmp participates in a Netlist: it is held by value and reported
// by componentCount(), and its input pair drives prepare()'s reachability scan
// alongside a resistive feedback path to ground.
// ---------------------------------------------------------------------------

TEST_CASE("opamp element - participates in a Netlist") {
    // A minimal non-inverting-style skeleton: a source pins inPlus, the op-amp
    // spans {inPlus, inMinus}, and resistors give inMinus a conductive path to
    // ground so the conservative pre-filter passes. (This is a vocabulary-level
    // participation check, not a builder/solve assertion — those are T006+.)
    Netlist<8, 8> net;
    const NodeId inPlus = net.addNode();  // 1
    const NodeId inMinus = net.addNode(); // 2
    const NodeId out = net.addNode();     // 3

    net.add(VoltageSource{inPlus, acfx::kGround, 1.0}); // drive + reference ground
    net.add(OpAmp{inPlus, inMinus, out});
    net.add(Resistor{out, inMinus, 10000.0}); // Rf: out -> inMinus
    net.add(Resistor{inMinus, acfx::kGround, 10000.0}); // Rg: inMinus -> ground

    CHECK(net.componentCount() == 4);
    CHECK(net.nodeCount() == 4); // ground + 3

    // The OpAmp is held by value in the component view.
    bool sawOpAmp = false;
    for (const Component& c : net.components()) {
        if (std::holds_alternative<OpAmp>(c)) {
            sawOpAmp = true;
            CHECK(terminalsOf(c).first == inPlus);
            CHECK(terminalsOf(c).second == inMinus);
        }
    }
    CHECK(sawOpAmp);

    // prepare() validates topology without throwing: every interior node has a
    // conductive path to ground through the resistors / source.
    CHECK_NOTHROW(net.prepare());
}

// --- builder tests appended by T006/US1 below ---
//
// T006 (US1) — Tier-1 builder-topology suite for the four op-amp-stage
// builders (contracts/opamp-stage-builder.md; research.md R3;
// data-model.md). Written test-first: at the point this block landed, the
// builders below did not yet exist / were stubs, so these TEST_CASEs were
// RED before opamp-stage.h implemented them (T007-T010), then GREEN after.
//
// ISOLATION (contract guarantee 5, FR-024): this translation unit includes
// only the opamp-stage primitive and the frozen circuit vocabulary; it
// includes nothing under core/labs/, and it links without any lab code. That
// the builders compile and run here is itself a compile-time isolation
// signal — the exhaustive grep-based isolation gate over every primitive
// header is a separate, later task (T025) and is NOT duplicated here.

namespace {

// Every component must be a member of the op-amp-stage vocabulary: the six
// frozen elements plus the one sanctioned extension, OpAmp (FR-001/opamp-
// element.md) — no other element type.
template <int N, int M>
bool onlyOpAmpStageVocabulary(const acfx::Netlist<N, M>& nl) {
    for (const Component& c : nl.components()) {
        const bool ok = std::holds_alternative<Resistor>(c) ||
                        std::holds_alternative<Capacitor>(c) ||
                        std::holds_alternative<Diode>(c) ||
                        std::holds_alternative<VoltageSource>(c) ||
                        std::holds_alternative<OpAmp>(c);
        if (!ok) {
            return false;
        }
    }
    return true;
}

// Count the OpAmp elements in a netlist. Every stage must carry EXACTLY ONE.
template <int N, int M>
int opAmpCount(const acfx::Netlist<N, M>& nl) {
    int count = 0;
    for (const Component& c : nl.components()) {
        if (std::holds_alternative<OpAmp>(c)) {
            ++count;
        }
    }
    return count;
}

// Count the Diode elements in a netlist.
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

// Count the Capacitor elements in a netlist.
template <int N, int M>
int capacitorCount(const acfx::Netlist<N, M>& nl) {
    int count = 0;
    for (const Component& c : nl.components()) {
        if (std::holds_alternative<Capacitor>(c)) {
            ++count;
        }
    }
    return count;
}

// Find the sole OpAmp in a netlist. Callers assert opAmpCount(nl) == 1 first.
template <int N, int M>
OpAmp theOpAmp(const acfx::Netlist<N, M>& nl) {
    for (const Component& c : nl.components()) {
        if (const auto* op = std::get_if<OpAmp>(&c)) {
            return *op;
        }
    }
    FAIL("theOpAmp: no OpAmp found in netlist");
    return OpAmp{};
}

}  // namespace

// ---------------------------------------------------------------------------
// nonInvertingGain — Vin->inPlus; OpAmp; Rf out->inMinus; Rg inMinus->gnd.
// ---------------------------------------------------------------------------

TEST_CASE("nonInvertingGain - prepare()-valid topology with matching counts") {
    const NonInvertingGainBom bom{10000.0, 10000.0, 1.0}; // gain = 1 + Rf/Rg = 2

    CHECK_NOTHROW(nonInvertingGain(bom));

    const auto stage = nonInvertingGain(bom);
    // Nodes: ground(0), inPlus(1), inMinus(2), out(3) = 4.
    CHECK(stage.netlist.nodeCount() == 4);
    // Components: Vin, OpAmp, Rf, Rg = 4.
    CHECK(stage.netlist.componentCount() == 4);
    CHECK(opAmpCount(stage.netlist) == 1);
    CHECK(onlyOpAmpStageVocabulary(stage.netlist));

    const OpAmp op = theOpAmp(stage.netlist);
    CHECK(op.inPlus == stage.inNode);
    CHECK(op.out == stage.outNode);
    CHECK(op.inMinus != stage.inNode);
    CHECK(op.inMinus != stage.outNode);
    CHECK(stage.inNode != stage.outNode);
}

// ---------------------------------------------------------------------------
// invertingGain — Vin->Rin->inMinus; inPlus->gnd; Rf out->inMinus.
// ---------------------------------------------------------------------------

TEST_CASE("invertingGain - prepare()-valid topology with matching counts") {
    const InvertingGainBom bom{1000.0, 10000.0, 1.0}; // gain = -Rf/Rin = -10

    CHECK_NOTHROW(invertingGain(bom));

    const auto stage = invertingGain(bom);
    // Nodes: ground(0), vin(1), inMinus(2), out(3) = 4 (inPlus ties to gnd).
    CHECK(stage.netlist.nodeCount() == 4);
    // Components: Vin, Rin, OpAmp, Rf = 4.
    CHECK(stage.netlist.componentCount() == 4);
    CHECK(opAmpCount(stage.netlist) == 1);
    CHECK(onlyOpAmpStageVocabulary(stage.netlist));

    const OpAmp op = theOpAmp(stage.netlist);
    CHECK(op.inPlus == kGround);
    CHECK(op.out == stage.outNode);
    CHECK(stage.inNode != stage.outNode);
    CHECK(stage.inNode != kGround);
}

// ---------------------------------------------------------------------------
// activeFirstOrder — Vin->Rin->inMinus; inPlus->gnd; Cf || Rf out->inMinus.
// ---------------------------------------------------------------------------

TEST_CASE("activeFirstOrder - prepare()-valid topology with matching counts") {
    // DC gain -Rf/Rin = -10; corner 1/(2*pi*Rf*Cf).
    const ActiveFirstOrderBom bom{1000.0, 10000.0, 1.0e-9, 1.0};

    CHECK_NOTHROW(activeFirstOrder(bom));

    const auto stage = activeFirstOrder(bom);
    // Same 4 nodes as invertingGain.
    CHECK(stage.netlist.nodeCount() == 4);
    // Components: Vin, Rin, OpAmp, Rf, Cf = 5.
    CHECK(stage.netlist.componentCount() == 5);
    CHECK(opAmpCount(stage.netlist) == 1);
    CHECK(capacitorCount(stage.netlist) == 1);
    CHECK(onlyOpAmpStageVocabulary(stage.netlist));

    const OpAmp op = theOpAmp(stage.netlist);
    CHECK(op.inPlus == kGround);
    CHECK(op.out == stage.outNode);

    // Rf and Cf are BOTH in the feedback path spanning (out, inMinus).
    bool sawFeedbackR = false, sawFeedbackC = false;
    for (const Component& c : stage.netlist.components()) {
        if (const auto* r = std::get_if<Resistor>(&c)) {
            if ((r->a == stage.outNode && r->b == op.inMinus) ||
                (r->b == stage.outNode && r->a == op.inMinus)) {
                sawFeedbackR = true;
            }
        }
        if (const auto* cap = std::get_if<Capacitor>(&c)) {
            if ((cap->a == stage.outNode && cap->b == op.inMinus) ||
                (cap->b == stage.outNode && cap->a == op.inMinus)) {
                sawFeedbackC = true;
            }
        }
    }
    CHECK(sawFeedbackR);
    CHECK(sawFeedbackC);
}

// ---------------------------------------------------------------------------
// opAmpDiodeClipper — Vin->Rin->inMinus; inPlus->gnd; feedback network
// out->inMinus = Rf || Cf || antiparallel diode string.
// ---------------------------------------------------------------------------

TEST_CASE("opAmpDiodeClipper - prepare()-valid topology with matching counts") {
    const OpAmpDiodeClipperBom bom{
        1000.0, 10000.0, 1.0e-9, siliconSignalDiode(), 1, 1, 1.0};

    CHECK_NOTHROW(opAmpDiodeClipper(bom));

    const auto stage = opAmpDiodeClipper(bom);
    // Same 4 nodes as invertingGain (the diode string spans existing nodes).
    CHECK(stage.netlist.nodeCount() == 4);
    // Components: Vin, Rin, OpAmp, Rf, Cf, 1 up-diode, 1 down-diode = 7.
    CHECK(stage.netlist.componentCount() == 7);
    CHECK(opAmpCount(stage.netlist) == 1);
    CHECK(capacitorCount(stage.netlist) == 1);
    CHECK(diodeCount(stage.netlist) == 2);
    CHECK(onlyOpAmpStageVocabulary(stage.netlist));

    const OpAmp op = theOpAmp(stage.netlist);
    CHECK(op.inPlus == kGround);
    CHECK(op.out == stage.outNode);

    // Exactly one nonlinearity LOCATION: every diode spans the single
    // (out, inMinus) node pair, in either orientation.
    for (const Component& c : stage.netlist.components()) {
        if (const auto* d = std::get_if<Diode>(&c)) {
            const bool fwd = d->anode == stage.outNode && d->cathode == op.inMinus;
            const bool rev = d->anode == op.inMinus && d->cathode == stage.outNode;
            CHECK((fwd || rev));
        }
    }
}

TEST_CASE("opAmpDiodeClipper - asymmetric population (DC-offset topology)") {
    const OpAmpDiodeClipperBom bom{
        1000.0, 10000.0, 1.0e-9, siliconSignalDiode(), 2, 1, 1.0};

    const auto stage = opAmpDiodeClipper(bom);
    CHECK(stage.netlist.componentCount() == 8); // 5 fixed + 3 diodes
    CHECK(diodeCount(stage.netlist) == 3);

    const OpAmp op = theOpAmp(stage.netlist);
    int up = 0, down = 0;
    for (const Component& c : stage.netlist.components()) {
        if (const auto* d = std::get_if<Diode>(&c)) {
            // Classify strictly by the (out, inMinus) port and orientation —
            // a stray diode on any OTHER node pair must not be silently
            // counted as "down" by a bare anode-mismatch check.
            const bool isUp = d->anode == stage.outNode && d->cathode == op.inMinus;
            const bool isDown = d->anode == op.inMinus && d->cathode == stage.outNode;
            CHECK((isUp || isDown));  // every diode lies on the (out, inMinus) port
            if (isUp) {
                ++up;
            } else if (isDown) {
                ++down;
            }
        }
    }
    CHECK(up == 2);
    CHECK(down == 1);
}

// ---------------------------------------------------------------------------
// Fail-loud BOM validation (T011): descriptive std::invalid_argument, no
// silent clamp, for every non-positive R/C, invalid diode parameter, or
// out-of-range diode population.
// ---------------------------------------------------------------------------

TEST_CASE("nonInvertingGain - invalid BOM throws std::invalid_argument naming the bad field") {
    CHECK_THROWS_WITH_AS(nonInvertingGain(NonInvertingGainBom{-1.0, 10000.0, 1.0}),
                         doctest::Contains("nonInvertingGain Rf"), std::invalid_argument);
    CHECK_THROWS_WITH_AS(nonInvertingGain(NonInvertingGainBom{10000.0, 0.0, 1.0}),
                         doctest::Contains("nonInvertingGain Rg"), std::invalid_argument);
}

TEST_CASE("invertingGain - invalid BOM throws std::invalid_argument naming the bad field") {
    CHECK_THROWS_WITH_AS(invertingGain(InvertingGainBom{0.0, 10000.0, 1.0}),
                         doctest::Contains("invertingGain Rin"), std::invalid_argument);
    CHECK_THROWS_WITH_AS(invertingGain(InvertingGainBom{1000.0, -10000.0, 1.0}),
                         doctest::Contains("invertingGain Rf"), std::invalid_argument);
}

TEST_CASE("activeFirstOrder - invalid BOM throws std::invalid_argument naming the bad field") {
    CHECK_THROWS_WITH_AS(activeFirstOrder(ActiveFirstOrderBom{-1000.0, 10000.0, 1e-9, 1.0}),
                         doctest::Contains("activeFirstOrder Rin"), std::invalid_argument);
    CHECK_THROWS_WITH_AS(activeFirstOrder(ActiveFirstOrderBom{1000.0, 0.0, 1e-9, 1.0}),
                         doctest::Contains("activeFirstOrder Rf"), std::invalid_argument);
    CHECK_THROWS_WITH_AS(activeFirstOrder(ActiveFirstOrderBom{1000.0, 10000.0, 0.0, 1.0}),
                         doctest::Contains("activeFirstOrder Cf"), std::invalid_argument);
}

TEST_CASE("opAmpDiodeClipper - invalid BOM throws std::invalid_argument naming the bad field/bound") {
    const DiodeSpec good = siliconSignalDiode();

    SUBCASE("non-positive R/C") {
        CHECK_THROWS_WITH_AS(
            opAmpDiodeClipper(OpAmpDiodeClipperBom{0.0, 10000.0, 1e-9, good, 1, 1, 1.0}),
            doctest::Contains("opAmpDiodeClipper Rin"), std::invalid_argument);
        CHECK_THROWS_WITH_AS(
            opAmpDiodeClipper(OpAmpDiodeClipperBom{1000.0, -1.0, 1e-9, good, 1, 1, 1.0}),
            doctest::Contains("opAmpDiodeClipper Rf"), std::invalid_argument);
        CHECK_THROWS_WITH_AS(
            opAmpDiodeClipper(OpAmpDiodeClipperBom{1000.0, 10000.0, 0.0, good, 1, 1, 1.0}),
            doctest::Contains("opAmpDiodeClipper Cf"), std::invalid_argument);
    }

    SUBCASE("non-positive diode parameter") {
        CHECK_THROWS_WITH_AS(
            opAmpDiodeClipper(OpAmpDiodeClipperBom{
                1000.0, 10000.0, 1e-9, DiodeSpec{0.0, 1.0, 0.02585}, 1, 1, 1.0}),
            doctest::Contains("diode Is"), std::invalid_argument);
        CHECK_THROWS_WITH_AS(
            opAmpDiodeClipper(OpAmpDiodeClipperBom{
                1000.0, 10000.0, 1e-9, DiodeSpec{1e-14, -1.0, 0.02585}, 1, 1, 1.0}),
            doctest::Contains("diode n"), std::invalid_argument);
        CHECK_THROWS_WITH_AS(
            opAmpDiodeClipper(OpAmpDiodeClipperBom{
                1000.0, 10000.0, 1e-9, DiodeSpec{1e-14, 1.0, 0.0}, 1, 1, 1.0}),
            doctest::Contains("diode Vt"), std::invalid_argument);
    }

    SUBCASE("diode count out of range") {
        // nUp / nDown must each be >= 1.
        CHECK_THROWS_WITH_AS(
            opAmpDiodeClipper(OpAmpDiodeClipperBom{1000.0, 10000.0, 1e-9, good, 0, 1, 1.0}),
            doctest::Contains("opAmpDiodeClipper nUp"), std::invalid_argument);
        CHECK_THROWS_WITH_AS(
            opAmpDiodeClipper(OpAmpDiodeClipperBom{1000.0, 10000.0, 1e-9, good, 1, 0, 1.0}),
            doctest::Contains("opAmpDiodeClipper nDown"), std::invalid_argument);
        // Total population exceeds kMaxOpAmpClipperDiodes (= 4).
        CHECK_THROWS_WITH_AS(
            opAmpDiodeClipper(
                OpAmpDiodeClipperBom{1000.0, 10000.0, 1e-9, good, 3, 2, 1.0}),
            doctest::Contains("exceeds kMaxOpAmpClipperDiodes"), std::invalid_argument);
    }
}
