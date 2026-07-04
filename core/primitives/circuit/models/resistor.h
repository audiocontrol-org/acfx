#pragma once

#include "primitives/circuit/node.h"

// Resistor — the linear circuit primitive's ohmic element (data-model.md
// "Resistor"; contracts/component-physics.md "Linear elements"; FR-003).
// A two-terminal element whose only physics is a scalar conductance
// G = 1/R between its terminals `a` and `b`.
//
// Nodal sign convention (solver concern, documented here for context only):
// when a solver stamps this component's contribution into the nodal
// conductance matrix, G goes on both diagonal entries (a,a) and (b,b), and
// -G on both off-diagonal entries (a,b) and (b,a). Resistor itself does not
// stamp/scatter (FR-006) — it only reports the scalar G; assembling it into
// a matrix is the solver's job.
//
// Precondition: R > 0. R == 0 (ideal short) and R == infinity (open) are
// representable in this type (admittance() then returns +infinity or 0,
// per IEEE 754 double division), but detecting an ill-posed topology (e.g.
// a short across an ideal voltage source) is netlist/solver validation, not
// this component's concern — admittance() never clamps or substitutes a
// fallback value (repo standard: no fallbacks, no mock data).
//
// Physics in double (FR-022). Header-only, zero-overhead, no heap, no I/O.
// Platform independence (Constitution IV): standard library only.

namespace acfx {

struct Resistor {
    NodeId a;
    NodeId b;
    double R;

    // Conductance G = 1/R contributed between terminals a and b.
    constexpr double admittance() const noexcept {
        return 1.0 / R;
    }
};

} // namespace acfx
