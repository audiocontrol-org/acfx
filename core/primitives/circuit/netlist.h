#pragma once

#include "primitives/circuit/components.h"
#include "primitives/circuit/node.h"
#include "dsp/span.h"

#include <array>
#include <cstddef>
#include <stdexcept>
#include <string>
#include <utility>
#include <variant>

// Netlist<MaxNodes, MaxComponents> — the fixed-capacity, heap-free container
// that owns circuit topology (data-model.md "Netlist"; contracts/netlist.md;
// FR-009/010/011). Both capacities are compile-time template parameters
// (FR-009, OQ3): the entire backing store is a std::array sized at
// instantiation, so there is NO heap growth, NO new/delete, and NO
// std::vector anywhere in this type. Over-capacity is a hard, descriptive
// error, never a silent truncation (FR-010).
//
// Node numbering convention (data-model.md "NodeId / node set"; node.h):
// node 0 is ground and exists IMPLICITLY — a freshly constructed Netlist
// already has nodeCount() == 1 (just ground). Callers do NOT addNode() the
// ground node; addNode() allocates the next NON-ground node, so the first
// call returns 1, the second 2, and so on. This is the cleaner of the two
// conventions (the alternative — forcing every caller to addNode() ground
// first — invites off-by-one bugs and a caller that forgets ground), and it
// is what the divider / RC test circuits assume: addNode() only ever names a
// real interior node, ground is always kGround == 0.
//
// Components are held BY VALUE in the array (Component is the vtable-free,
// heap-free std::variant from components.h), so the Netlist owns its physics
// with no indirection. components() hands a solver an immutable view over
// exactly the in-use prefix (acfx::span, dsp/span.h — the one span vocabulary
// that compiles under both C++17 and C++20; std::span is C++20-only and this
// header must stay C++17).
//
// prepare() finalizes the build and, once landed, validates topology; a
// solver that accepts a prepared Netlist may assume a well-posed circuit
// (contracts/netlist.md). See the T013 marker inside prepare(): the full
// floating-node / missing-ground / capacity validation is a separate task.
// The per-sample solve path that consumes a prepared Netlist does not throw
// and does not allocate (FR-011) — every check that can throw lives on the
// build/prepare side, never in the read accessors below.
//
// Header-only, no heap, no I/O. Platform independence (Constitution IV):
// standard library only; no desktop or MCU platform-specific headers.
// Exceptions are raised only on the control/build thread (add / addNode /
// prepare), never on the audio path.

namespace acfx {

// The two terminal nodes of a component, regardless of element kind. Every
// passive/source element is two-terminal, but the field names differ (a/b, p/n,
// anode/cathode); this normalizes them so topology validation can treat any
// Component uniformly. Used by prepare() for both the missing-ground scan and
// the floating-node connectivity build. No allocation, no throw.
//
// OpAmp is a three-terminal nullor but reports only its INPUT pair
// {inPlus, inMinus} here — the two terminals the virtual-short constraint spans
// (opamp.h; research R1). Its `out` terminal is the norator-driven output,
// reported by the solver's bordered augmentation (research R2) rather than as a
// passive two-terminal edge, so it is deliberately not surfaced by terminalsOf.
inline std::pair<NodeId, NodeId> terminalsOf(const Component& c) noexcept {
    if (const auto* r = std::get_if<Resistor>(&c)) return {r->a, r->b};
    if (const auto* cap = std::get_if<Capacitor>(&c)) return {cap->a, cap->b};
    if (const auto* l = std::get_if<Inductor>(&c)) return {l->a, l->b};
    if (const auto* v = std::get_if<VoltageSource>(&c)) return {v->p, v->n};
    if (const auto* i = std::get_if<CurrentSource>(&c)) return {i->p, i->n};
    if (const auto* op = std::get_if<OpAmp>(&c)) return {op->inPlus, op->inMinus};
    const auto& d = std::get<Diode>(c);
    return {d.anode, d.cathode};
}

// True iff the element guarantees a conductive path between its two terminals
// in the discretized backward-Euler system, and therefore contributes an edge
// to the floating-node connectivity check (contracts/netlist.md; FR-010):
//   - Resistor      -> 1/R
//   - Inductor      -> dt/L   (companion conductance)
//   - Capacitor     -> C/dt   (companion conductance)
//   - VoltageSource -> pins its two nodes together
// CurrentSource, Diode, and OpAmp are deliberately EXCLUDED: a current source
// into an otherwise-isolated node, a lone reverse-biased diode, or an op-amp
// output does not guarantee a DC/operating path, so none counts toward
// reachability to ground. For the OpAmp specifically, the output terminal is
// norator-driven and provides no passive conductance; the feedback network
// supplies reachability (research R5). This nodal scan is only a fast,
// conservative PRE-FILTER: once the system is bordered with nullor constraint
// rows, the AUTHORITATIVE well-posedness gate is the non-singularity of the
// augmented system at solve time (research R2/R5), not this connectivity check.
inline bool contributesConductivePath(const Component& c) noexcept {
    return std::holds_alternative<Resistor>(c) ||
           std::holds_alternative<Inductor>(c) ||
           std::holds_alternative<Capacitor>(c) ||
           std::holds_alternative<VoltageSource>(c);
}

template <int MaxNodes, int MaxComponents>
class Netlist {
public:
    static_assert(MaxNodes > 0, "Netlist requires MaxNodes >= 1 (ground node 0)");
    static_assert(MaxComponents > 0, "Netlist requires MaxComponents >= 1");

