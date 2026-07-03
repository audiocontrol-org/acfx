#pragma once

#include <array>
#include <cmath>
#include <cstdint>

// Hysteresis — stub header for tape-dynamics-core.h compilation (T003).
// Real implementation forthcoming; this skeleton provides the interface.
//
// The graduated Jiles-Atherton primitive; first stateful inhabitant of
// nonlinear/. Platform-independent (Constitution IV): standard library only.
// RT-safe (Constitution VI): all state compile-time-sized, no heap/locks.
//
// See specs/tape-dynamics/contracts/hysteresis-api.md.

namespace acfx {

enum class Solver : std::uint8_t { rk2, rk4, newtonRaphson };

struct JAParams {
    double Ms = 1.0;      // saturation magnetization > 0
    double a = 1.0;       // anhysteretic shape > 0
    double alpha = 0.0;   // inter-domain coupling >= 0
    double k = 1.0;       // coercivity (loop width) > 0
    double c = 0.5;       // reversibility 0..1
};

class Hysteresis {
public:
    // Configuration — call outside the audio hot path.
    void prepare(double sampleRate) noexcept {
        sampleRate_ = sampleRate;
    }

    void reset() noexcept {
        M_ = 0.0;
        Hprev_ = 0.0;
    }

    void setParams(const JAParams& p) noexcept {
        params_ = p;
    }

    void setMs(double v) noexcept {
        params_.Ms = v > 0.0 ? v : 1.0;
    }

    void setA(double v) noexcept {
        params_.a = v > 0.0 ? v : 1.0;
    }

    void setAlpha(double v) noexcept {
        params_.alpha = v >= 0.0 ? v : 0.0;
    }

    void setK(double v) noexcept {
        params_.k = v > 0.0 ? v : 1.0;
    }

    void setC(double v) noexcept {
        params_.c = (v >= 0.0 && v <= 1.0) ? v : 0.5;
    }

    void setSolver(Solver s) noexcept {
        solver_ = s;
    }

    // Audio path — RT-safe, one high-rate step. Returns magnetization-derived output.
    [[nodiscard]] float process(float H) noexcept {
        // Stub: return input (unity passthrough for now).
        // Later tasks implement the actual JA ODE integrator with selected solver.
        return H;
    }

private:
    double sampleRate_ = 48000.0;
    JAParams params_;
    Solver solver_ = Solver::rk4;
    double M_ = 0.0;      // current magnetization
    double Hprev_ = 0.0;  // previous applied field
};

} // namespace acfx
