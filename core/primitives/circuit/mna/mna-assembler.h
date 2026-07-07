#pragma once

#include "primitives/circuit/components.h"
#include "primitives/circuit/mna/mna-system.h"
#include "primitives/circuit/netlist.h"
#include "primitives/circuit/node.h"

#include <array>
#include <cassert>
#include <cmath>
#include <cstddef>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <variant>

// MnaAssembler — Layer 2 of the Modified Nodal Analysis primitive: the single
// authoritative site that maps each Netlist Component onto MnaSystem stamps
// (contracts/mna-assembler.md; research D2/D3/D4/D6/D7; data-model.md
// "MnaAssembler"). MnaSystem knows only how to stamp and solve a bordered
// [G B; C 0] system; MnaAssembler is what turns "a Resistor between a and b" or
// "an ideal VoltageSource between p and n" into the right stamps.
//
// Two-phase surface (D4):
//   - plan(nl, sys)   : the THROWING, once-per-topology pass. Walks the netlist,
//                       allocates one branch per element that needs a branch
//                       unknown (ideal voltage sources now; op-amps in T009),
//                       records the component -> branch map, and validates
//                       MNA-specific preconditions (degenerate R, out-of-range
//                       node) that the netlist's own prepare() does not cover.
//                       Overflow of the branch capacity is enforced by
//                       MnaSystem::addBranch() (the only throwing engine method).
//   - refresh(nl, comps, sys) : the noexcept, allocation-free, hot-path pass. It
//                       resets the system and re-stamps every element's value
//                       for this solve using the fixed plan. It NEVER calls
//                       addBranch (D4) and never validates — plan() already did.
//
// Element -> stamp mapping (contracts/mna-assembler.md, the authoritative table):
//   Resistor{a,b,R}      -> stampConductance(a, b, 1/R)                 (no branch)
//   CurrentSource{p,n,I} -> stampRhsCurrent(p,+I), stampRhsCurrent(n,-I)(no branch)
//   VoltageSource{p,n,V} -> addBranch()@plan; stampBranchIncidence(k,p,n)
//                           + stampBranchValue(k,V); zero branch diagonal
//                           (ideal source: no series resistance). GROUNDED and
//                           FLOATING alike (SC-005) — the incidence stamp drops
//                           the ground column and handles two interior terminals
//                           uniformly.
//   OpAmp                -> nullor border: LATER task T009 (extension point).
//   Capacitor/Inductor/  -> Norton companion from CompanionSupply: LATER task
//     Diode                 T012 (extension point; no branch).
//
// CompanionSupply (the sibling seam, D6): refresh() is templated on any type
// exposing `Companion at(int componentIndex) const noexcept`. MNA never computes
// a companion — newton-iteration / implicit-integration supply them; a test
// harness supplies a trivial one. US1 contains no reactive/nonlinear element, so
// the supply is never consulted here.
//
// RT-safety (Principle VI): plan() may throw and runs off the audio path;
// refresh()/reads are noexcept and heap-free — the only storage is a
// fixed-capacity std::array sized by the template parameters. No fallbacks
// (Principle V): an unrepresentable/degenerate element throws at plan time,
// never a silent no-op or a gmin substitution.
//
// C++17, standard library only; no platform or component-graphics headers.
// double throughout.

namespace acfx::mna {

template <int MaxNodes, int MaxComponents, int MaxBranches>
class MnaAssembler {
public:
    static_assert(MaxNodes >= 1, "MnaAssembler requires MaxNodes >= 1 (ground)");
    static_assert(MaxComponents >= 1,
                  "MnaAssembler requires MaxComponents >= 1");
    static_assert(MaxBranches >= 0, "MnaAssembler requires MaxBranches >= 0");

    // Plan phase (D4): allocate branch unknowns, record the component->branch
    // map, and validate MNA-specific preconditions. THROWS (off the hot path):
    //   - MnaSystem::addBranch() throws std::length_error on branch overflow.
    //   - a degenerate resistor (R <= 0, non-finite) throws std::invalid_argument.
    //   - a node id outside [0, MaxNodes) throws std::out_of_range (a stamp would
    //     otherwise corrupt the matrix). The netlist's prepare() validates against
    //     its own nodeCount; this is the MNA storage-capacity guard.
    // No fallback: an unrepresentable element throws, never a silent skip.
    void plan(const Netlist<MaxNodes, MaxComponents>& nl,
              MnaSystem<MaxNodes, MaxBranches>& sys) {
        branchOf_.fill(kNoBranch);

        const auto comps = nl.components();
        for (std::size_t i = 0; i < comps.size(); ++i) {
            const Component& c = comps[i];
            std::visit(
                [&](const auto& elem) {
                    using T = std::decay_t<decltype(elem)>;
                    if constexpr (std::is_same_v<T, Resistor>) {
                        validateNode(elem.a, i);
                        validateNode(elem.b, i);
                        validateResistor(elem, i);
                        branchOf_[i] = kNoBranch;
                    } else if constexpr (std::is_same_v<T, CurrentSource>) {
                        validateNode(elem.p, i);
                        validateNode(elem.n, i);
                        branchOf_[i] = kNoBranch;
                    } else if constexpr (std::is_same_v<T, VoltageSource>) {
                        validateNode(elem.p, i);
                        validateNode(elem.n, i);
                        // Ideal source: one branch-current unknown, grounded or
                        // floating alike. addBranch() is the only throwing engine
                        // method (branch-capacity overflow).
                        branchOf_[i] = sys.addBranch();
                    } else if constexpr (std::is_same_v<T, OpAmp>) {
                        // OpAmp: nullor border (one branch injecting the norator
                        // current at `out`, one nullator constraint row). Branch
                        // allocated in T009 — deliberately no-op here; US1 has no
                        // op-amp. Left as a labeled extension point.
                        branchOf_[i] = kNoBranch;
                    } else {
                        // Capacitor / Inductor / Diode: companion-stamped in T012
                        // (Norton companion from the CompanionSupply, no branch).
                        // No-op here; US1 has none. Labeled extension point.
                        branchOf_[i] = kNoBranch;
                    }
                },
                c);
        }

        planned_ = true;
    }

