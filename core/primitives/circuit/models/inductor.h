#pragma once

#include "primitives/circuit/node.h"
#include "primitives/circuit/models/companion.h"

// Inductor — the reactive circuit primitive's inductive element (data-model.md
// "Inductor"; contracts/component-physics.md "Reactive"; FR-022). A
// two-terminal element of inductance `L` between terminals `a` and `b`.
//
// Companion model (research.md R3; design D2): the inductor is discretised with
// the backward-Euler rule, giving at each timestep a Norton companion with
// equivalent conductance Geq = dt/L and history current Ieq = -iPrev, where
// iPrev is the inductor branch current at the previous timestep.
//
// Dual of the capacitor: where the capacitor gives Geq = C/dt with a
// voltage-history term Ieq = Geq * vPrev, the inductor swaps the roles of dt
// and the reactance and carries a current-history term Ieq = -iPrev. Keeping L
// in v1 exists specifically to exercise the companion() seam a second time via
// this dual (design D2 / R3).
//
// The component holds NO state. `iPrev` (the previous inductor branch current)
// is supplied BY THE SOLVER (design D2 / R3); this keeps Inductor a pure value
// with no per-instance history, so the same descriptor can back many solver
// instances. The solver stamps Geq / Ieq into the nodal system.
//
// Backward Euler is deliberately non-normative: it is the lab's naive,
// first-order choice (research.md R3). A better integrator (trapezoidal, etc.)
// would change companion(), not this component's role.
//
// Precondition: dt > 0 and L > 0. L == 0 would divide by zero (dt/L ->
// +infinity per IEEE 754); a zero or negative inductance, or a zero or negative
// step, is a netlist- or solver-configuration error, not a case this component
// papers over — companion() never clamps or substitutes a fallback value (repo
// standard: no fallbacks, no mock data).
//
// Physics in double (FR-022). Header-only, zero-overhead, no heap, no I/O.
// Platform independence (Constitution IV): standard library only.

namespace acfx {

struct Inductor {
    NodeId a;
    NodeId b;
    double L;

    // Backward-Euler companion for a timestep `dt` given the previous inductor
    // branch current `iPrev` (supplied by the solver). Returns the equivalent
    // conductance Geq = dt/L and history current Ieq = -iPrev. Dual of the
    // capacitor's companion(). Precondition: dt > 0 and L > 0.
    Companion companion(double dt, double iPrev) const noexcept {
        return Companion{dt / L, -iPrev};
    }
};

} // namespace acfx
