#pragma once

#include "primitives/circuit/diode-clipper/clipper-config.h"
#include "primitives/circuit/components.h"
#include "primitives/circuit/netlist.h"
#include "primitives/circuit/node.h"
#include "primitives/circuit/models/diode.h"
#include "primitives/circuit/models/resistor.h"
#include "primitives/circuit/models/capacitor.h"
#include "primitives/circuit/models/sources.h"

#include <stdexcept>
#include <string>

// diode-clipper.h — the solver-neutral diode-clipper builders (US1;
// contracts/diode-clipper-builder.md; FR-001..007). Three pure functions compose
// the frozen component-abstractions vocabulary (Resistor / Capacitor / Diode /
// VoltageSource / Netlist) into a diode clipping-stage TOPOLOGY: a fixed-capacity
// Netlist plus the input/output/port node handles. TOPOLOGY ONLY — no solved
// response, no process()/audio path (FR-001).
//
// PORTABLE PRIMITIVE (Constitution IV): C++17, header-only, RT-safe, heap-free
// (the returned Netlist is a stack std::array). Includes NOTHING under
// core/labs/ (FR-004/FR-019); the frozen circuit vocabulary is consumed
// unchanged.
//
// DRIVING THE INPUT (data-model.md "State & lifecycle"; FR-005). Each builder
// bakes the input stimulus as a grounded ideal VoltageSource at `inNode`. The
// source value is the optional trailing argument `vIn` (default 0 V); a
// time-varying or swept excitation is expressed by REBUILDING the netlist with a
// new `vIn` — the sanctioned control-rate-rebuild pattern (FR-005). The passive
// clipping stage's "drive" gain is upstream and out of scope (spec Assumptions);
// `vIn` here is only the instantaneous input node voltage the solver reads. The
// documented one-argument call `symmetricShuntClipper(values)` stays valid (vIn
// defaults to 0).
//
// FAIL LOUD (Constitution V; FR-007): non-positive R/C, a non-positive diode
// parameter, or an out-of-range diode population raises a descriptive
// std::invalid_argument on the build thread. No silent clamp, no fallback.

