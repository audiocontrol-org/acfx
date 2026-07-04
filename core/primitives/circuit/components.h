#pragma once

#include "primitives/circuit/models/resistor.h"
#include "primitives/circuit/models/capacitor.h"
#include "primitives/circuit/models/inductor.h"
#include "primitives/circuit/models/sources.h"
#include "primitives/circuit/models/diode.h"

#include <variant>

// Component — the heap-free, vtable-free container over the fixed v1 type
// set (data-model.md "Component (container form)"; contracts/component-
// physics.md; FR-008, research R4). A Component is exactly one of the six
// v1 element kinds, held by value in a std::variant: dispatch is via
// std::visit / std::holds_alternative, never a vtable, never RTTI, never
// the heap.
//
// Classifiers below are what the solver uses to route each component (and,
// later, to enforce the >=2-nonlinearity refusal, FR-016). The three
// buckets partition the fixed type set:
//   - isNonlinear: Diode only.
//   - isReactive:  Capacitor, Inductor (the two companion-model elements).
//   - isLinear:    everything else — Resistor, VoltageSource, CurrentSource.
//     Note VoltageSource/CurrentSource land here for routing purposes even
//     though neither exposes an admittance() (sources.h): they are still
//     neither nonlinear nor reactive, so "linear" is the correct bucket for
//     a solver deciding how to iterate/stamp them.
//
// Header-only, zero-overhead, no heap, no I/O. Platform independence
// (Constitution IV): standard library only; no desktop or MCU
// platform-specific headers.

namespace acfx {

using Component = std::variant<Resistor, Capacitor, Inductor, VoltageSource,
                                CurrentSource, Diode>;

// True iff `c` holds the nonlinear element (Diode). The only nonlinearity in
// the v1 type set.
inline bool isNonlinear(const Component& c) {
    return std::holds_alternative<Diode>(c);
}

// True iff `c` holds a reactive (companion-model) element: Capacitor or
// Inductor.
inline bool isReactive(const Component& c) {
    return std::holds_alternative<Capacitor>(c) ||
           std::holds_alternative<Inductor>(c);
}

// True iff `c` holds a linear element for solver-routing purposes: anything
// that is neither nonlinear nor reactive — Resistor, VoltageSource, or
// CurrentSource. Sources are conductance-free (sources.h) but still belong
// to this bucket: routing only distinguishes "needs Newton iteration"
// (nonlinear) and "needs a companion model" (reactive) from everything else.
inline bool isLinear(const Component& c) {
    return !isNonlinear(c) && !isReactive(c);
}

} // namespace acfx
