#pragma once

#include <stdexcept>

#include "primitives/circuit/wdf/one-port.h"

// wave-sources.h — adaptable resistive source one-ports for the WDF
// primitive family (contracts/wdf-one-ports.md "ResistiveVoltageSource"
// VS1-VS2, "ResistiveCurrentSource" CS1; data-model.md
// "ResistiveVoltageSource" / "ResistiveCurrentSource"; research R1/R7).
// Each type here is its OWN wave-domain type in namespace acfx::wdf -- it
// does NOT reuse the nodal structs in core/primitives/circuit/models/ and
// carries NO NodeId (WDF topology is a tree assembled by sibling adaptor
// nodes, not the nodal solver's graph; data-model.md "Consumed existing
// types (unchanged)", research R5).
//
// Both leaves satisfy the duck-typed OnePort concept (one-port.h,
// is_one_port_v) with isAdaptable == true: reflected() is valid BEFORE this
// sample's incident() (I2). The wave path (portResistance/reflected/
// incident/setVoltage/setCurrent) is noexcept, heap-free, lock-free (G1
// RT-safety). The only throw is construction-time parameter validation
// (G2 no fallback) -- non-physical parameters throw std::invalid_argument;
// nothing is ever clamped. Neither leaf holds wave state: the per-sample
// drive value (E or I) is the audio input, not delayed state, so
// reflected() tracks the most recent setVoltage()/setCurrent() call, NOT a
// value derived from a prior incident() (memoryless).

namespace acfx::wdf {

// ResistiveVoltageSource — the wave-domain Thevenin source one-port
// (contracts/wdf-one-ports.md "ResistiveVoltageSource" VS1-VS2;
// data-model.md "ResistiveVoltageSource"): a per-sample drive voltage E in
// series with a fixed resistance R.
//
// Adapted (reflection-free): the port resistance IS the source's own series
// resistance R, so the reflected wave equals the drive voltage E regardless
// of incident history -- incident() carries no information the source
// needs to retain (the load's contribution is fully absorbed by the
// matched port resistance).
class ResistiveVoltageSource {
public:
    // VS1: throws std::invalid_argument if R <= 0 (construction, off the
    // hot path). No fallback, no clamping (repo standard). E defaults to 0.
    explicit ResistiveVoltageSource(double R, double E = 0.0) : R_(R), E_(E) {
        if (!(R > 0.0)) {
            throw std::invalid_argument(
                "ResistiveVoltageSource: R must be > 0 (got a non-positive resistance)");
        }
    }

    // VS1: portResistance() == R.
    double portResistance() const noexcept {
        return R_;
    }

    // VS1: adapted -- reflected() == E (the current drive value).
    double reflected() const noexcept {
        return E_;
    }

    // Adapted, memoryless: the incident wave carries no information this
    // source needs to retain -- it is fully absorbed by the matched port
    // resistance.
    void incident(double /*a*/) noexcept {}

    // VS2: updates the per-sample drive value (the audio input); Rp is
    // unaffected. The next reflected() call returns this E.
    void setVoltage(double E) noexcept {
        E_ = E;
    }

    static constexpr bool isAdaptable = true;

private:
    double R_;
    double E_;  // per-sample drive voltage; NOT delayed wave state.
};

static_assert(is_one_port_v<ResistiveVoltageSource>,
              "ResistiveVoltageSource must satisfy the OnePort concept trait");

// ResistiveCurrentSource — the wave-domain Norton source one-port
// (contracts/wdf-one-ports.md "ResistiveCurrentSource" CS1; data-model.md
// "ResistiveCurrentSource"): a per-sample drive current I in parallel with
// a fixed resistance R.
//
// Adapted (reflection-free): the port resistance IS the source's own
// parallel resistance R, so the reflected wave equals R*I regardless of
// incident history -- incident() carries no information the source needs
// to retain, matching ResistiveVoltageSource's memoryless shape.
class ResistiveCurrentSource {
public:
    // CS1: throws std::invalid_argument if R <= 0 (construction, off the
    // hot path). No fallback, no clamping (repo standard). I defaults to 0.
    explicit ResistiveCurrentSource(double R, double I = 0.0) : R_(R), I_(I) {
        if (!(R > 0.0)) {
            throw std::invalid_argument(
                "ResistiveCurrentSource: R must be > 0 (got a non-positive resistance)");
        }
    }

    // CS1: portResistance() == R.
    double portResistance() const noexcept {
        return R_;
    }

    // CS1: adapted -- reflected() == R * I (the current drive value).
    double reflected() const noexcept {
        return R_ * I_;
    }

    // Adapted, memoryless: the incident wave carries no information this
    // source needs to retain -- it is fully absorbed by the matched port
    // resistance.
    void incident(double /*a*/) noexcept {}

    // Updates the per-sample drive value; Rp is unaffected. The next
    // reflected() call returns R * this I.
    void setCurrent(double I) noexcept {
        I_ = I;
    }

    static constexpr bool isAdaptable = true;

private:
    double R_;
    double I_;  // per-sample drive current; NOT delayed wave state.
};

static_assert(is_one_port_v<ResistiveCurrentSource>,
              "ResistiveCurrentSource must satisfy the OnePort concept trait");

}  // namespace acfx::wdf
