#pragma once

// Companion — the backward-Euler companion-model output for one reactive
// element at a single timestep (contracts/component-physics.md "Reactive";
// research.md R3; data-model.md). Discretising a reactive element (capacitor
// or inductor) with the backward-Euler rule yields, at each step, a Norton
// companion: an equivalent conductance `Geq` in parallel with a history
// current source `Ieq`. Both the capacitor and the inductor produce this same
// pair, so the type is shared here rather than duplicated per element.
//
// The solver stamps `Geq` into the nodal conductance matrix and `Ieq` into the
// right-hand-side current vector; forming those contributions is the solver's
// job, not the element's (the element only reports the pair).
//
// Physics in double (FR-022). Header-only, zero-overhead, no heap, no I/O.
// Platform independence (Constitution IV): standard library only.

namespace acfx {

// Backward-Euler equivalent conductance (Geq) and history current (Ieq) for a
// single reactive element at one timestep.
struct Companion {
    double Geq;
    double Ieq;
};

} // namespace acfx
