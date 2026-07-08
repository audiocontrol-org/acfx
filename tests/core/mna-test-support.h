#pragma once

#include <array>
#include <cassert>
#include <cstddef>

#include "primitives/circuit/models/companion.h"

// Shared test harness for the mna-assembler suites -- split across
// mna-assembler-test.cpp (US1 linear/source cases + US2 nullor cases),
// mna-assembler-companions-test.cpp (US3 companion cases), and
// mna-assembler-rtsafety-test.cpp (US4 RT-safety / plan-time-throw cases) to
// stay under the Constitution VII per-file line budget -- the two
// CompanionSupply stand-ins the suites need, in one place so none of them
// duplicates the harness.

namespace mna_test {

using acfx::Companion;

// Trivial CompanionSupply for cases that never contain a capacitor/inductor/
// diode: refresh() must never actually call at() on this -- it exists only
// to satisfy the assembler's per-solve signature (contract "CompanionSupply
// (the sibling seam)"). Returning a zeroed companion is a deliberately inert
// value, not a fallback: if the assembler ever DID call it for a linear-only
// netlist, that would itself be a bug this stub would silently mask, but no
// case using it contains an element that could trigger the call.
struct NoCompanions {
    Companion at(int /*componentIndex*/) const noexcept {
        return Companion{0.0, 0.0};
    }
};

// IndexedCompanions is a hand-written stand-in for the newton-iteration /
// implicit-integration siblings that supply real companions in production: it
// names an EXACT Companion{Geq,Ieq} per component index (`Companion at(int)
// const noexcept`) -- unlike NoCompanions above (one inert value for every
// index, since the linear-only suites never call at() at all).
template <std::size_t N>
struct IndexedCompanions {
    std::array<Companion, N> byIndex{};

    Companion at(int componentIndex) const noexcept {
        // Bounds-checked despite the `at`-shaped name (govern finding): a
        // caller-side index bug here would otherwise index the fixed array
        // out of bounds via plain operator[]. Kept as an assert (rather than
        // byIndex.at(), which throws) to preserve noexcept -- this stand-in
        // is called from MnaAssembler's refresh() hot path, whose CompanionSupply
        // contract is noexcept by type (see mna-assembler-rtsafety-test.cpp).
        assert(componentIndex >= 0 && static_cast<std::size_t>(componentIndex) < N &&
               "IndexedCompanions::at: componentIndex out of range");
        return byIndex[static_cast<std::size_t>(componentIndex)];
    }
};

}  // namespace mna_test
