#pragma once

#include <cmath>
#include <cstdint>

// Hysteresis — the graduated Jiles-Atherton primitive; first *stateful*
// inhabitant of nonlinear/ (FR-001/016).
//
// Platform-independent (Constitution IV): standard library only, no
// JUCE/libDaisy/Teensy headers. RT-safe (Constitution VI): all state
// compile-time-sized, no heap/locks in process(); work is O(solver stages)
// bounded.
//
// This header formalizes the T001 scaffolding stub into the exact contract
// surface (types + class shell). It does NOT implement the physics: dMdH()
// is declared but not defined (T006 owns the body — Langevin anhysteretic +
// irreversible/reversible split), and process() is a compiling PLACEHOLDER
// that performs no integration (T007/T008 add the RK2/RK4/Newton-Raphson
// steppers; T009 adds the stiff-solver stability guard).
//
// See specs/tape-dynamics/contracts/hysteresis-api.md (the exact surface)
// and specs/tape-dynamics/data-model.md, "Entity: Hysteresis" / "Entity:
// JAParams" (state fields, constraints, invariants).

namespace acfx {

// Integration method selected for the per-sample JA ODE step (FR-005).
enum class Solver : std::uint8_t { rk2, rk4, newtonRaphson };

// The five Jiles-Atherton physical parameters (R1/R2). Plain scalars; no
// state of its own — Hysteresis::setParams()/per-parameter setters clamp
// assignments into these constraints (guarded, not mocked — Constitution V).
struct JAParams {
    double Ms = 1.0;     // saturation magnetization (ceiling)  > 0
    double a = 1.0;      // anhysteretic shape                  > 0
    double alpha = 0.0;  // inter-domain coupling                >= 0
    double k = 1.0;      // coercivity (loop width / memory)    > 0
    double c = 0.5;      // reversibility (loop openness)       0..1
};

class Hysteresis {
public:
    // Configuration — call outside the audio hot path.

    // Set the sample rate; configures the integrator step size dt = 1/fs
    // used by the (future) solver steps. Guarded against non-positive input
    // so dt_ never becomes zero/negative/non-finite.
    void prepare(double sampleRate) noexcept {
        if (sampleRate > 0.0 && std::isfinite(sampleRate)) {
            sampleRate_ = sampleRate;
            dt_ = 1.0 / sampleRate;
        }
    }

    // Defined initial condition (FR-003, contract C2): after reset(),
    // identical input sequences reproduce identical output — no hidden
    // global state.
    void reset() noexcept {
        M_ = 0.0;
        Hprev_ = 0.0;
    }

    // Assigns all five JAParams fields via the per-parameter setters below,
    // so a bulk assignment clamps to the same constrained domain as the
    // individual setters (guarded, not mocked — Constitution V).
    void setParams(const JAParams& p) noexcept {
        setMs(p.Ms);
        setA(p.a);
        setAlpha(p.alpha);
        setK(p.k);
        setC(p.c);
    }

    // Per-parameter setters (FR-002). Each clamps to the constraint from
    // data-model.md "Entity: JAParams"; a non-finite write is ignored so no
    // NaN/Inf parameter can ever leak into the (future) ODE step.
    void setMs(double v) noexcept {
        if (std::isfinite(v) && v > 0.0) params_.Ms = v;
    }

    void setA(double v) noexcept {
        if (std::isfinite(v) && v > 0.0) params_.a = v;
    }

    void setAlpha(double v) noexcept {
        if (std::isfinite(v) && v >= 0.0) params_.alpha = v;
    }

    void setK(double v) noexcept {
        if (std::isfinite(v) && v > 0.0) params_.k = v;
    }

    void setC(double v) noexcept {
        if (std::isfinite(v) && v >= 0.0 && v <= 1.0) params_.c = v;
    }

    void setSolver(Solver s) noexcept {
        solver_ = s;
    }