namespace acfx {

namespace detail {

// A diode of the given spec between anode and cathode. Factored out so every
// builder stamps identical Shockley parameters from one DiodeSpec.
inline Diode diodeOf(const DiodeSpec& d, NodeId anode, NodeId cathode) {
    return Diode{anode, cathode, d.Is, d.n, d.Vt};
}

// Require a non-negative diode direction count (an individual up/down leg).
// Negative populations are a caller error, never clamped to zero.
inline void requireNonNegative(int count, const char* field) {
    if (count < 0) {
        throw std::invalid_argument(
            std::string("diode-clipper builder: ") + field +
            " must be >= 0 (got " + std::to_string(count) + ")");
    }
}

}  // namespace detail

// ---------------------------------------------------------------------------
// symmetricShuntClipper (FR-001) — Vin → series R → n1; matched antiparallel
// Diode pair Diode{n1,gnd} ∥ Diode{gnd,n1}; filter Cf n1 → gnd. Output n1; port
// (n1, gnd). Odd-symmetric transfer.
// ---------------------------------------------------------------------------
inline SymmetricShuntClipper symmetricShuntClipper(const SymmetricShuntValues& v,
                                                   double vIn = 0.0) {
    detail::requirePositive(v.R, "symmetricShuntClipper R");
    detail::requirePositive(v.Cf, "symmetricShuntClipper Cf");
    detail::requireValidDiode(v.diode);

    SymmetricShuntClipper c{};
    const NodeId nIn = c.netlist.addNode();  // 1: driven input node
    const NodeId n1 = c.netlist.addNode();   // 2: shunt / output node

    c.netlist.add(VoltageSource{nIn, kGround, vIn});
    c.netlist.add(Resistor{nIn, n1, v.R});
    c.netlist.add(detail::diodeOf(v.diode, n1, kGround));  // forward leg
    c.netlist.add(detail::diodeOf(v.diode, kGround, n1));  // reverse leg
    c.netlist.add(Capacitor{n1, kGround, v.Cf});
    c.netlist.prepare();

    c.inNode = nIn;
    c.outNode = n1;
    c.portP = n1;
    c.portN = kGround;
    return c;
}

// ---------------------------------------------------------------------------
// asymmetricShuntClipper (FR-002) — series R → n1; `upCount` Diode{n1,gnd} +
// `downCount` Diode{gnd,n1}; filter Cf across. Non-odd transfer (DC offset).
// Requires upCount != downCount (equal is the symmetric case) and a total
// population in [1, kMaxClipperDiodes]. Output n1; port (n1, gnd).
// ---------------------------------------------------------------------------
inline AsymmetricShuntClipper asymmetricShuntClipper(const AsymmetricShuntValues& v,
                                                     double vIn = 0.0) {
    detail::requirePositive(v.R, "asymmetricShuntClipper R");
    detail::requirePositive(v.Cf, "asymmetricShuntClipper Cf");
    detail::requireValidDiode(v.diode);
    detail::requireNonNegative(v.upCount, "asymmetricShuntClipper upCount");
    detail::requireNonNegative(v.downCount, "asymmetricShuntClipper downCount");
    detail::requireDiodePopulation(v.upCount + v.downCount,
                                   "asymmetricShuntClipper upCount+downCount");
    if (v.upCount == v.downCount) {
        throw std::invalid_argument(
            "asymmetricShuntClipper: upCount == downCount is the symmetric case "
            "(use symmetricShuntClipper)");
    }

    AsymmetricShuntClipper c{};
    const NodeId nIn = c.netlist.addNode();  // 1: driven input node
    const NodeId n1 = c.netlist.addNode();   // 2: shunt / output node

    c.netlist.add(VoltageSource{nIn, kGround, vIn});
    c.netlist.add(Resistor{nIn, n1, v.R});
    for (int i = 0; i < v.upCount; ++i) {
        c.netlist.add(detail::diodeOf(v.diode, n1, kGround));  // anode → shunt
    }
    for (int i = 0; i < v.downCount; ++i) {
        c.netlist.add(detail::diodeOf(v.diode, kGround, n1));  // reversed
    }
    c.netlist.add(Capacitor{n1, kGround, v.Cf});
    c.netlist.prepare();

    c.inNode = nIn;
    c.outNode = n1;
    c.portP = n1;
    c.portN = kGround;
    return c;
}

// ---------------------------------------------------------------------------
// seriesClipper (FR-003) — Vin → input coupling Cc (series) → n1; `seriesCount`
// inline Diode{n1,n2}; R n2 → gnd. The coupling cap blocks DC (output → 0 at
// DC). Output n2; port (n1, n2). "Series" here means the diodes sit in the
// series SIGNAL PATH (ahead of the shunt-to-ground R), NOT a node-chain of
// diodes with intermediate nodes: all `seriesCount` diodes span the SAME port
// node pair (n1, n2), summing their current at that single nonlinearity
// location (FR-012). A node-chain would create distinct node pairs — a second
// interacting nonlinearity the bounded transient solver deliberately refuses
// (deferred to Phase 5).
// ---------------------------------------------------------------------------
inline SeriesClipper seriesClipper(const SeriesValues& v, double vIn = 0.0) {
    detail::requirePositive(v.Cc, "seriesClipper Cc");
    detail::requirePositive(v.R, "seriesClipper R");
    detail::requireValidDiode(v.diode);
    detail::requireDiodePopulation(v.seriesCount, "seriesClipper seriesCount");

    SeriesClipper c{};
    const NodeId nIn = c.netlist.addNode();  // 1: driven input node
    const NodeId n1 = c.netlist.addNode();   // 2: post-coupling-cap node
    const NodeId n2 = c.netlist.addNode();   // 3: post-diode / output node

    c.netlist.add(VoltageSource{nIn, kGround, vIn});
    c.netlist.add(Capacitor{nIn, n1, v.Cc});
    for (int i = 0; i < v.seriesCount; ++i) {
        c.netlist.add(detail::diodeOf(v.diode, n1, n2));  // inline, port (n1,n2)
    }
    c.netlist.add(Resistor{n2, kGround, v.R});
    c.netlist.prepare();

    c.inNode = nIn;
    c.outNode = n2;
    c.portP = n1;
    c.portN = n2;
    return c;
}

}  // namespace acfx
