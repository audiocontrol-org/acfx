#pragma once

#include "primitives/circuit/opamp-stage/opamp-config.h"
#include "primitives/circuit/components.h"
#include "primitives/circuit/netlist.h"
#include "primitives/circuit/node.h"
#include "primitives/circuit/models/opamp.h"
#include "primitives/circuit/models/resistor.h"
#include "primitives/circuit/models/capacitor.h"
#include "primitives/circuit/models/diode.h"
#include "primitives/circuit/models/sources.h"

#include <stdexcept>
#include <string>

// opamp-stage.h — the four solver-neutral op-amp-stage builders (US1;
// contracts/opamp-stage-builder.md; data-model.md; research.md R3). Each
// builder is a PURE function composing the frozen component-abstractions
// vocabulary PLUS the one sanctioned extension (OpAmp, models/opamp.h) into
// an op-amp-stage TOPOLOGY: a fixed-capacity Netlist plus the input/output
// node handles. TOPOLOGY ONLY — no solved response, no process()/audio path
// (contract guarantee 1).
//
// PORTABLE PRIMITIVE (Constitution IV): C++17, header-only, RT-safe,
// heap-free (the returned Netlist is a stack std::array; no new/delete/
// std::vector anywhere below). Includes NOTHING under core/labs/ (contract
// guarantee 5) — the isolation check is that this translation unit compiles
// and links using only the frozen circuit vocabulary + opamp-config.h.
// Mirrors core/primitives/circuit/diode-clipper/diode-clipper.h's style
// closely (sibling primitive).
//
// DRIVING THE INPUT. Each builder bakes the input stimulus as a grounded
// ideal VoltageSource whose value is the BOM's `vin` field. A time-varying or
// swept excitation is expressed by REBUILDING the netlist with a new BOM —
// the sanctioned control-rate-rebuild pattern (mirrors diode-clipper.h).
//
// FAIL LOUD (Constitution V; contract "Errors"): non-positive R/C, a
// non-positive diode parameter, an out-of-range diode population, a floating
// op-amp input, or a missing feedback path all raise a descriptive
// std::invalid_argument on the build thread. No silent clamp, no fallback.
// NOTE on the latter two failure modes: in all four fixed topologies below,
// every op-amp input is ALWAYS wired (to the driving source, to the feedback
// network, or hard-tied to ground) and the feedback resistor is ALWAYS
// instantiated as part of the topology itself — so "floating input" /
// "missing feedback path" cannot arise from a well-typed BOM; they are ruled
// out BY CONSTRUCTION rather than by a runtime scan. What IS checked at
// runtime is exactly the set of scalar preconditions that a caller can
// actually violate: strictly-positive R/C, valid diode parameters, and a
// diode population within the per-topology capacity.

