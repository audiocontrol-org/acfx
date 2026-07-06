#include <doctest/doctest.h>

#include <utility>
#include <variant>

#include "primitives/circuit/components.h"
#include "primitives/circuit/netlist.h"
#include "primitives/circuit/node.h"

// Tier-1 suite for the opamp-stage primitive.
//
// T004 — the OpAmp nullor vocabulary element: the one sanctioned extension of
// the frozen circuit/ vocabulary (opamp-element.md; research R1). These tests
// pin the additive element down at the vocabulary layer — construction, the
// three classifiers, terminalsOf's input-pair span, and participation in a
// Netlist — before any builder or solver is written. Builder/topology tests are
// appended by later tasks (see the marker at the end of this file).

using acfx::Component;
using acfx::Netlist;
using acfx::NodeId;
using acfx::OpAmp;
using acfx::Resistor;
using acfx::VoltageSource;
using acfx::contributesConductivePath;
using acfx::isLinear;
using acfx::isNonlinear;
using acfx::isReactive;
using acfx::terminalsOf;

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
