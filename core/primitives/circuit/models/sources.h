#pragma once

// Independent sources — VoltageSource and CurrentSource (component-physics.md
// "Sources"; data-model.md). Both are linear elements, but neither is a
// conductance:
//
//   - VoltageSource is IDEAL (no series resistance). It pins the constraint
//     `(p - n) = V` directly. An ideal voltage source cannot be expressed as
//     a finite admittance without an ad hoc (and forbidden) gmin-style
//     approximation, so it exposes no admittance()/evaluate() at all — the
//     solver must impose it structurally via fixed-node reduction (R1).
//   - CurrentSource is a right-hand-side contribution only: it injects a
//     fixed current `+I` at node p and `-I` at node n. It has no admittance
//     either (its contribution does not depend on any node voltage).
//
// Platform independence (Constitution IV): standard library only; no
// desktop or MCU platform-specific headers. Header-only, zero-overhead,
// no heap, no I/O.

#include "primitives/circuit/node.h"

namespace acfx {

// VoltageSource — an ideal independent voltage source between nodes p and n,
// pinning `V(p) - V(n) = V`.
//
// This is a constraint, not a conductance: it has no admittance() or
// evaluate(). A solver realizes it via fixed-node reduction (component-
// physics.md R1) — never by substituting a large-but-finite conductance
// (that would be the forbidden gmin fallback).
struct VoltageSource {
    NodeId p;
    NodeId n;
    double V;
};

// CurrentSource — an independent current source between nodes p and n,
// injecting `+I` at node p and `-I` at node n.
//
// This is a fixed right-hand-side contribution: it does not depend on any
// node voltage, so it has no admittance(). current() returns the injected
// current `I` (amps); the solver applies it with the +p/-n sign convention
// documented above.
struct CurrentSource {
    NodeId p;
    NodeId n;
    double I;

    // The source's injected current (A), following the +p/-n convention.
    double current() const {
        return I;
    }
};

} // namespace acfx