    // Audio path — RT-safe, one high-rate step. Integrates the JA state one
    // step over dH = H - Hprev using the selected solver, advances the stored
    // magnetization M_, records Hprev_ = H, and returns the M-derived output.
    //
    // Integration is in the field variable H (the ODE is dM/dH, evaluated at
    // the sub-stage H positions), NOT in time: one input sample = one step of
    // width dH. All internal math is double; only the returned value narrows.
    //
    // The JA derivative depends on delta = sign(dH). For a single Runge-Kutta
    // step the step's overall delta (the direction of the whole dH) is held
    // CONSTANT across every sub-stage evaluation — the domain-wall drive
    // direction is a property of the step, not of the individual stages. This
    // is the standard treatment of JA under an explicit RK stepper: a stage
    // must not flip delta merely because its intermediate M/H excursion would
    // locally imply the opposite sign. We pass dH straight into dMdH() (whose
    // internal delta = sign(dH)) at every stage to enforce this.
    //
    // T007 implements the explicit RK2 (Heun) and RK4 cases. The
    // newtonRaphson case is temporarily routed to RK4 so the type compiles and
    // never crashes; T008 fills this with the implicit stepper. The stiff
    // -transient clamp / deNaN guard is T009 (FR-006, contract C3) and is
    // deliberately NOT applied here.
    [[nodiscard]] float process(float H) noexcept {
        const double Hnew = static_cast<double>(H);
        const double Hprev = Hprev_;
        const double dH = Hnew - Hprev;

        double Mnext = M_;
        switch (solver_) {
            case Solver::rk2:
                Mnext = stepRK2(Hprev, M_, dH);
                break;
            case Solver::rk4:
                Mnext = stepRK4(Hprev, M_, dH);
                break;
            case Solver::newtonRaphson:
                // T008 fills this — temporarily route to RK4 so process() is
                // callable and non-crashing under the newtonRaphson selection.
                Mnext = stepRK4(Hprev, M_, dH);
                break;
        }

        M_ = Mnext;
        Hprev_ = Hnew;
        return static_cast<float>(M_);
    }

private:
    // Test seam: grants the primitive's unit tests access to the shared JA
    // derivative dMdH() (private RT-internal, not part of the public surface).
    // No behavior of its own — purely befriends the test accessor.
    friend struct HysteresisTestAccess;

    // Numerical thresholds for the JA derivative (all dimensionless / in the
    // argument space of the Langevin function). Chosen so the small-x series
    // and the coth-overflow guard both stay well inside double precision.
    static constexpr double kLangevinSmall = 1.0e-3;  // |x| below → series
    static constexpr double kLangevinLarge = 20.0;     // |x| above → asymptote
    static constexpr double kDenomFloor = 1.0e-12;     // divide-by-~0 guard

    // Langevin function L(x) = coth(x) - 1/x, defined everywhere:
    //   * small |x|: series  x/3 - x^3/45  (avoids the coth 1/x cancellation)
    //   * large |x|: coth(x) -> sign(x), 1/sinh^2 -> 0 (avoids sinh overflow)
    [[nodiscard]] static double langevin(double x) noexcept {
        const double ax = std::fabs(x);
        if (ax < kLangevinSmall) {
            return x * (1.0 / 3.0) - (x * x * x) * (1.0 / 45.0);
        }
        if (ax > kLangevinLarge) {
            return (x > 0.0 ? 1.0 : -1.0) - 1.0 / x;  // coth(x) ~= sign(x)
        }
        return 1.0 / std::tanh(x) - 1.0 / x;  // coth(x) - 1/x
    }

    // dL/dx = 1/x^2 - 1/sinh^2(x)  ( = 1 - coth^2(x) + 1/x^2 ), the anhysteretic
    // slope shape:
    //   * small |x|: series  1/3 - x^2/15  (limit L'(0) = 1/3)
    //   * large |x|: 1/sinh^2 -> 0, so L'(x) -> 1/x^2
    [[nodiscard]] static double langevinDeriv(double x) noexcept {
        const double ax = std::fabs(x);
        if (ax < kLangevinSmall) {
            return (1.0 / 3.0) - (x * x) * (1.0 / 15.0);
        }
        if (ax > kLangevinLarge) {
            return 1.0 / (x * x);
        }
        const double sh = std::sinh(x);
        return 1.0 / (x * x) - 1.0 / (sh * sh);
    }

    // Floor a denominator's magnitude to kDenomFloor while preserving its sign,
    // so a ~0 denominator yields a large-but-finite quotient (never NaN/Inf).
    [[nodiscard]] static double guardDenom(double d) noexcept {
        if (std::fabs(d) < kDenomFloor) {
            return (d < 0.0) ? -kDenomFloor : kDenomFloor;
        }
        return d;
    }

