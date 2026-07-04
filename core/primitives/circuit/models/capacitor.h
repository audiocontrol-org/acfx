#pragma once

#include "primitives/circuit/node.h"
#include "primitives/circuit/models/companion.h"

// Capacitor — the reactive circuit primitive's capacitive element (data-model.md
// "Capacitor"; contracts/component-physics.md "Reactive"; FR-022). A
// two-terminal element of capacitance `C` between terminals `a` and `b`.
//
// Companion model (research.md R3; design D2): the capacitor is discretised
// with the backward-Euler rule, giving at each timestep a Norton companion
// with equivalent conductance Geq = C/dt and history current Ieq = Geq * vPrev,
// where vPrev is the voltage across the capacitor at the previous timestep.
//
// The component holds NO state. `vPrev` (the previous across-cap node voltage)
// is supplied BY THE SOLVER (design D2 / R3); this keeps Capacitor a pure
// value with no per-instance history, so the same descriptor can back many
// solver instances. The solver stamps Geq / Ieq into the nodal system.
//
// Backward Euler is deliberately non-normative: it is the lab's naive,
// first-order choice (research.md R3). A better integrator (trapezoidal, etc.)
// would change companion(), not this component's role.
//
// Precondition: dt > 0. dt == 0 would divide by zero (C/dt -> +infinity per
// IEEE 754); a zero or negative step is a solver-configuration error, not a
// case this component papers over — companion() never clamps or substitutes a
// fallback value (repo standard: no fallbacks, no mock data).
//
// Physics in double (FR-022). Header-only, zero-overhead, no heap, no I/O.
// Platform independence (Constitution IV): standard library only.

namespace acfx {

struct Capacitor {
    NodeId a;
    NodeId b;
    double C;

    // Backward-Euler companion for a timestep `dt` given the previous
    // across-capacitor voltage `vPrev` (supplied by the solver). Returns the
    // equivalent conductance Geq = C/dt and history current Ieq = Geq * vPrev.
    // Precondition: dt > 0.
    Companion companion(double dt, double vPrev) const noexcept {
        const double Geq = C / dt;
        return Companion{Geq, Geq * vPrev};
    }
};

} // namespace acfx
