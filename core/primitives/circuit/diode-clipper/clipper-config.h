#pragma once

#include "primitives/circuit/netlist.h"
#include "primitives/circuit/node.h"

#include <stdexcept>
#include <string>

// clipper-config.h — the diode-clipper builders' value vocabulary (data-model.md
// "Per-topology bill of materials"; contracts/diode-clipper-builder.md;
// FR-001..007). Plain aggregates only: a bill of materials (resistor/capacitor
// values + diode parameters + diode population) per topology, the assembled
// return struct, and the per-topology fixed Netlist capacities.
//
// PORTABLE PRIMITIVE (Constitution IV): C++17, header-only, standard-library
// only, RT-safe, heap-free (the returned Netlist is a fixed std::array). This
// header includes NOTHING under core/labs/ (FR-004/FR-019) — it depends only on
// the frozen component-abstractions vocabulary (Netlist, NodeId). The builders
// that consume these types live in diode-clipper.h.
//
// No fallbacks / no mock (Constitution V): the validation helpers below raise a
// descriptive std::invalid_argument on bad BOM input (non-positive value, or an
// out-of-range diode population); they never clamp, substitute, or fabricate.

namespace acfx {

// ---------------------------------------------------------------------------
// Diode-population cap (shared with the lab solver's default MaxDiodes).
// ---------------------------------------------------------------------------

// The default per-clipper diode-population ceiling (data-model.md R2). A build
// whose total diode count exceeds this is rejected (FR-007); it also matches the
// lab TransientClipper's default MaxDiodes = 4, so a builder-valid netlist fits
// the solver's augmented capacity without retuning. The per-topology capacity
// aliases below are sized to hold this many diodes plus the topology's fixed
// components with a little headroom.
inline constexpr int kMaxClipperDiodes = 4;

// ---------------------------------------------------------------------------
// Diode parameters (shared) — the Shockley set fed to each Diode component.
// ---------------------------------------------------------------------------

// DiodeSpec — reverse saturation current Is (A), ideality factor n, thermal
// voltage Vt (V). Validation: all > 0 (FR-007). The canonical default is a
// silicon signal diode, matching the component-abstractions reference set.
struct DiodeSpec {
    double Is;  // reverse saturation current (A)
    double n;   // ideality factor (dimensionless)
    double Vt;  // thermal voltage (V)
};

// Canonical silicon signal-diode parameters (component-abstractions reference:
// Is = 1e-14 A, n = 1, Vt = 25.85 mV at ~300 K). Exposed as a helper so the
// builders' representative bills of materials and the tests share one default.
inline constexpr DiodeSpec siliconSignalDiode() {
    return DiodeSpec{1.0e-14, 1.0, 0.02585};
}

// ---------------------------------------------------------------------------
// Per-topology bill of materials.
// ---------------------------------------------------------------------------

// Symmetric shunt: series resistor R → a matched antiparallel diode pair to
// ground, with a filter capacitor Cf across the diodes. The output is the shunt
// node. One diode spec backs the matched pair.
struct SymmetricShuntValues {
    double R;          // series resistor (ohm)
    double Cf;         // filter capacitor across the diodes (farad)
    DiodeSpec diode;   // shared spec for the matched antiparallel pair
};

// Asymmetric shunt: same series-R → shunt structure with an UNEQUAL diode
// population (upCount anode→shunt-node, downCount reversed), giving a non-odd
// transfer with a DC offset. v1 canonical { upCount = 2, downCount = 1 }.
// Validation: 1 <= upCount + downCount <= kMaxClipperDiodes, upCount != downCount
// (equal populations are the symmetric case — use symmetricShuntClipper).
struct AsymmetricShuntValues {
    double R;          // series resistor (ohm)
    double Cf;         // filter capacitor across the diodes (farad)
    DiodeSpec diode;   // shared spec for every diode in the population
    int upCount;       // diodes oriented anode → shunt node
    int downCount;     // diodes oriented shunt node → anode (reversed)
};

// Series (inline) clipper: input coupling capacitor Cc in series ahead of
// seriesCount inline diodes, a resistor R to ground. The coupling cap blocks DC
// (output → 0 at DC). Output is the post-diode node. Validation: seriesCount in
// [1, kMaxClipperDiodes].
struct SeriesValues {
    double Cc;         // input coupling capacitor in series (farad)
    double R;          // resistor to ground (ohm)
    DiodeSpec diode;   // shared spec for the inline diode string
    int seriesCount;   // number of inline diodes across the port node pair
};

// ---------------------------------------------------------------------------
// Assembled netlist (per topology).
// ---------------------------------------------------------------------------

// Clipper<MaxNodes, MaxComponents> — the emitted topology plus the input,
// output, and clipper-port node handles. `portP`/`portN` are the node pair
// carrying the diode string (consumed by the solver); `outNode` is the topology
// output; `inNode` is the driven source node.
template <int MaxNodes, int MaxComponents>
struct Clipper {
    Netlist<MaxNodes, MaxComponents> netlist;
    NodeId inNode;
    NodeId outNode;
    NodeId portP;
    NodeId portN;
};

// Per-topology capacity aliases (data-model.md), sized to hold each topology's
// fixed components plus up to kMaxClipperDiodes diodes with headroom:
//   - shunt (symmetric/asymmetric): nodes {ground, in, shunt} = 3 (<= 4);
//     components {Vin, R, Cf} = 3 fixed + up to 4 diodes = 7 (<= 8).
//   - series: nodes {ground, in, n1, n2} = 4 (<= 5); components {Vin, Cc, R} = 3
//     fixed + up to 4 inline diodes = 7 (<= 8).
using SymmetricShuntClipper = Clipper<4, 8>;
using AsymmetricShuntClipper = Clipper<4, 8>;
using SeriesClipper = Clipper<5, 8>;

// ---------------------------------------------------------------------------
// Validation helpers (fail loud; FR-007). Not part of the public surface, but
// header-visible so the builders in diode-clipper.h share exactly one policy.
// ---------------------------------------------------------------------------

namespace detail {

// Require a strictly-positive physical value, else a descriptive throw naming
// the field. No clamp, no fallback (Constitution V).
inline void requirePositive(double value, const char* field) {
    if (!(value > 0.0)) {
        throw std::invalid_argument(
            std::string("diode-clipper builder: ") + field +
            " must be > 0 (got " + std::to_string(value) + ")");
    }
}

// Validate a diode spec: Is, n, Vt all > 0.
inline void requireValidDiode(const DiodeSpec& d) {
    requirePositive(d.Is, "diode Is");
    requirePositive(d.n, "diode n");
    requirePositive(d.Vt, "diode Vt");
}

// Require an in-range total diode population [1, kMaxClipperDiodes]. The label
// names the builder/field for a descriptive message.
inline void requireDiodePopulation(int total, const char* field) {
    if (total < 1) {
        throw std::invalid_argument(
            std::string("diode-clipper builder: ") + field +
            " must be >= 1 (got " + std::to_string(total) + ")");
    }
    if (total > kMaxClipperDiodes) {
        throw std::invalid_argument(
            std::string("diode-clipper builder: ") + field + " (" +
            std::to_string(total) + ") exceeds kMaxClipperDiodes (" +
            std::to_string(kMaxClipperDiodes) + ")");
    }
}

}  // namespace detail

}  // namespace acfx
