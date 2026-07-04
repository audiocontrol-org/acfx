#pragma once

#include "primitives/circuit/components.h"
#include "primitives/circuit/netlist.h"
#include "primitives/circuit/node.h"
#include "primitives/circuit/tone-stack/taper.h"

#include <stdexcept>
#include <string>

// Tone-stack builders — the solver-neutral passive tone-stack primitive
// (contracts/tone-stack-builder.md; data-model.md; FR-001/002). Each builder
// assembles the FROZEN component-abstractions vocabulary (Resistor, Capacitor,
// VoltageSource, Netlist) into a passive tone-control TOPOLOGY, driven by a bill
// of materials and pot positions. It returns a prepared `Netlist` — TOPOLOGY
// ONLY: no solved response, no audio path, no `process()` (FR-001/017). A solver
// (the lab `solveAC` now; Phase-5 MNA / Phase-6 WDF later) consumes it.
//
// THE SEAM (FR-003): the builders introduce no new circuit-vocabulary element
// and modify none. Potentiometers are BUILD-TIME math (taper.h): a divider is
// two `Resistor` legs from `wiper()`, a rheostat is one leg from `rheostat()`.
// The component vocabulary stays frozen exactly as component-abstractions left
// it. A control change is a fresh builder call (control-rate rebuild; FR-004);
// the builders are pure functions with no retained state and no heap (fixed
// compile-time `Netlist` capacities).
//
// TOPOLOGIES (research.md R1/R2). Two distinct shapes so the abstraction is not
// over-fit to one (the anti-over-fit reasoning that kept the inductor in
// component-abstractions):
//
//   toneStackFMV — a Fender/Marshall/Vox-style passive 3-band guitar stack
//     (basic Bassman form: treble + bass shaping caps). The treble pot is a
//     3-terminal divider whose wiper is the output; the bass and mid pots are
//     rheostats (bass in series with the bass cap to ground, mid shunting the
//     bass/mid junction to ground — the classic mid-scoop mechanism). The exact
//     vendor BOM/wiring fidelity (a specific Fender part list) is the later
//     `design:feature/fender-tone-stack` item; this primitive teaches the
//     pot-driven 3-band pattern.
//
//   toneStackBaxandall — a passive Baxandall/James 2-band shelving control.
//     Bass and treble are each 3-terminal dividers; a bass cap bypasses the
//     bass pot at high frequency (so the bass control is a low shelf) and a
//     treble cap couples only high frequency into the treble pot (a high
//     shelf). Baxandall/James pots are conventionally LINEAR taper — the
//     network shapes the response, not the taper.
//
// ERRORS, NEVER FALLBACKS (Constitution V, FR-010): any non-positive value in
// the bill of materials, or any control outside [0, 1], raises a descriptive
// `std::invalid_argument` on the build/control thread (taper.h validates the
// pot inputs; the builder validates the remaining BOM fields). No silent clamp,
// no fabricated topology.
//
// Physics in double. Header-only, C++17, standard-library + the frozen circuit
// vocabulary only — NO include of anything under core/labs/ (isolation, FR-016).
// Platform independence (Constitution IV).

