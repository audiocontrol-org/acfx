#pragma once

#include "primitives/circuit/models/capacitor.h"
#include "primitives/circuit/models/companion.h"
#include "primitives/circuit/models/inductor.h"
#include "primitives/circuit/node.h"

// ReactiveIntegrator — the implicit-integration primitive: it discretizes a
// netlist's reactive elements (capacitors, inductors) into per-step Norton
// companions under a chosen implicit integration RULE, composes those fixed
// companions with the linear MnaSystem (or, when a nonlinear element is present,
// with NewtonSolver), and advances the per-element history once per converged
// step (contracts/reactive-integrator.md; research R1-R9; data-model.md).
//
// The integration RULE is a compile-time template policy (research R2): a small
// stateless struct with two pure static functions,
//   capacitorCompanion(C, dt, vPrev, iPrev) -> Companion
//   inductorCompanion (L, dt, vPrev, iPrev) -> Companion
// returning the Norton pair in MNA's consumption convention
//   i(a,b) = Geq*(V(a) - V(b)) - Ieq.
// Fixing the rule per plan keeps the per-sample companion path branch-free and
// mirrors the siblings' template-sizing pattern. Two policies ship here:
// BackwardEuler (first-order, L-stable) and Trapezoidal (second-order,
// A-stable). BackwardEuler is single-sourced against the shipped element
// companion() methods (research R9) so the C/dt and dt/L constants live in
// exactly one place; Trapezoidal computes its own {Geq, Ieq} because there is no
// element method for it (adding one would push history-shaped concerns onto the
// stateless value-types — design record Approach D rejection).
//
// This header lands the two RULE POLICIES. The ReactiveIntegrator class itself
// (StepResult, the two-phase plan()/step() surface, the internal CompanionSupply
// and cross-sample history) is the next increment (T005) and is added to THIS
// same header.
//
// C++17, header-only, standard library only; no platform or component-graphics
// headers. double throughout (FR-022). The policy functions are noexcept,
// allocation-free, and pure (hold no state).

namespace acfx::integration {

// Backward-Euler integration rule (research R1, first-order / L-stable). Reads
// only the history term its rule consumes: the capacitor's voltage history
// (vPrev, ignoring iPrev) and the inductor's current history (iPrev, ignoring
// vPrev). Single-sourced against the shipped element companion() methods (R9):
// the C/dt and dt/L physics live in exactly one place. The unused argument is
// kept so both policies share one uniform 4-arg signature (contract RP2).
struct BackwardEuler {
    // Capacitor backward-Euler companion: Geq = C/dt, Ieq = Geq*vPrev. Delegates
    // to Capacitor::companion(dt, vPrev) so the C/dt constant is single-sourced
    // (RP1/R9); iPrev is unused (backward-Euler's capacitor row does not read
    // the current history) but kept for a uniform 4-arg policy signature (RP2).
    // Node ids do not affect companion(), so a ground-ground carrier of C is used.
    static Companion capacitorCompanion(double C, double dt, double vPrev,
                                        double /*iPrev*/) noexcept {
        return Capacitor{kGround, kGround, C}.companion(dt, vPrev);
    }

    // Inductor backward-Euler companion: Geq = dt/L, Ieq = -iPrev. Delegates to
    // Inductor::companion(dt, iPrev) so the dt/L constant is single-sourced
    // (RP1/R9); vPrev is unused but kept for a uniform 4-arg signature (RP2).
    // Node ids do not affect companion(), so a ground-ground carrier of L is used.
    static Companion inductorCompanion(double L, double dt, double /*vPrev*/,
                                       double iPrev) noexcept {
        return Inductor{kGround, kGround, L}.companion(dt, iPrev);
    }
};

// Trapezoidal integration rule (research R1, second-order / A-stable). Consumes
// BOTH history terms (vPrev and iPrev) for each element. No element method
// exists for the trapezoidal rule (it needs extra history the stateless
// value-types deliberately do not hold), so it computes {Geq, Ieq} directly.
struct Trapezoidal {
    // Capacitor trapezoidal companion: Geq = 2C/dt, Ieq = Geq*vPrev + iPrev.
    static Companion capacitorCompanion(double C, double dt, double vPrev,
                                        double iPrev) noexcept {
        const double Geq = 2.0 * C / dt;
        return Companion{Geq, Geq * vPrev + iPrev};
    }

    // Inductor trapezoidal companion: Geq = dt/(2L), Ieq = -(iPrev + Geq*vPrev).
    static Companion inductorCompanion(double L, double dt, double vPrev,
                                       double iPrev) noexcept {
        const double Geq = dt / (2.0 * L);
        return Companion{Geq, -(iPrev + Geq * vPrev)};
    }
};

} // namespace acfx::integration
