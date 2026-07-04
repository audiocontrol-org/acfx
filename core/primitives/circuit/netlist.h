#pragma once

#include "primitives/circuit/components.h"
#include "primitives/circuit/node.h"
#include "dsp/span.h"

#include <array>
#include <cstddef>
#include <stdexcept>
#include <string>

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

    // Finalize the build. Minimal for now: it exists so the build/solve seam
    // is stable and callers can already say "I'm done adding". No allocation,
    // and (once T013 lands) it is the ONLY place that validates topology, so
    // the per-sample solve path stays throw-free / alloc-free (FR-011).
    void prepare() {
        // T013: floating-node + missing-ground validation lands here.
        // (contracts/netlist.md prepare() contract: on the first failing
        // check raise a distinct, descriptive error — "missing ground
        // reference" / "floating node N" / "netlist over capacity". Until
        // then prepare() is a no-op finalizer that neither throws nor
        // allocates.)
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
