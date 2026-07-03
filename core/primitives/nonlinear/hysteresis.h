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

    // Audio path — RT-safe, one high-rate step. Returns a
    // magnetization-derived output.
    //
    // PLACEHOLDER (T005 scope): performs no integration yet — trivial
    // pass-through so the type compiles and is callable end-to-end for
    // downstream consumers (tape-dynamics-core.h, hysteresis-test.cpp).
    // T007/T008 replace this body with the RK2/RK4/Newton-Raphson steppers
    // over dMdH(); T009 adds the finiteness/stability guard (FR-006,
    // contract C3).
    [[nodiscard]] float process(float H) noexcept {
        return H;
    }

private:
    // T006/T007/T008/T009 seam: the shared JA derivative dM/dH, reused by
    // every solver (R3). Declared here to fix the shape of the contract;
    // intentionally NOT defined in this header — T006 supplies the body
    // (Langevin anhysteretic M_an = Ms*L(H_e/a) with the small-x series near
    // 0, effective field H_e = H + alpha*M, irreversible+reversible split).
    // Not yet called from process(), so leaving it undefined does not break
    // compilation or linking of this translation unit.
    [[nodiscard]] double dMdH(double H, double M, double dH) const noexcept;

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
