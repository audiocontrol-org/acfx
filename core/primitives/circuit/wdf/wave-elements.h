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

// Capacitor — the wave-domain capacitive one-port under the bilinear transform
// s -> (2/T)(1 - z^-1)/(1 + z^-1), T = dt (contracts/wdf-one-ports.md
// "Capacitor" C1-C2; data-model.md "Capacitor"; research R2).
//
// Bilinear discretization of the admittance Y(s) = sC yields a
// frequency-INDEPENDENT port resistance Rp = T/(2C) in series with a pure UNIT
// DELAY: the reflected wave b[n] equals the PREVIOUS sample's incident wave
// a[n-1]. This unit delay is the property the whole WDF paradigm relies on. The
// leaf stores exactly one wave sample of state (the previous incident); it is
// initialized to 0, so b[0] == 0 before any incident() call.
//
// Adaptable (isAdaptable == true): reflected() is valid BEFORE this sample's
// incident() -- it returns the stored state, independent of the current-sample
// incident wave (I2 call ordering).
class Capacitor {
public:
    // C1: throws std::invalid_argument if C <= 0 or dt <= 0 (construction, off
    // the hot path). No fallback, no clamping (repo standard). Rp = T/(2C) is
    // computed ONCE here and never recomputed on the wave path.
    Capacitor(double C, double dt) : Rp_(dt / (2.0 * C)) {
        if (!(C > 0.0)) {
            throw std::invalid_argument("Capacitor: C must be > 0 (got a non-positive capacitance)");
        }
        if (!(dt > 0.0)) {
            throw std::invalid_argument("Capacitor: dt must be > 0 (got a non-positive timestep)");
        }
    }

    // C1: portResistance() == T/(2C) (the bilinear port resistance).
    double portResistance() const noexcept {
        return Rp_;
    }

    // C2: unit delay -- reflected() returns the previous incident wave
    // (b[n] = a[n-1]); the stored state is 0 initially, so b[0] == 0.
    double reflected() const noexcept {
        return state_;
    }

    // C2: store this sample's incident wave; the next reflected() returns it.
    void incident(double a) noexcept {
        state_ = a;
    }

    static constexpr bool isAdaptable = true;

private:
    double Rp_;
    double state_ = 0.0;  // a[n-1]; init 0 => b[0] = 0
};

static_assert(is_one_port_v<Capacitor>, "Capacitor must satisfy the OnePort concept trait");

// Inductor — the wave-domain inductive one-port, the DUAL of the capacitor
// under the same bilinear transform (contracts/wdf-one-ports.md "Inductor"
// L1-L2, G4 duality; data-model.md "Inductor"; research R2).
//
// Bilinear discretization of the impedance Z(s) = sL yields port resistance
// Rp = 2L/T and a sign-INVERTED unit delay: b[n] = -a[n-1]. Same one stored
// wave sample as the capacitor; only the port resistance form and the reflected
// sign are swapped (G4).
//
// Adaptable (isAdaptable == true), same call ordering as Capacitor (I2).
class Inductor {
public:
    // L1: throws std::invalid_argument if L <= 0 or dt <= 0. Rp = 2L/T computed
    // ONCE in the constructor. No fallback, no clamping.
    Inductor(double L, double dt) : Rp_(2.0 * L / dt) {
        if (!(L > 0.0)) {
            throw std::invalid_argument("Inductor: L must be > 0 (got a non-positive inductance)");
        }
        if (!(dt > 0.0)) {
            throw std::invalid_argument("Inductor: dt must be > 0 (got a non-positive timestep)");
        }
    }

    // L1: portResistance() == 2L/T (the dual bilinear port resistance).
    double portResistance() const noexcept {
        return Rp_;
    }

    // L2: sign-inverted unit delay -- reflected() returns -(previous incident)
    // (b[n] = -a[n-1]); the stored state is 0 initially, so b[0] == 0.
    double reflected() const noexcept {
        return -state_;
    }

    // L2: store this sample's incident wave; the next reflected() returns its
    // negation.
    void incident(double a) noexcept {
        state_ = a;
    }

    static constexpr bool isAdaptable = true;

private:
    double Rp_;
    double state_ = 0.0;  // a[n-1]; init 0 => b[0] = 0
};

static_assert(is_one_port_v<Inductor>, "Inductor must satisfy the OnePort concept trait");

}  // namespace acfx::wdf
