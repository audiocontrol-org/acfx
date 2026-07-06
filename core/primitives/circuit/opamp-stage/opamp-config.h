#pragma once

#include "primitives/circuit/netlist.h"
#include "primitives/circuit/node.h"

#include <stdexcept>
#include <string>

// opamp-config.h — the op-amp-stage builders' value vocabulary (data-model.md
// "Builder BOM/config structs"; contracts/opamp-stage-builder.md; FR-003..010).
// Plain aggregates only: a bill of materials per topology, the assembled
// return struct shared by all four topologies, and the per-topology fixed
// Netlist capacities. Mirrors core/primitives/circuit/diode-clipper/
// clipper-config.h's shape and validation style closely (sibling primitive).
//
// PORTABLE PRIMITIVE (Constitution IV): C++17, header-only, standard-library
// only, RT-safe, heap-free (the returned Netlist is a fixed std::array). This
// header includes NOTHING under core/labs/ (FR-024) — it depends only on the
// frozen component-abstractions vocabulary (Netlist, NodeId). It is also
// self-contained with respect to sibling primitive features: rather than
// reaching into diode-clipper/clipper-config.h for its diode-parameter type,
// it defines its own local `DiodeSpec` — same name and shape as the
// established repo convention, deliberately NOT invented as a new name
// (data-model.md calls the field `DiodeParams`, but the repo's actual,
// already-established type for "Shockley Is/n/Vt" is `DiodeSpec`
// (clipper-config.h); this header mirrors that name). The builders that
// consume these types live in opamp-stage.h (T007-T011).
//
// No fallbacks / no mock (Constitution V): the validation helpers below raise
// a descriptive std::invalid_argument on bad BOM input (non-positive value,
// invalid diode parameter, or an out-of-range diode population); they never
// clamp, substitute, or fabricate.

