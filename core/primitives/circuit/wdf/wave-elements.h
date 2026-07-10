#pragma once

#include <stdexcept>

#include "primitives/circuit/wdf/one-port.h"

// wave-elements.h — adaptable wave-domain leaves for the WDF primitive family
// (contracts/wdf-one-ports.md "Adaptable leaves"; data-model.md "Adaptable
// one-ports"). Each type here is its OWN wave-domain type in namespace
// acfx::wdf -- it does NOT reuse the nodal structs in
// core/primitives/circuit/models/ and carries NO NodeId (WDF topology is a
// tree assembled by sibling adaptor nodes, not the nodal solver's graph;
// data-model.md "Consumed existing types (unchanged)", research R5).
//
// Every leaf here satisfies the duck-typed OnePort concept (one-port.h,
// is_one_port_v) with isAdaptable == true: reflected() is valid BEFORE this
// sample's incident() (I2). The wave path (portResistance/reflected/
// incident) is noexcept, heap-free, lock-free (G1 RT-safety). The only
// throw is construction-time parameter validation (G2 no fallback) --
// non-physical parameters throw std::invalid_argument; nothing is ever
// clamped.

namespace acfx::wdf {

// Resistor — the wave-domain ohmic one-port (contracts/wdf-one-ports.md
// "Resistor" R1-R3; data-model.md "Resistor").
//
// Adapted (reflection-free): the port resistance IS the port's own
// resistance R, so the reflected wave is always 0 regardless of incident
// history -- a resistor terminated in its own reference resistance absorbs
// all incident power. R3: the general unadapted reflection
// b = a*(R-Rp)/(R+Rp) for an ARBITRARY reference resistance Rp is the
// adaptor layer's concern (FR-002), not this leaf's -- Resistor exposes no
// such method.
class Resistor {
public:
    // R1: throws std::invalid_argument if R <= 0 (construction, off the
    // hot path). No fallback, no clamping (repo standard).
    explicit Resistor(double R) : R_(R) {
        if (!(R > 0.0)) {
            throw std::invalid_argument("Resistor: R must be > 0 (got a non-positive resistance)");
        }
    }

    // R2: portResistance() == R.
    double portResistance() const noexcept {
        return R_;
    }

    // R2: adapted -- reflected() == 0 for any incident history (memoryless).
    double reflected() const noexcept {
        return 0.0;
    }

    // Adapted, memoryless: the incident wave carries no information the
    // resistor needs to retain -- it is fully absorbed.
    void incident(double /*a*/) noexcept {}

    static constexpr bool isAdaptable = true;

private:
    double R_;
};

static_assert(is_one_port_v<Resistor>, "Resistor must satisfy the OnePort concept trait");

}  // namespace acfx::wdf