    // T006/T007/T008/T009 seam: the shared JA derivative dM/dH, reused by every
    // solver (R3). The arrangement below is the standard closed-form
    // Jiles-Atherton dM/dH (Jiles & Atherton 1986; the form used in
    // Chowdhury, "Real-Time Physical Modelling for Analog Tape Machines",
    // DAFx 2019 / research.md R1):
    //
    //   H_e     = H + alpha*M                     (effective field)
    //   x       = H_e / a
    //   M_an    = Ms * L(x)                        (anhysteretic, Langevin)
    //   dMan/dHe= (Ms/a) * L'(x)                   (anhysteretic slope)
    //   dMirr/dH= (M_an - M) / (delta*k - alpha*(M_an - M)),  delta = sign(dH)
    //   dM/dH   = ( (1-c)*dMirr/dH + c*dMan/dHe )
    //             / ( 1 - alpha*c*dMan/dHe )       (effective-field feedback)
    //
    // Sign fix (the classic JA pathology): the irreversible term only advances
    // the domain walls in the drive direction, so dMirr/dH is zeroed when
    // (M_an - M) opposes delta, and clamped >= 0 as a safety net. delta is
    // taken as +1 for dH == 0 so a zero step never makes the denominator sign
    // ambiguous or divides by zero.
    [[nodiscard]] double dMdH(double H, double M, double dH) const noexcept {
        const double Ms = params_.Ms;
        const double a = params_.a;         // > 0 (guaranteed by setA)
        const double alpha = params_.alpha;
        const double k = params_.k;         // > 0 (guaranteed by setK)
        const double c = params_.c;

        const double He = H + alpha * M;
        const double x = He / a;
        const double Man = Ms * langevin(x);
        const double dMan_dHe = (Ms / a) * langevinDeriv(x);

        const double delta = (dH < 0.0) ? -1.0 : 1.0;  // sign(dH), dH==0 -> +1
        const double manMinusM = Man - M;

        // Irreversible susceptibility with the JA sign correction.
        double dMirr_dH = 0.0;
        if (delta * manMinusM > 0.0) {
            const double denom = guardDenom(delta * k - alpha * manMinusM);
            dMirr_dH = manMinusM / denom;
            if (dMirr_dH < 0.0) dMirr_dH = 0.0;  // safety: never drive M away
        }

        // Combined reversible + irreversible with effective-field feedback.
        const double num = (1.0 - c) * dMirr_dH + c * dMan_dHe;
        const double den = guardDenom(1.0 - alpha * c * dMan_dHe);
        return num / den;
    }

    // Explicit RK2 (Heun / trapezoidal-predictor-corrector) over one step of
    // width dH, integrating dM/dH from (H0, M0). Two dMdH evaluations:
    //   k1 = f(H0,        M0)                 slope at the step's start
    //   k2 = f(H0 + dH,   M0 + dH*k1)         slope at the Euler-predicted end
    //   M1 = M0 + (dH/2)*(k1 + k2)            average the two slopes
    // delta = sign(dH) is held constant across both stages (see process()): the
    // whole step's dH is passed into every dMdH() call, never the stage offset.
    [[nodiscard]] double stepRK2(double H0, double M0, double dH) const noexcept {
        const double k1 = dMdH(H0, M0, dH);
        const double k2 = dMdH(H0 + dH, M0 + dH * k1, dH);
        return M0 + (dH * 0.5) * (k1 + k2);
    }

    // Standard explicit RK4 over one step of width dH, integrating dM/dH from
    // (H0, M0). Four dMdH evaluations at H0, +dH/2, +dH/2, +dH with 1/6 weights:
    //   k1 = f(H0,         M0)
    //   k2 = f(H0 + dH/2,  M0 + (dH/2)*k1)
    //   k3 = f(H0 + dH/2,  M0 + (dH/2)*k2)
    //   k4 = f(H0 + dH,    M0 + dH*k3)
    //   M1 = M0 + (dH/6)*(k1 + 2*k2 + 2*k3 + k4)
    // delta = sign(dH) is held constant across all four stages (see process()):
    // the whole step's dH is passed into every dMdH() call, not the sub-offset.
    [[nodiscard]] double stepRK4(double H0, double M0, double dH) const noexcept {
        const double half = dH * 0.5;
        const double k1 = dMdH(H0, M0, dH);
        const double k2 = dMdH(H0 + half, M0 + half * k1, dH);
        const double k3 = dMdH(H0 + half, M0 + half * k2, dH);
        const double k4 = dMdH(H0 + dH, M0 + dH * k3, dH);
        return M0 + (dH / 6.0) * (k1 + 2.0 * k2 + 2.0 * k3 + k4);
    }

    // Configuration.
    double sampleRate_ = 48000.0;
    double dt_ = 1.0 / 48000.0;  // integrator step size, set by prepare()
    JAParams params_;
    Solver solver_ = Solver::rk4;

    // State (per-sample mutable, RT) — data-model.md "Entity: Hysteresis".
    double M_ = 0.0;      // current magnetization (the output-bearing state)
    double Hprev_ = 0.0;  // previous applied field (dH = H - Hprev, sign(dH))
};

}  // namespace acfx
