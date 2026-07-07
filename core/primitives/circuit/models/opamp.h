#pragma once

// OpAmp — the ideal operational amplifier as a nullor (opamp-stages design D2,
// FR-001/FR-002; research R1/R2). This is the ONE sanctioned extension of the
// otherwise-frozen circuit/ vocabulary.
//
// An ideal op-amp is a nullor: a nullator across its two inputs plus a norator
// at its output.
//
//   - The NULLATOR imposes the virtual short `V(inPlus) = V(inMinus)` and draws
//     ZERO input current at both inputs. It is a constraint, not a device with
//     a transfer function.
//   - The NORATOR at `out` sources whatever current the surrounding feedback
//     network demands; that output branch current is a free unknown the solve
//     determines. The op-amp has infinite — but strictly LINEAR — gain in its
//     operating region.
//
// Like VoltageSource (sources.h), OpAmp is a CONSTRAINT, not a conductance:
//   - NO admittance() — the nullor is not expressible as any finite admittance.
//     A large-but-finite gain (a VCVS with huge A, or a large-conductance
//     feedback divider) is the forbidden gmin-style fallback that makes the
//     system ill-conditioned and hides the virtual short behind a fragile
//     approximation (research R4; Constitution V). The solver realizes the
//     op-amp EXACTLY by bordering the reduced nodal system with one constraint
//     row (`V(inPlus) - V(inMinus) = 0`) and one branch-current column that
//     injects the norator current at `out` (research R2) — never by a gain.
//   - NO companion() — the ideal op-amp carries no reactive state.
//   - NO parameter fields — deliberately no Vsat / GBW / slewRate / finite-gain
//     / offset in v1. Carrying ideal-defaulted non-ideality fields now would
//     invite a solver to read them and quietly become nonideal. The
//     non-ideality axis is a CAPTURED, separate future deliverable (FR-025):
//     the struct is shaped so those fields can be added later with ideal
//     defaults, non-breaking, by that deliverable — but they are absent here.
//
// Terminals: `inPlus` (non-inverting input), `inMinus` (inverting input), and
// `out` (the norator-driven output). terminalsOf() reports only the input pair
// — the span of the virtual-short constraint; the output is the driven terminal
// handled by the solver's augmentation, NOT a passive two-terminal edge
// (netlist.h). contributesConductivePath() is false: the output is excluded
// from the nodal reachability pre-filter (the feedback network provides
// reachability; the augmented-system singularity is the authoritative
// well-posedness gate — research R5).
//
// Platform independence (Constitution IV): standard library only; no desktop or
// MCU platform-specific headers. Header-only, zero-overhead, no heap, no I/O.

#include "primitives/circuit/node.h"

namespace acfx {

// OpAmp — an ideal op-amp (nullor) with non-inverting input `inPlus`, inverting
// input `inMinus`, and output `out`.
//
// This is a constraint, not a conductance: it has no admittance() and no
// companion(), and it carries no physical parameters. A solver realizes it via
// the bordered nullor stamp (research R2) — never by substituting a large finite
// gain (that would be the forbidden gmin/large-gain fallback, research R4).
struct OpAmp {
    NodeId inPlus;   // non-inverting input
    NodeId inMinus;  // inverting input
    NodeId out;      // output (norator-driven)
};

} // namespace acfx
