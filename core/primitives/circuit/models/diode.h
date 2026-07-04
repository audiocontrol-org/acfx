#pragma once

#include "primitives/circuit/node.h"

#include <cmath>

// Diode — the circuit primitive's nonlinear element and the reference
// nonlinearity for the whole feature (data-model.md "Diode";
// contracts/component-physics.md "Nonlinear element"; research.md R2;
// FR-004). A two-terminal junction between `anode` and `cathode` whose
// physics is the Shockley law, evaluated at a bias `vAK = V(anode) - V(cathode)`.
//
// SHOCKLEY LAW (the constitutive relation):
//
//     I(vAK) = Is * (exp(vAK / (n*Vt)) - 1)
//
//   where
//     Is  = reverse saturation current (A),
//     n   = ideality (emission) factor (dimensionless, ~1..2),
//     Vt  = thermal voltage k*T/q (~25.85 mV at 300 K),
//     n*Vt = the junction's scaling voltage (SPICE calls this `vte`).
//
//   For vAK >> n*Vt the exponential dominates (forward conduction); for
//   vAK << 0 the exp term vanishes and I -> -Is (reverse saturation). The
//   "-1" is what makes I(0) == 0 exactly.
//
// ANALYTIC SMALL-SIGNAL CONDUCTANCE (the Jacobian a Newton solver needs):
//
//     g(vAK) = dI/dV = d/dV [ Is*(exp(vAK/(n*Vt)) - 1) ]
//                    = (Is / (n*Vt)) * exp(vAK / (n*Vt))
//
//   i.e. the derivative of the exp term; the constant -Is differentiates
//   away. Note g relates to the current by g = (I + Is) / (n*Vt), so g is
//   always > 0 (a diode is passive). We return the closed form directly
//   rather than the (I+Is)/nVt rearrangement so `evaluate` reads as the
//   literal derivative of the law above.
//
// Physics in double (FR-022). Header-only, zero-overhead, no heap, no I/O.
// Platform independence (Constitution IV): standard library only; no
// desktop or MCU platform-specific headers.
//
// The seam: Diode owns *physics only* (evaluate + the junction-limiting
// helpers a Newton step consults). It does not stamp, scatter, or iterate —
// assembling g/I into a system and driving Newton is the solver's job
// (FR-006). The limiter lives here because it is a property of *this
// junction* (it depends on Is, n, Vt), not of any particular solver.

namespace acfx {

// evaluate()'s result: the Shockley current and its analytic small-signal
// conductance at the queried bias. Diode-local by design — a Newton step
// consumes exactly this {current, conductance} pair, and sharing a wider
// struct across component kinds would couple unrelated physics.
struct DiodeSample {
    double current;      // I(vAK) in amperes
    double conductance;  // g = dI/dV in siemens (always > 0)
};

struct Diode {
    NodeId anode;
    NodeId cathode;
    double Is;  // reverse saturation current (A)
    double n;   // ideality factor (dimensionless)
    double Vt;  // thermal voltage (V)

    // The junction scaling voltage n*Vt (SPICE `vte`) — the denominator in
    // every exp/log below. Factored out so the physics reads cleanly.
    constexpr double vte() const noexcept {
        return n * Vt;
    }

    // Shockley current and analytic conductance at anode->cathode bias vAK.
    //
    // Computes the *true* physics at the given bias — it never clamps the
    // output to a fabricated value (repo standard: no fallbacks, no mock
    // data). Keeping the exp argument in a range where it does not overflow
    // is the caller/solver's responsibility, discharged via
    // limitJunctionVoltage() below between Newton iterations. If a caller
    // still passes a bias large enough to overflow, std::exp returns +inf
    // per IEEE 754 and that propagates honestly rather than being masked.
    DiodeSample evaluate(double vAK) const noexcept {
        const double e = std::exp(vAK / vte());
        return DiodeSample{
            Is * (e - 1.0),        // current  = Is*(exp - 1)
            (Is / vte()) * e       // dI/dV    = (Is/nVt)*exp
        };
    }

    // Critical voltage Vcrit — the bias at which the diode's exponential
    // I(V) curve has maximum curvature relative to its own conductance; it
    // is the anchor the pnjlim limiter clamps around. Derived (as in SPICE)
    // by setting d/dV of the linearized-vs-true error to zero, which yields
    //
    //     Vcrit = n*Vt * ln( n*Vt / (sqrt(2) * Is) )
    //
    // Above Vcrit the exp is steep enough that an unlimited Newton step can
    // overshoot by many decades of current; the limiter engages there.
    double vCrit() const noexcept {
        const double vt = vte();
        return vt * std::log(vt / (std::sqrt(2.0) * Is));
    }

    // Junction voltage limiting — the standard SPICE `pnjlim` step clamp
    // (research R2). Given the raw Newton proposal vNew and the previous
    // iterate vOld, return a damped voltage that keeps the exp argument from
    // exploding between iterations, WITHOUT altering the fixed point the
    // Newton loop is converging to (the limiter is inactive once the iterates
    // settle below Vcrit / within a step of n*Vt).
    //
    // Textbook logic (Vladimirescu, "The SPICE Book"; Nagel, SPICE2):
    //   Only intervene when the proposed bias is both above Vcrit AND the
    //   step exceeds 2*n*Vt (a step small enough, or a bias low enough, needs
    //   no help). Then:
    //     - if vOld > 0, project the step through the diode's *logarithmic*
    //       response: vNew = vOld + n*Vt*ln(1 + (vNew - vOld)/(n*Vt)); if the
    //       log argument is non-positive (a huge reverse jump), fall back to
    //       Vcrit — the curve's safe anchor, not a fabricated physics value.
    //     - if vOld <= 0, re-enter forward conduction gently by mapping the
    //       proposed bias onto the log scale: vNew = n*Vt*ln(vNew / (n*Vt)).
    double limitJunctionVoltage(double vNew, double vOld) const noexcept {
        const double vt = vte();
        const double vcrit = vCrit();
        if (vNew > vcrit && std::fabs(vNew - vOld) > 2.0 * vt) {
            if (vOld > 0.0) {
                const double arg = 1.0 + (vNew - vOld) / vt;
                if (arg > 0.0) {
                    return vOld + vt * std::log(arg);
                }
                return vcrit;
            }
            return vt * std::log(vNew / vt);
        }
        return vNew;
    }
};

} // namespace acfx