namespace acfx {

// ===========================================================================
// FMV (Fender/Marshall/Vox) 3-band stack.
// ===========================================================================

// Bill of materials for the FMV stack (all values in SI units: ohms, farads).
struct FMVValues {
    double r1;      // slope resistor (Ω)
    double c1;      // treble cap (F)
    double c2;      // bass cap (F)
    double rTreble; // treble pot track (Ω) — 3-terminal divider, wiper = output
    double rBass;   // bass pot track (Ω)   — rheostat (bass branch to ground)
    double rMid;    // mid pot track (Ω)     — rheostat (shunt to ground)
    double rLoad;   // following-stage input impedance to ground (Ω) — explicit
};

// Pot positions in [0, 1].
struct FMVControls {
    double bass;
    double mid;
    double treble;
};

// Compile-time capacities for the FMV netlist. Non-ground nodes: input, treble-
// cap node, slope junction, output (treble wiper), bass-cap node, bass/mid
// junction (6, so 7 including ground). Components: source + slope + 2 caps +
// treble divider (2 legs) + bass rheostat + mid rheostat + load (9). Small
// headroom above the exact counts.
inline constexpr int kFmvNodes = 8;
inline constexpr int kFmvComponents = 12;

using FMVNetlist = Netlist<kFmvNodes, kFmvComponents>;

// ===========================================================================
// Baxandall (passive James) 2-band shelving control.
// ===========================================================================

struct BaxandallValues {
    double rBass;      // bass pot track (Ω) — 3-terminal divider
    double cBass;      // bass cap (F) — bypasses the bass pot top at HF (low shelf)
    double rBassOut;   // bass wiper -> output mixing resistor (Ω)
    double cTreble;    // treble cap (F) — couples HF into the treble pot (high shelf)
    double rTreble;    // treble pot track (Ω) — 3-terminal divider
    double rTrebleOut; // treble wiper -> output mixing resistor (Ω)
    double rLoad;      // following-stage input impedance to ground (Ω) — explicit
};

struct BaxandallControls {
    double bass;
    double treble;
};

// Compile-time capacities for the Baxandall netlist. Non-ground nodes: input,
// bass wiper, treble-cap node, treble wiper, output (5, so 6 including ground).
// Components: source + bass divider (2 legs) + bass cap + bass-out + treble cap
// + treble divider (2 legs) + treble-out + load (10). Small headroom.
inline constexpr int kBaxNodes = 7;
inline constexpr int kBaxComponents = 12;

using BaxandallNetlist = Netlist<kBaxNodes, kBaxComponents>;

// ===========================================================================
// A builder result: the prepared netlist plus the input/output node ids the
// lab AC solver reads H = V(out)/V(in) between.
// ===========================================================================

template <int MaxNodes, int MaxComponents>
struct ToneStack {
    Netlist<MaxNodes, MaxComponents> netlist;
    NodeId inNode;
    NodeId outNode;
};

using FMVToneStack = ToneStack<kFmvNodes, kFmvComponents>;
using BaxandallToneStack = ToneStack<kBaxNodes, kBaxComponents>;

namespace detail {

// Validate that a bill-of-materials field is strictly positive (FR-010).
inline void requirePositive(double value, const char* name) {
    if (!(value > 0.0)) {
        throw std::invalid_argument(
            std::string("tone-stack builder: ") + name +
            " must be > 0 (got " + std::to_string(value) + ")");
    }
}

}  // namespace detail

// ---------------------------------------------------------------------------
// toneStackFMV — assemble the FMV 3-band stack (FR-001).
// ---------------------------------------------------------------------------
inline FMVToneStack toneStackFMV(const FMVValues& v, const FMVControls& c,
                                 Taper taper) {
    detail::requirePositive(v.r1, "r1");
    detail::requirePositive(v.c1, "c1");
    detail::requirePositive(v.c2, "c2");
    detail::requirePositive(v.rTreble, "rTreble");
    detail::requirePositive(v.rBass, "rBass");
    detail::requirePositive(v.rMid, "rMid");
    detail::requirePositive(v.rLoad, "rLoad");
    // Pot positions are validated inside wiper()/rheostat() (FR-010).

    const WiperSplit treble = wiper(v.rTreble, c.treble, taper);
    const double rBass = rheostat(v.rBass, c.bass, taper);
    const double rMid = rheostat(v.rMid, c.mid, taper);

    FMVNetlist nl;
    const NodeId nIn = nl.addNode();    // 1: input (driven)
    const NodeId nTreb = nl.addNode();  // 2: treble-cap node (treble pot top)
    const NodeId nJ = nl.addNode();     // 3: slope junction (treble pot bottom)
    const NodeId nOut = nl.addNode();   // 4: output (treble wiper)
    const NodeId nBass = nl.addNode();  // 5: bass-cap node
    const NodeId nMid = nl.addNode();   // 6: bass/mid junction

    nl.add(VoltageSource{nIn, kGround, 1.0});    // AC drive (unit input)
    nl.add(Capacitor{nIn, nTreb, v.c1});         // treble cap
    nl.add(Resistor{nIn, nJ, v.r1});             // slope resistor
    nl.add(Resistor{nTreb, nOut, treble.rTop});  // treble pot upper leg
    nl.add(Resistor{nOut, nJ, treble.rBottom});  // treble pot lower leg
    nl.add(Capacitor{nJ, nBass, v.c2});          // bass cap
    nl.add(Resistor{nBass, nMid, rBass});        // bass rheostat
    nl.add(Resistor{nMid, kGround, rMid});       // mid rheostat (shunt to ground)
    nl.add(Resistor{nOut, kGround, v.rLoad});    // following-stage load
    nl.prepare();

    return FMVToneStack{nl, nIn, nOut};
}

// ---------------------------------------------------------------------------
// toneStackBaxandall — assemble the passive James 2-band stack (FR-002).
// ---------------------------------------------------------------------------
inline BaxandallToneStack toneStackBaxandall(const BaxandallValues& v,
                                             const BaxandallControls& c,
                                             Taper taper) {
    detail::requirePositive(v.rBass, "rBass");
    detail::requirePositive(v.cBass, "cBass");
    detail::requirePositive(v.rBassOut, "rBassOut");
    detail::requirePositive(v.cTreble, "cTreble");
    detail::requirePositive(v.rTreble, "rTreble");
    detail::requirePositive(v.rTrebleOut, "rTrebleOut");
    detail::requirePositive(v.rLoad, "rLoad");

    const WiperSplit bass = wiper(v.rBass, c.bass, taper);
    const WiperSplit treble = wiper(v.rTreble, c.treble, taper);

    BaxandallNetlist nl;
    const NodeId nIn = nl.addNode();     // 1: input (driven)
    const NodeId nBw = nl.addNode();     // 2: bass wiper
    const NodeId nTt = nl.addNode();     // 3: treble-cap node (treble pot top)
    const NodeId nTw = nl.addNode();     // 4: treble wiper
    const NodeId nOut = nl.addNode();    // 5: output (mixing node)

    nl.add(VoltageSource{nIn, kGround, 1.0});        // AC drive (unit input)
    nl.add(Resistor{nIn, nBw, bass.rTop});           // bass pot upper leg
    nl.add(Resistor{nBw, kGround, bass.rBottom});    // bass pot lower leg
    nl.add(Capacitor{nIn, nBw, v.cBass});            // bass cap: bypass at HF (low shelf)
    nl.add(Resistor{nBw, nOut, v.rBassOut});         // bass wiper -> output
    nl.add(Capacitor{nIn, nTt, v.cTreble});          // treble cap: couple HF (high shelf)
    nl.add(Resistor{nTt, nTw, treble.rTop});         // treble pot upper leg
    nl.add(Resistor{nTw, kGround, treble.rBottom});  // treble pot lower leg
    nl.add(Resistor{nTw, nOut, v.rTrebleOut});       // treble wiper -> output
    nl.add(Resistor{nOut, kGround, v.rLoad});        // following-stage load
    nl.prepare();

    return BaxandallToneStack{nl, nIn, nOut};
}

}  // namespace acfx