    // Allocate the next non-ground node and return its handle (1, 2, ...).
    // Ground (node 0) is implicit and already counted at construction, so the
    // first call returns 1. Throws a descriptive std::out_of_range naming the
    // MaxNodes limit when the node set is already full — no heap growth, no
    // silent reuse (FR-010).
    NodeId addNode() {
        if (nodeCount_ >= MaxNodes) {
            throw std::out_of_range(
                "Netlist::addNode: node capacity exceeded (MaxNodes = " +
                std::to_string(MaxNodes) + ")");
        }
        return nodeCount_++;
    }

    // Append a component by value. Throws a descriptive std::out_of_range
    // naming the MaxComponents limit when the store is full — no heap growth,
    // no truncation (FR-010). The component is copied into the fixed array.
    void add(const Component& c) {
        if (count_ >= MaxComponents) {
            throw std::out_of_range(
                "Netlist::add: component capacity exceeded (MaxComponents = " +
                std::to_string(MaxComponents) + ")");
        }
        components_[static_cast<std::size_t>(count_)] = c;
        ++count_;
    }

    // Finalize the build and validate topology. This is the ONLY place that
    // validates the circuit, so the per-sample solve path that consumes a
    // prepared Netlist stays throw-free / alloc-free (FR-011). Every check
    // runs against the in-use prefix; on the FIRST failing check prepare()
    // raises a DISTINCT, descriptive exception (contracts/netlist.md,
    // FR-010/011) — no fallback, no silent repair.
    //
    // Checks, in order:
    //   1. Missing ground — node 0 must be touched by at least one component
    //      terminal, else the system has no reference (std::invalid_argument).
    //   2. Floating node — every non-ground node in [1, nodeCount()) must have
    //      a conductive path to ground through Resistor / Inductor / Capacitor
    //      / VoltageSource edges (std::invalid_argument naming the node).
    // Over-capacity is already enforced at add()/addNode() time
    // (std::out_of_range), so the three failure modes stay distinct and
    // independently assertable.
    //
    // Allocation: STACK only — union-find parent[] is a std::array sized by the
    // MaxNodes template parameter, so prepare() never touches the heap.
    void prepare() {
        // Check 0: every component terminal must reference an in-use node in
        // [0, nodeCount_). This runs FIRST, before the union-find below indexes
        // parent[terminal]: a terminal in [nodeCount_, MaxNodes) would otherwise
        // read a value-initialized parent slot (== 0) and silently unite with
        // ground, letting a malformed/floating netlist pass validation; a
        // terminal >= MaxNodes would be out-of-bounds. node.h::isValidNode is
        // exactly this predicate. (Audit AUDIT-BARRAGE-01, cross-model HIGH.)
        for (int i = 0; i < count_; ++i) {
            const auto t = terminalsOf(components_[static_cast<std::size_t>(i)]);
            if (!isValidNode(t.first, nodeCount_) ||
                !isValidNode(t.second, nodeCount_)) {
                throw std::invalid_argument(
                    "component-abstractions netlist: component " +
                    std::to_string(i) +
                    " references an out-of-range node (valid range is [0, " +
                    std::to_string(nodeCount_) + "))");
            }
        }

        // Check 1: missing ground. Any terminal of any component kind counts
        // as a reference to ground (kGround == node 0).
        bool groundReferenced = false;
        for (int i = 0; i < count_; ++i) {
            const auto t = terminalsOf(components_[static_cast<std::size_t>(i)]);
            if (t.first == kGround || t.second == kGround) {
                groundReferenced = true;
                break;
            }
        }
        if (!groundReferenced) {
            throw std::invalid_argument(
                "component-abstractions netlist: missing ground reference "
                "(no component connected to node 0)");
        }

        // Check 2: floating node. Union-find over conductive edges; then every
        // non-ground node must share ground's connected component.
        std::array<int, static_cast<std::size_t>(MaxNodes)> parent{};
        for (int i = 0; i < nodeCount_; ++i) {
            parent[static_cast<std::size_t>(i)] = i;
        }

        // Iterative find with path halving (no recursion, no allocation).
        const auto find = [&parent](int x) noexcept {
            while (parent[static_cast<std::size_t>(x)] != x) {
                const int grand =
                    parent[static_cast<std::size_t>(
                        parent[static_cast<std::size_t>(x)])];
                parent[static_cast<std::size_t>(x)] = grand;
                x = grand;
            }
            return x;
        };
        const auto unite = [&parent, &find](int a, int b) noexcept {
            const int ra = find(a);
            const int rb = find(b);
            if (ra != rb) {
                parent[static_cast<std::size_t>(ra)] = rb;
            }
        };

        for (int i = 0; i < count_; ++i) {
            const Component& c = components_[static_cast<std::size_t>(i)];
            if (!contributesConductivePath(c)) {
                continue;
            }
            const auto t = terminalsOf(c);
            unite(t.first, t.second);
        }

        const int groundRoot = find(kGround);
        for (int n = 1; n < nodeCount_; ++n) {
            if (find(n) != groundRoot) {
                throw std::invalid_argument(
                    "component-abstractions netlist: floating node " +
                    std::to_string(n) +
                    " (no conductive path to ground)");
            }
        }
    }

    // Number of nodes, INCLUDING implicit ground (so >= 1 always).
    int nodeCount() const noexcept { return nodeCount_; }

    // Number of components currently held.
    int componentCount() const noexcept { return count_; }

    // Immutable view over exactly the in-use component prefix. A solver reads
    // all physics from here. No allocation, no throw.
    span<const Component> components() const noexcept {
        return span<const Component>(components_.data(),
                                     static_cast<std::size_t>(count_));
    }

private:
    // Fixed backing store — sized at instantiation, never grows.
    std::array<Component, MaxComponents> components_{};
    int count_ = 0;

    // Ground (node 0) exists implicitly, so the node count starts at 1.
    int nodeCount_ = 1;
};

} // namespace acfx
