#pragma once

#include "primitives/circuit/components.h"
#include "primitives/circuit/node.h"

#include <type_traits>
#include <variant>

// Collision-free per-component TOPOLOGY signature for the implicit-integration
// plan-drift guard: the variant discriminant (component kind) folded with every
// terminal node id in a positional base-(MaxNodes+1) encoding. Two components
// with the same kind AND the same terminal node ids (in the same order) produce
// the same value; ANY differing kind or node id — a same-count resistor→source
// swap, a diode moved to another slot, a same-kind terminal rewiring — produces
// a different one. Element VALUES are intentionally NOT folded (they vary across
// refreshes; only topology is fingerprinted). noexcept, allocation-free.
//
// This deliberately mirrors NewtonSolver::topoSig (the SAME definition) so the
// integrator's step() guard is exactly as strong as Newton's — a netlist whose
// topology drifted from plan() is rejected before any companion compute / MNA
// refresh / Newton solve indexes the stale plan. This feature does not modify
// the shipped Newton sibling (its charter), so the definition is replicated
// here; folding both onto one shared vocabulary primitive is a follow-on
// (backlog: share-component-topology-signature).

namespace acfx::integration {

template <int MaxNodes>
inline unsigned long long componentTopoSig(const Component& c) noexcept {
    static_assert(MaxNodes >= 1 && MaxNodes <= 1000000,
                  "componentTopoSig requires 1 <= MaxNodes <= 1e6 so the "
                  "base-(MaxNodes+1) terminal fold cannot overflow");
    constexpr unsigned long long kBase =
        static_cast<unsigned long long>(MaxNodes) + 1ULL;
    unsigned long long sig = static_cast<unsigned long long>(c.index()) + 1ULL;
    const auto fold = [&sig](NodeId n) {
        sig = sig * kBase + static_cast<unsigned long long>(n + 1);
    };
    std::visit(
        [&](const auto& e) {
            using T = std::decay_t<decltype(e)>;
            if constexpr (std::is_same_v<T, Resistor>) {
                fold(e.a); fold(e.b);
            } else if constexpr (std::is_same_v<T, Capacitor>) {
                fold(e.a); fold(e.b);
            } else if constexpr (std::is_same_v<T, Inductor>) {
                fold(e.a); fold(e.b);
            } else if constexpr (std::is_same_v<T, VoltageSource>) {
                fold(e.p); fold(e.n);
            } else if constexpr (std::is_same_v<T, CurrentSource>) {
                fold(e.p); fold(e.n);
            } else if constexpr (std::is_same_v<T, Diode>) {
                fold(e.anode); fold(e.cathode);
            } else if constexpr (std::is_same_v<T, OpAmp>) {
                fold(e.inPlus); fold(e.inMinus); fold(e.out);
            }
        },
        c);
    return sig;
}

}  // namespace acfx::integration