namespace acfx {

// ---------------------------------------------------------------------------
// nonInvertingGain (contract row 1; data-model.md "NonInvertingGainBom") —
// Vin -> inPlus; OpAmp{inPlus,inMinus,out}; Rf out->inMinus;
// Rg inMinus->gnd. Closed-loop gain 1 + Rf/Rg.
// ---------------------------------------------------------------------------
inline NonInvertingGainResult nonInvertingGain(const NonInvertingGainBom& bom) {
    detail::requirePositive(bom.Rf, "nonInvertingGain Rf");
    detail::requirePositive(bom.Rg, "nonInvertingGain Rg");

    NonInvertingGainResult result{};
    Netlist<4, 4>& net = result.netlist;

    const NodeId inPlus = net.addNode();   // 1: driven non-inverting input
    const NodeId inMinus = net.addNode();  // 2: inverting input / feedback summing node
    const NodeId out = net.addNode();      // 3: op-amp output

    net.add(VoltageSource{inPlus, kGround, bom.vin});
    net.add(OpAmp{inPlus, inMinus, out});
    net.add(Resistor{out, inMinus, bom.Rf});     // Rf: out -> inMinus
    net.add(Resistor{inMinus, kGround, bom.Rg}); // Rg: inMinus -> gnd
    net.prepare();

    result.inNode = inPlus;
    result.outNode = out;
    return result;
}

// ---------------------------------------------------------------------------
// invertingGain (contract row 2; data-model.md "InvertingGainBom") —
// Vin -> Rin -> inMinus; inPlus -> gnd (tied directly, no extra node);
// Rf out->inMinus. Gain -Rf/Rin.
// ---------------------------------------------------------------------------
inline InvertingGainResult invertingGain(const InvertingGainBom& bom) {
    detail::requirePositive(bom.Rin, "invertingGain Rin");
    detail::requirePositive(bom.Rf, "invertingGain Rf");

    InvertingGainResult result{};
    Netlist<4, 4>& net = result.netlist;

    const NodeId vinNode = net.addNode();  // 1: driven input node
    const NodeId inMinus = net.addNode();  // 2: inverting input / summing node
    const NodeId out = net.addNode();      // 3: op-amp output

    net.add(VoltageSource{vinNode, kGround, bom.vin});
    net.add(Resistor{vinNode, inMinus, bom.Rin}); // Rin: vin -> inMinus
    net.add(OpAmp{kGround, inMinus, out});        // inPlus tied to gnd
    net.add(Resistor{out, inMinus, bom.Rf});      // Rf: out -> inMinus
    net.prepare();

    result.inNode = vinNode;
    result.outNode = out;
    return result;
}

// ---------------------------------------------------------------------------
// activeFirstOrder (contract row 3; data-model.md "ActiveFirstOrderBom") —
// inverting first-order LOW-PASS: Vin -> Rin -> inMinus; inPlus -> gnd;
// feedback Rf AND Cf BOTH out->inMinus (parallel). DC gain -Rf/Rin, corner
// 1/(2*pi*Rf*Cf).
// ---------------------------------------------------------------------------
inline ActiveFirstOrderResult activeFirstOrder(const ActiveFirstOrderBom& bom) {
    detail::requirePositive(bom.Rin, "activeFirstOrder Rin");
    detail::requirePositive(bom.Rf, "activeFirstOrder Rf");
    detail::requirePositive(bom.Cf, "activeFirstOrder Cf");

    ActiveFirstOrderResult result{};
    Netlist<4, 5>& net = result.netlist;

    const NodeId vinNode = net.addNode();  // 1: driven input node
    const NodeId inMinus = net.addNode();  // 2: inverting input / summing node
    const NodeId out = net.addNode();      // 3: op-amp output

    net.add(VoltageSource{vinNode, kGround, bom.vin});
    net.add(Resistor{vinNode, inMinus, bom.Rin}); // Rin: vin -> inMinus
    net.add(OpAmp{kGround, inMinus, out});        // inPlus tied to gnd
    net.add(Resistor{out, inMinus, bom.Rf});      // Rf: out -> inMinus (parallel leg 1)
    net.add(Capacitor{out, inMinus, bom.Cf});     // Cf: out -> inMinus (parallel leg 2)
    net.prepare();

    result.inNode = vinNode;
    result.outNode = out;
    return result;
}

// ---------------------------------------------------------------------------
// opAmpDiodeClipper (contract row 4; data-model.md "OpAmpDiodeClipperBom") —
// the TS808 core: Vin -> Rin -> inMinus; inPlus -> gnd; feedback network
// out->inMinus = Rf || Cf || antiparallel diode string (nUp Diode{out,inMinus}
// + nDown Diode{inMinus,out}). Exactly one nonlinearity LOCATION (the
// feedback diode pair spans the single (out, inMinus) node pair, however many
// diodes populate each direction).
// ---------------------------------------------------------------------------
inline OpAmpDiodeClipperResult opAmpDiodeClipper(const OpAmpDiodeClipperBom& bom) {
    detail::requirePositive(bom.Rin, "opAmpDiodeClipper Rin");
    detail::requirePositive(bom.Rf, "opAmpDiodeClipper Rf");
    detail::requirePositive(bom.Cf, "opAmpDiodeClipper Cf");
    detail::requireValidDiode(bom.diode);
    detail::requireDiodeCount(bom.nUp, "opAmpDiodeClipper nUp");
    detail::requireDiodeCount(bom.nDown, "opAmpDiodeClipper nDown");
    detail::requireDiodePopulation(bom.nUp + bom.nDown,
                                   "opAmpDiodeClipper nUp+nDown");

    OpAmpDiodeClipperResult result{};
    Netlist<4, 5 + kMaxOpAmpClipperDiodes>& net = result.netlist;

    const NodeId vinNode = net.addNode();  // 1: driven input node
    const NodeId inMinus = net.addNode();  // 2: inverting input / summing node
    const NodeId out = net.addNode();      // 3: op-amp output

    net.add(VoltageSource{vinNode, kGround, bom.vin});
    net.add(Resistor{vinNode, inMinus, bom.Rin}); // Rin: vin -> inMinus
    net.add(OpAmp{kGround, inMinus, out});        // inPlus tied to gnd
    net.add(Resistor{out, inMinus, bom.Rf});      // Rf: clean-gain floor
    net.add(Capacitor{out, inMinus, bom.Cf});     // Cf: feedback filter cap
    for (int i = 0; i < bom.nUp; ++i) {
        net.add(Diode{out, inMinus, bom.diode.Is, bom.diode.n, bom.diode.Vt});
    }
    for (int i = 0; i < bom.nDown; ++i) {
        net.add(Diode{inMinus, out, bom.diode.Is, bom.diode.n, bom.diode.Vt});
    }
    net.prepare();

    result.inNode = vinNode;
    result.outNode = out;
    return result;
}

} // namespace acfx