namespace acfx {

// ---------------------------------------------------------------------------
// Diode-population cap for the op-amp+diode clipper's feedback string.
// ---------------------------------------------------------------------------

// The default per-clipper feedback-diode population ceiling (data-model.md
// "OpAmpDiodeClipperBom"). A build whose nUp+nDown exceeds this is rejected
// (FR-010); the per-topology capacity alias below is sized to hold this many
// diodes plus the topology's fixed components with a little headroom.
inline constexpr int kMaxOpAmpClipperDiodes = 4;

// ---------------------------------------------------------------------------
// Diode parameters (shared) — the Shockley set fed to each Diode component.
// ---------------------------------------------------------------------------

// DiodeSpec — reverse saturation current Is (A), ideality factor n, thermal
// voltage Vt (V). Validation: all > 0 (FR-010). Same name/shape as
// diode-clipper/clipper-config.h's DiodeSpec, defined locally here so this
// header stays a self-contained sibling primitive.
struct DiodeSpec {
    double Is;  // reverse saturation current (A)
    double n;   // ideality factor (dimensionless)
    double Vt;  // thermal voltage (V)
};

// Canonical silicon signal-diode parameters (component-abstractions
// reference: Is = 1e-14 A, n = 1, Vt = 25.85 mV at ~300 K) — matches
// clipper-config.h::siliconSignalDiode() so representative BOMs across both
// features share one default.
inline constexpr DiodeSpec siliconSignalDiode() {
    return DiodeSpec{1.0e-14, 1.0, 0.02585};
}

// ---------------------------------------------------------------------------
// Per-topology bill of materials (data-model.md "Builder BOM/config structs").
// ---------------------------------------------------------------------------

// Non-inverting gain: Rf (out -> inMinus), Rg (inMinus -> gnd), input at
// inPlus. Closed-loop gain 1 + Rf/Rg.
struct NonInvertingGainBom {
    double Rf;    // feedback resistor, out -> inMinus (ohm)
    double Rg;    // ground-leg resistor, inMinus -> gnd (ohm)
    double vin;   // drive source amplitude at inPlus (V)
};

// Inverting gain: Rin (vin -> inMinus), Rf (out -> inMinus), inPlus -> gnd.
// Gain -Rf/Rin.
struct InvertingGainBom {
    double Rin;   // input resistor, vin -> inMinus (ohm)
    double Rf;    // feedback resistor, out -> inMinus (ohm)
    double vin;   // drive source amplitude (V)
};

// Active first-order (inverting) low-pass: Rin (vin -> inMinus); feedback
// Cf parallel Rf (out -> inMinus); inPlus -> gnd. DC gain -Rf/Rin, corner
// 1/(2*pi*Rf*Cf).
struct ActiveFirstOrderBom {
    double Rin;   // input resistor (ohm)
    double Rf;    // feedback resistor, out -> inMinus (ohm)
    double Cf;    // feedback capacitor, parallel with Rf (farad)
    double vin;   // drive amplitude (V)
};

// Op-amp + diode clipper (TS808 core): Rin (vin -> inMinus); feedback network
// out -> inMinus = Rf || Cf || antiparallel diode string; inPlus -> gnd.
// Exactly one nonlinearity location (the feedback diode pair). nUp/nDown >= 1;
// symmetric iff nUp == nDown (asymmetric -> DC offset / even harmonics).
struct OpAmpDiodeClipperBom {
    double Rin;      // input resistor, vin -> inMinus (ohm)
    double Rf;       // feedback resistor setting the clean-gain floor (ohm)
    double Cf;       // feedback capacitor across the diode network (farad)
    DiodeSpec diode; // shared Shockley spec for every feedback diode
    int nUp;         // forward diode count, Diode{out, inMinus}
    int nDown;       // reverse diode count, Diode{inMinus, out}
    double vin;      // drive amplitude (V)
};

// ---------------------------------------------------------------------------
// Assembled netlist (shared shape across all four topologies).
// ---------------------------------------------------------------------------

// OpAmpStage<MaxNodes, MaxComponents> — the emitted topology plus the input
// and output node handles (data-model.md "Netlist sizing"; contracts/
// opamp-stage-builder.md: each *Result bundles a per-topology-sized Netlist
// plus inNode/outNode).
template <int MaxNodes, int MaxComponents>
struct OpAmpStage {
    Netlist<MaxNodes, MaxComponents> netlist;
    NodeId inNode;
    NodeId outNode;
};

// Per-topology capacity aliases, sized to the exact element count of each
// topology (data-model.md "Netlist sizing"):
//   - nonInvertingGain: nodes {gnd, inPlus, inMinus, out} = 4;
//     components {Vin, OpAmp, Rf, Rg} = 4.
//   - invertingGain: nodes {gnd, vin, inMinus, out} = 4 (inPlus ties directly
//     to gnd, no extra node); components {Vin, Rin, OpAmp, Rf} = 4.
//   - activeFirstOrder: same 4 nodes as invertingGain; components
//     {Vin, Rin, OpAmp, Rf, Cf} = 5.
//   - opAmpDiodeClipper: same 4 nodes as invertingGain (the feedback diode
//     string spans the existing out/inMinus nodes, no new node);
//     components {Vin, Rin, OpAmp, Rf, Cf} = 5 fixed + up to
//     kMaxOpAmpClipperDiodes feedback diodes = 9.
using NonInvertingGainResult = OpAmpStage<4, 4>;
using InvertingGainResult = OpAmpStage<4, 4>;
using ActiveFirstOrderResult = OpAmpStage<4, 5>;
using OpAmpDiodeClipperResult = OpAmpStage<4, 5 + kMaxOpAmpClipperDiodes>;

// ---------------------------------------------------------------------------
// Validation helpers (fail loud; FR-010). Not part of the public surface, but
// header-visible so the builders in opamp-stage.h share exactly one policy.
// ---------------------------------------------------------------------------

// Namespaced per-feature (acfx::opamp_detail, NOT the shared acfx::detail) so
// these inline helpers do not collide with the identically-named-but-different
// bodies in clipper-config.h / tone-stack.h. Sharing acfx::detail across those
// headers is an ODR violation (one body wins at link time, so a validation
// error can print the wrong feature's message); opamp-stages stays out of it.
namespace opamp_detail {

// Require a strictly-positive physical value, else a descriptive throw naming
// the field. No clamp, no fallback (Constitution V).
inline void requirePositive(double value, const char* field) {
    if (!(value > 0.0)) {
        throw std::invalid_argument(
            std::string("opamp-stage builder: ") + field +
            " must be > 0 (got " + std::to_string(value) + ")");
    }
}

// Validate a diode spec: Is, n, Vt all > 0.
inline void requireValidDiode(const DiodeSpec& d) {
    requirePositive(d.Is, "diode Is");
    requirePositive(d.n, "diode n");
    requirePositive(d.Vt, "diode Vt");
}

// Require an individual diode-direction count (nUp or nDown) >= 1 — each leg
// of the antiparallel feedback string must be populated (data-model.md
// "OpAmpDiodeClipperBom": "nUp/nDown >= 1").
inline void requireDiodeCount(int count, const char* field) {
    if (count < 1) {
        throw std::invalid_argument(
            std::string("opamp-stage builder: ") + field +
            " must be >= 1 (got " + std::to_string(count) + ")");
    }
}

// Require the total feedback-diode population (nUp + nDown) fit within
// kMaxOpAmpClipperDiodes — the per-topology capacity alias above is sized to
// exactly this ceiling, so an over-populated BOM is rejected before it could
// ever overflow the fixed Netlist.
inline void requireDiodePopulation(int total, const char* field) {
    if (total > kMaxOpAmpClipperDiodes) {
        throw std::invalid_argument(
            std::string("opamp-stage builder: ") + field + " (" +
            std::to_string(total) + ") exceeds kMaxOpAmpClipperDiodes (" +
            std::to_string(kMaxOpAmpClipperDiodes) + ")");
    }
}

}  // namespace opamp_detail

}  // namespace acfx
