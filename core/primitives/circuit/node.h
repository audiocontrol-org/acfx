#pragma once

// NodeId — the circuit primitive's node-handle vocabulary (data-model.md
// "NodeId / node set"; FR-001). A circuit is a set of integer nodes spanning
// 0 .. nodeCount-1; node 0 is, by convention, ground. NodeId is a plain
// integer index type (physics values elsewhere are double; a node handle is
// not a physical quantity, so it stays integral).
//
// Platform independence (Constitution IV): standard library only; no
// desktop or MCU platform-specific headers. Header-only, zero-overhead,
// no heap, no I/O.

namespace acfx {

// A non-negative index identifying a circuit node.
using NodeId = int;

// Ground is node 0 by convention (FR-001). Every valid netlist references it.
inline constexpr NodeId kGround = 0;

// True if `node` is the ground node.
constexpr bool isGround(NodeId node) noexcept {
    return node == kGround;
}

// True if `node` is a well-formed handle for a circuit of `nodeCount` nodes,
// i.e. node is in [0, nodeCount). Does not by itself imply the node is
// reachable/connected — that is a netlist-level validation concern
// (data-model.md "Validation rules").
constexpr bool isValidNode(NodeId node, int nodeCount) noexcept {
    return node >= 0 && node < nodeCount;
}

} // namespace acfx
