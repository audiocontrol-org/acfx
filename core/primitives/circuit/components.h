#pragma once

#include "primitives/circuit/models/resistor.h"
#include "primitives/circuit/models/capacitor.h"
#include "primitives/circuit/models/inductor.h"
#include "primitives/circuit/models/sources.h"
#include "primitives/circuit/models/diode.h"
#include "primitives/circuit/models/opamp.h"

#include <variant>

// Component — the heap-free, vtable-free container over the fixed type set
// (data-model.md "Component (container form)"; contracts/component-physics.md;
// FR-008, research R4). A Component is exactly one of the element kinds, held
// by value in a std::variant: dispatch is via std::visit /
// std::holds_alternative, never a vtable, never RTTI, never the heap.
//
// The v1 type set had six elements (Resistor, Capacitor, Inductor,
// VoltageSource, CurrentSource, Diode). opamp-stages extends it by EXACTLY ONE
// additive inhabitant — OpAmp, the ideal nullor (opamp-element.md; research
// R1) — with no change to any existing element's semantics.
//
// Classifiers below are what the solver uses to route each component (and,
// later, to enforce the >=2-nonlinearity refusal, FR-016). The three
// buckets partition the type set:
//   - isNonlinear: Diode only.
//   - isReactive:  Capacitor, Inductor (the two companion-model elements).
//   - isLinear:    everything else — Resistor, VoltageSource, CurrentSource,
//     OpAmp. Note VoltageSource/CurrentSource/OpAmp land here for routing
//     purposes even though none exposes an admittance() (sources.h, opamp.h):
//     they are still neither nonlinear nor reactive, so "linear" is the correct
//     bucket for a solver deciding how to iterate/stamp them. The ideal op-amp
//     is linear in its operating region — it carries no nonlinearity and no
//     reactive state (research R1).
//
// Header-only, zero-overhead, no heap, no I/O. Platform independence
// (Constitution IV): standard library only; no desktop or MCU
// platform-specific headers.

namespace acfx {

using Component = std::variant<Resistor, Capacitor, Inductor, VoltageSource,
                                CurrentSource, Diode, OpAmp>;

// True iff `c` holds the nonlinear element (Diode). The only nonlinearity in
// the type set — OpAmp is an ideal LINEAR element and is NOT nonlinear.
inline bool isNonlinear(const Component& c) {
    return std::holds_alternative<Diode>(c);
}

// True iff `c` holds a reactive (companion-model) element: Capacitor or
// Inductor. The ideal OpAmp carries no reactive state and is NOT reactive.
inline bool isReactive(const Component& c) {
    return std::holds_alternative<Capacitor>(c) ||
           std::holds_alternative<Inductor>(c);
}

// True iff `c` holds a linear element for solver-routing purposes: anything
// that is neither nonlinear nor reactive — Resistor, VoltageSource,
// CurrentSource, or OpAmp. Sources and the ideal op-amp are conductance-free
// (sources.h, opamp.h) but still belong to this bucket: routing only
// distinguishes "needs Newton iteration" (nonlinear) and "needs a companion
// model" (reactive) from everything else.
inline bool isLinear(const Component& c) {
    return !isNonlinear(c) && !isReactive(c);
}

} // namespace acfx