    // Refresh phase (D4): reset the system and re-stamp every element's value for
    // this solve using the fixed plan. noexcept, allocation-free, NEVER calls
    // addBranch. Precondition: plan() has run (guarded by planned_); the callers
    // always plan() first.
    //
    // CompanionSupply is any type with `Companion at(int) const noexcept`
    // (contracts/mna-assembler.md). US1 consults it for no element.
    template <class CompanionSupply>
    void refresh(const Netlist<MaxNodes, MaxComponents>& nl,
                 const CompanionSupply& comps,
                 MnaSystem<MaxNodes, MaxBranches>& sys) const noexcept {
        assert(planned_ && "MnaAssembler::refresh called before plan()");
        (void)comps;  // consulted only by the T012 reactive/nonlinear cases.

        sys.reset();

        const auto components = nl.components();
        for (std::size_t i = 0; i < components.size(); ++i) {
            const Component& c = components[i];
            const int branch = branchOf_[i];
            std::visit(
                [&](const auto& elem) {
                    using T = std::decay_t<decltype(elem)>;
                    if constexpr (std::is_same_v<T, Resistor>) {
                        // Conductance stamp: 1/R (plan() guaranteed R > 0).
                        sys.stampConductance(elem.a, elem.b, 1.0 / elem.R);
                    } else if constexpr (std::is_same_v<T, CurrentSource>) {
                        // Pure RHS: +I at p, -I at n.
                        sys.stampRhsCurrent(elem.p, +elem.I);
                        sys.stampRhsCurrent(elem.n, -elem.I);
                    } else if constexpr (std::is_same_v<T, VoltageSource>) {
                        // Ideal source on its planned branch k: incidence couples
                        // p/n to the branch column+row, value pins v(p)-v(n)=V.
                        // No stampBranchResistance => zero branch diagonal, which
                        // is exactly the nullor/ideal-source constraint (grounded
                        // and floating alike, SC-005).
                        sys.stampBranchIncidence(branch, elem.p, elem.n);
                        sys.stampBranchValue(branch, elem.V);
                    } else if constexpr (std::is_same_v<T, OpAmp>) {
                        // OpAmp nullor border stamp: T009. No-op here (US1 has no
                        // op-amp). Labeled extension point.
                        (void)branch;
                    } else {
                        // Capacitor / Inductor / Diode: stamp the Norton companion
                        // comps.at(static_cast<int>(i)) as conductance Geq +
                        // RHS Ieq: T012. No-op here (US1 has none). Extension point.
                    }
                },
                c);
        }
    }

    // True once plan() has run. Exposed for callers/tests that want to assert the
    // two-phase ordering (refresh() itself asserts on it).
    bool planned() const noexcept { return planned_; }

private:
    // Sentinel for "this component is not branch-augmented" (Resistor,
    // CurrentSource, and the companion elements).
    static constexpr int kNoBranch = -1;

    // MNA storage-capacity node guard: a node id must land in [0, MaxNodes) or a
    // stamp would write outside the fixed matrix. Ground (0) is valid.
    static void validateNode(NodeId node, std::size_t componentIndex) {
        if (node < 0 || node >= MaxNodes) {
            throw std::out_of_range(
                "MnaAssembler::plan: component " +
                std::to_string(componentIndex) +
                " references node id " + std::to_string(node) +
                " outside the assembler's [0, MaxNodes) range (MaxNodes = " +
                std::to_string(MaxNodes) + ")");
        }
    }

    // A resistor's conductance is 1/R; R <= 0 (or non-finite) has no finite,
    // physical conductance and would inject inf/NaN into the matrix. Reject it at
    // plan time rather than stamp a silent fallback (Principle V).
    static void validateResistor(const Resistor& r, std::size_t componentIndex) {
        if (!(r.R > 0.0) || !std::isfinite(r.R)) {
            throw std::invalid_argument(
                "MnaAssembler::plan: component " +
                std::to_string(componentIndex) +
                " is a degenerate resistor (R must be finite and > 0, got R = " +
                std::to_string(r.R) + ")");
        }
    }

    // ---- fixed-capacity state (NO heap, data-model.md "MnaAssembler") --------

    // Component index -> planned branch index, or kNoBranch (-1) when the element
    // is not branch-augmented. Sized by MaxComponents; fixed by plan().
    std::array<int, MaxComponents> branchOf_{};

    // Two-phase guard: refresh() before plan() is a precondition violation (D4).
    bool planned_ = false;
};

}  // namespace acfx::mna
