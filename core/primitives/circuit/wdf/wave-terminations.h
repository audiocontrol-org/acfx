#pragma once

#include <stdexcept>

#include "primitives/circuit/wdf/one-port.h"

// wave-terminations.h — boundary-condition one-ports for the WDF primitive
// family (contracts/wdf-one-ports.md "ResistiveTermination" RT1,
// "ShortCircuit" SH1-SH2, "OpenCircuit" OP1; data-model.md "ResistiveTermination"
// / "Reflective one-ports"; research R6). Each type here is its OWN
// wave-domain type in namespace acfx::wdf -- it does NOT reuse the nodal
// structs in core/primitives/circuit/models/ and carries NO NodeId (WDF
// topology is a tree assembled by sibling adaptor nodes, not the nodal
// solver's graph; data-model.md "Consumed existing types (unchanged)",
// research R5).
//
// ResistiveTermination is ADAPTABLE (isAdaptable == true): a matched load,
// behaviorally identical to Resistor -- reflected() is valid BEFORE this
// sample's incident() (I2).
//
// ShortCircuit and OpenCircuit are REFLECTIVE (isAdaptable == false): the
// call ordering INVERTS (I2) -- incident(a) is called FIRST (storing the
// wave), THEN reflected() returns a function of that stored wave. Their
// reflection is Rp-INDEPENDENT (research R6); the externally-supplied Rp is
// carried only for junction wave<->KCL conversion at the adaptor layer, not
// consulted by reflected() itself.
//
// The wave path (portResistance/reflected/incident) is noexcept, heap-free,
// lock-free (G1 RT-safety). The only throw is construction-time parameter
// validation (G2 no fallback) -- non-physical parameters throw
// std::invalid_argument; nothing is ever clamped.

namespace acfx::wdf {

// ResistiveTermination — the wave-domain matched-load one-port
// (contracts/wdf-one-ports.md "ResistiveTermination" RT1; data-model.md
// "ResistiveTermination"): a resistor used as a boundary termination.
//
// Adapted (reflection-free): the port resistance IS the termination's own
// resistance R, so the reflected wave is always 0 regardless of incident
// history -- a load terminated in its own reference resistance absorbs all
// incident power. Behaviorally identical to Resistor; kept as a distinct
// type to name the boundary-condition intent at tree leaves.
class ResistiveTermination {
public:
    // RT1: throws std::invalid_argument if R <= 0 (construction, off the
    // hot path). No fallback, no clamping (repo standard).
    explicit ResistiveTermination(double R) : R_(R) {
        if (!(R > 0.0)) {
            throw std::invalid_argument(
                "ResistiveTermination: R must be > 0 (got a non-positive resistance)");
        }
    }

    // RT1: portResistance() == R.
    double portResistance() const noexcept {
        return R_;
    }

    // RT1: adapted -- reflected() == 0 (matched load) for any incident
    // history.
    double reflected() const noexcept {
        return 0.0;
    }

    // Adapted, memoryless: the incident wave carries no information the
    // termination needs to retain -- it is fully absorbed.
    void incident(double /*a*/) noexcept {}

    static constexpr bool isAdaptable = true;

private:
    double R_;
};

static_assert(is_one_port_v<ResistiveTermination>,
              "ResistiveTermination must satisfy the OnePort concept trait");

// ShortCircuit — the wave-domain short-circuit boundary one-port
// (contracts/wdf-one-ports.md "ShortCircuit" SH1-SH2; data-model.md
// "ShortCircuit"; research R6): v = 0 at the port, so b = -a.
//
// Reflective (isAdaptable == false): the call order INVERTS relative to the
// adaptable leaves (I2) -- incident(a) is called FIRST, storing the wave;
// reflected() then returns -a_ (the negation of the last stored incident
// wave). The reflection is independent of Rp; Rp is carried purely as the
// externally-imposed reference resistance an adaptor needs for junction
// wave<->KCL conversion, never consulted by reflected() itself.
class ShortCircuit {
public:
    // SH1: throws std::invalid_argument if Rp <= 0 (construction, off the
    // hot path). No fallback, no clamping (repo standard).
    explicit ShortCircuit(double Rp) : Rp_(Rp) {
        if (!(Rp > 0.0)) {
            throw std::invalid_argument(
                "ShortCircuit: Rp must be > 0 (got a non-positive reference resistance)");
        }
    }

    // SH1: portResistance() == Rp (externally-imposed reference; not
    // consulted by reflected()).
    double portResistance() const noexcept {
        return Rp_;
    }

    // SH2: reflective -- valid only AFTER this sample's incident(a) (I2).
    // Returns -a_ (the negation of the last stored incident wave),
    // independent of Rp (SH1).
    double reflected() const noexcept {
        return -a_;
    }

    // SH2: store this sample's incident wave; the following reflected()
    // returns its negation.
    void incident(double a) noexcept {
        a_ = a;
    }

    static constexpr bool isAdaptable = false;

private:
    double Rp_;
    double a_ = 0.0;  // last incident wave (set by incident())
};

static_assert(is_one_port_v<ShortCircuit>, "ShortCircuit must satisfy the OnePort concept trait");

// OpenCircuit — the wave-domain open-circuit boundary one-port
// (contracts/wdf-one-ports.md "OpenCircuit" OP1; data-model.md
// "OpenCircuit"; research R6): i = 0 at the port, so b = +a.
//
// Reflective (isAdaptable == false), same inverted call ordering as
// ShortCircuit (I2): incident(a) first, then reflected() returns +a_. Same
// Rp-independence (OP1) -- Rp is carried only for junction wave<->KCL
// conversion.
class OpenCircuit {
public:
    // OP1: throws std::invalid_argument if Rp <= 0. No fallback, no
    // clamping.
    explicit OpenCircuit(double Rp) : Rp_(Rp) {
        if (!(Rp > 0.0)) {
            throw std::invalid_argument(
                "OpenCircuit: Rp must be > 0 (got a non-positive reference resistance)");
        }
    }

    // OP1: portResistance() == Rp (externally-imposed reference; not
    // consulted by reflected()).
    double portResistance() const noexcept {
        return Rp_;
    }

    // OP1: reflective -- valid only AFTER this sample's incident(a) (I2).
    // Returns +a_ (the last stored incident wave), independent of Rp.
    double reflected() const noexcept {
        return a_;
    }

    // Store this sample's incident wave; the following reflected() returns
    // it unchanged.
    void incident(double a) noexcept {
        a_ = a;
    }

    static constexpr bool isAdaptable = false;

private:
    double Rp_;
    double a_ = 0.0;  // last incident wave (set by incident())
};

static_assert(is_one_port_v<OpenCircuit>, "OpenCircuit must satisfy the OnePort concept trait");

}  // namespace acfx::wdf
