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
// seriesClipper (FR-003) — Vin → input coupling Cc (series) → n1; a single inline
// Diode{n1,n2}; R n2 → gnd. The coupling cap blocks DC (output → 0 at DC). Output
// n2; port (n1, n2). "Series" means the diode sits in the series SIGNAL PATH
// (ahead of the shunt-to-ground R), distinct from the shunt family.
//
// v1 SCOPE — a single inline diode (seriesCount == 1). A multi-diode series
// STRING is deliberately deferred for a load-bearing reason, not an oversight:
//   - A TRUE series chain (n1 → D → nMid → D → n2) has a diode-only intermediate
//     node nMid. The frozen Netlist::prepare() floating-node rule excludes diodes
//     from conductive paths (a lone diode does not guarantee a DC path to
//     ground), so nMid is flagged "floating" and prepare() REJECTS the chain — it
//     cannot be a prepare()-valid topology (FR-006) without gmin / MNA (Phase 5).
//   - STACKING seriesCount diodes across the SAME (n1, n2) pair would prepare()
//     and solve, but that is electrically PARALLEL (one diode of seriesCount×
//     area), not "series" — a misleading meaning for the field.
// So v1 ships the unambiguous canonical: exactly one inline series diode. A
// non-unit seriesCount is a descriptive throw, never a silently-wrong parallel
// stamp. Multi-diode series strings are a Phase-5 refinement.
// ---------------------------------------------------------------------------
inline SeriesClipper seriesClipper(const SeriesValues& v, double vIn = 0.0) {
    detail::requirePositive(v.Cc, "seriesClipper Cc");
    detail::requirePositive(v.R, "seriesClipper R");
    detail::requireValidDiode(v.diode);
    if (v.seriesCount != 1) {
        throw std::invalid_argument(
            "seriesClipper: v1 supports exactly one inline series diode "
            "(seriesCount == 1); a multi-diode series string needs intermediate "
            "nodes the prepare() floating-node rule cannot admit (deferred to "
            "Phase 5) — got seriesCount=" + std::to_string(v.seriesCount));
    }

    SeriesClipper c{};
    const NodeId nIn = c.netlist.addNode();  // 1: driven input node
    const NodeId n1 = c.netlist.addNode();   // 2: post-coupling-cap node
    const NodeId n2 = c.netlist.addNode();   // 3: post-diode / output node

    c.netlist.add(VoltageSource{nIn, kGround, vIn});
    c.netlist.add(Capacitor{nIn, n1, v.Cc});
    c.netlist.add(detail::diodeOf(v.diode, n1, n2));  // single inline diode, port (n1,n2)
    c.netlist.add(Resistor{n2, kGround, v.R});
    c.netlist.prepare();

    c.inNode = nIn;
    c.outNode = n2;
    c.portP = n1;
    c.portN = n2;
    return c;
}

}  // namespace acfx
