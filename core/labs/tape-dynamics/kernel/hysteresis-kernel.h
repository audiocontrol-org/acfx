#pragma once

#include <cmath>

// ============================================================================
// hysteresis-kernel.h — the tape-dynamics LAB kernel (T011, FR-015).
//
// Pedagogical twin of the graduated primitive at
// core/primitives/nonlinear/hysteresis.h: same Jiles-Atherton (JA) physics
// and math, one focused RK4 stepper instead of the primitive's RK4/RK2/
// Newton-Raphson choices, kept as living documentation per acfx's
// lab -> primitive -> effect layering (Constitution IX).
//
// Full theory (Langevin function, solver derivation, why ADAA doesn't apply
// to a stateful process): see core/labs/tape-dynamics/README.md (T010).
// Design rationale: specs/tape-dynamics/research.md R1/R3/R5.
// State/behavior contract: specs/tape-dynamics/data-model.md, "Entity:
// Hysteresis".
//
// THE BIG IDEA: unlike every memoryless nonlinearity elsewhere in this
// codebase (core/labs/waveshaping/, core/labs/saturation/), tape has MEMORY —
// magnetization M depends on the PATH the field H took, not just its current
// value, because pinned domain walls do not fully relax when H is removed
// (coercivity). That path-dependence is why this kernel is STATEFUL (holds M
// and H_prev across calls), and why plotting M against H traces a closed
// loop with nonzero area — the primitive's defining acceptance measurement
// (spec.md SC-001), and also the dissipated energy behind tape's "soft glue"
// compression (see dMdH() below and research.md R6).
// ============================================================================

namespace acfx::labs::tape_dynamics {

// ----------------------------------------------------------------------------
// JAParams — the five physical numbers defining one magnetic material /
// tape formulation. Plain scalars, no runtime state. HysteresisKernel's
// setters clamp assignments into the constraints below (guarded, never
// silently substituted — acfx never uses mock/fallback values).
// ----------------------------------------------------------------------------
struct JAParams {
    // Saturation magnetization: the ceiling |M| approaches as H -> +-infinity
    // ("every domain aligned"). Constraint: Ms > 0.
    double Ms = 1.0;

    // Anhysteretic shape parameter: how sharply the idealized memory-free
    // M-vs-H curve bends over near Ms (see langevin() below). Larger a ->
    // softer knee. Constraint: a > 0.
    double a = 1.0;

    // Inter-domain (mean-field) coupling: how much a domain's neighbors'
    // magnetization feeds back into the EFFECTIVE field the material sees
    // (He = H + alpha*M below). A small second-order correction.
    // Constraint: alpha >= 0.
    double alpha = 0.0;

    // Coercivity: field strength needed to overcome domain-wall pinning.
    // Larger k -> wider loop (more memory, more dissipated energy per
    // cycle); smaller k -> narrower, closer to memoryless. Constraint: k > 0.
    double k = 1.0;

    // Reversibility fraction: proportion of domain-wall motion that is
    // elastic/reversible (c) vs. pinned/irreversible (1-c). c = 1 collapses
    // to the single-valued anhysteretic curve (loop area -> 0); c = 0 is the
    // widest loop for a given k. Constraint: 0 <= c <= 1.
    double c = 0.5;
};

// ----------------------------------------------------------------------------
// HysteresisKernel — the RT-safe, platform-independent lab kernel.
//
// RT-safety (Constitution VI): every method is noexcept; nothing allocates,
// locks, or does data-dependent-length work — process() is a single, fixed
// 4-stage RK step every call. <cmath> is the only dependency (Constitution
// IV: no vendor MCU/plugin-host headers), so this compiles unmodified on
// desktop, in a DAW plugin, or on embedded firmware.
// ----------------------------------------------------------------------------
class HysteresisKernel {
public:
    // Sets the sample rate. dt_ is not used by the field-domain integrator
    // below (process() steps in H, not time); it's captured for a host
    // wrapper (tape-dynamics-core.h) composing this kernel under an
    // oversampler. Guarded: a non-positive/non-finite rate is ignored rather
    // than poisoning dt_ with zero/negative/NaN.
    void prepare(double sampleRate) noexcept {
        if (sampleRate > 0.0 && std::isfinite(sampleRate)) {
            sampleRate_ = sampleRate;
            dt_ = 1.0 / sampleRate;
        }
    }

    // Defined initial condition: M = 0, H_prev = 0 ("degaussed tape, silent
    // field"). reset() then an identical input sequence always reproduces
    // an identical output sequence — no hidden global state.
    void reset() noexcept {
        M_ = 0.0;
        Hprev_ = 0.0;
        lastFiniteM_ = 0.0;
    }

    // Bulk assignment, routed through the per-parameter setters below so a
    // JAParams handed in wholesale gets the same clamping as setting each
    // field individually (guarded, not mocked).
    void setParams(const JAParams& p) noexcept {
        setMs(p.Ms);
        setA(p.a);
        setAlpha(p.alpha);
        setK(p.k);
        setC(p.c);
    }

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

    // ------------------------------------------------------------------
    // process() — advance the JA state by one field step and return the
    // new magnetization.
    //
    // We integrate dM/dH — magnetization as a function of the FIELD
    // VARIABLE H, not time — so each audio sample supplies the next H the
    // "tape head" sees: one call = one fixed RK4 step of width dH = H -
    // H_prev (the graduated primitive also offers RK2 and an implicit
    // Newton-Raphson stepper for stiffer settings).
    //
    // The STEP DIRECTION (sign of dH) is held fixed across every RK
    // sub-stage: the irreversible term in dMdH() below only makes physical
    // sense as "domain walls advancing in the direction the field is
    // currently driving them," so delta = sign(dH) is computed once, from
    // the WHOLE step, and reused at every stage.
    [[nodiscard]] float process(float H) noexcept {
        // A non-finite input field can't be stepped over meaningfully; hold
        // the previous field (dH = 0) rather than let a NaN/Inf poison
        // H_prev_/M_ for every sample after it.
        const double Hnew = std::isfinite(H) ? static_cast<double>(H) : Hprev_;
        const double Hprev = Hprev_;
        const double dH = Hnew - Hprev;

        double Mnext = stepRK4(Hprev, M_, dH);

        // Stiff-solver stability guard (mirrors the graduated primitive's
        // FR-006/contract C3): the JA ODE is stiff near the coercive field,
        // and a hot transient can drive an explicit RK4 step to overshoot
        // into a non-finite or absurd M. Resolve to a defined, stable value:
        //   * non-finite -> fall back to the last known-finite M (not 0 —
        //     snapping to "erased tape" mid-signal is itself audible).
        //   * |M| beyond a small multiple of Ms -> clamp to that bound, sign
        //     preserved (a large but PHYSICAL excursion shouldn't clip at Ms
        //     itself; only a diverged one should be caught).
        const double bound = kMBoundMultiplier * params_.Ms;
        if (!std::isfinite(Mnext)) {
            Mnext = lastFiniteM_;
        } else if (Mnext > bound) {
            Mnext = bound;
        } else if (Mnext < -bound) {
            Mnext = -bound;
        }

        M_ = Mnext;
        lastFiniteM_ = Mnext;
        Hprev_ = Hnew;
        return static_cast<float>(M_);
    }

private:
    // ------------------------------------------------------------------
    // Numerical thresholds, dimensionless, in the argument space of the
    // Langevin function (see langevin() below).
    // ------------------------------------------------------------------

    // Below this |x|, evaluate the Langevin function via its Taylor series
    // instead of coth(x) - 1/x directly: as x -> 0, coth(x) -> 1/x, so the
    // direct formula cancels two large, nearly-equal numbers in floating
    // point. The series sidesteps that cancellation.
    static constexpr double kLangevinSmall = 1.0e-3;

    // Above this |x|, coth(x) is indistinguishable from sign(x) in double
    // precision and sinh(x) already risks overflow — use the asymptotic
    // form instead.
    static constexpr double kLangevinLarge = 20.0;

    // Floor applied to any denominator before dividing, so a ~0 denominator
    // yields a large-but-FINITE quotient instead of Inf/NaN.
    static constexpr double kDenomFloor = 1.0e-12;
    // Well-posedness floor for the feedback denominator (see the primitive
    // core/primitives/nonlinear/hysteresis.h): keep 1 - alpha*c*dMan/dHe > 0
    // so an ill-posed alpha cannot invert dM/dH.
    static constexpr double kFeedbackDenomFloor = 1.0e-6;

    // The stiff-solver guard's clamp bound is this multiple of the runtime
    // Ms (caller-configurable, not a fixed constant). 4x sits well above any
    // physically-expected excursion while still catching a diverging
    // transient long before float overflow.
    static constexpr double kMBoundMultiplier = 4.0;

    // ------------------------------------------------------------------
    // langevin(x) = coth(x) - 1/x — the classical Langevin function from
    // statistical mechanics of paramagnetism. Jiles-Atherton borrows this
    // shape as the material's ANHYSTERETIC curve (see dMdH() below): "the
    // memory-free curve M would follow if there were no pinning at all."
    //
    // Defined for every real x (removable singularity at 0, handled by the
    // small-x series), odd, asymptotes to +-1 as x -> +-infinity — an
    // S-shaped saturating curve: linear near the origin, flattening toward
    // the saturation ceiling Ms as the effective field grows.
    [[nodiscard]] static double langevin(double x) noexcept {
        const double ax = std::fabs(x);
        if (ax < kLangevinSmall) {
            // Taylor series of coth(x) - 1/x about x = 0: x/3 - x^3/45 + ...
            return x * (1.0 / 3.0) - (x * x * x) * (1.0 / 45.0);
        }
        if (ax > kLangevinLarge) {
            // coth(x) -> sign(x) as |x| grows; the -1/x term still matters
            // at this scale but sinh(x) itself would already be enormous, so
            // we skip evaluating it.
            return (x > 0.0 ? 1.0 : -1.0) - 1.0 / x;
        }
        return 1.0 / std::tanh(x) - 1.0 / x;  // coth(x) - 1/x, direct form.
    }

    // d/dx [coth(x) - 1/x] = 1/x^2 - 1/sinh^2(x): the SLOPE of the
    // anhysteretic curve. Used both directly (reversible contribution to
    // dM/dH, scaled by c) and inside the effective-field feedback
    // denominator in dMdH() (alpha couples M back into He, so the slope
    // needs correcting for that feedback loop).
    [[nodiscard]] static double langevinDeriv(double x) noexcept {
        const double ax = std::fabs(x);
        if (ax < kLangevinSmall) {
            // Series: 1/3 - x^2/15 + ... (matches d/dx of the series above).
            return (1.0 / 3.0) - (x * x) * (1.0 / 15.0);
        }
        if (ax > kLangevinLarge) {
            return 1.0 / (x * x);  // 1/sinh^2(x) -> 0 out here.
        }
        const double sh = std::sinh(x);
        return 1.0 / (x * x) - 1.0 / (sh * sh);
    }

    // Clamp a denominator's magnitude away from zero, sign preserved, so
    // "divide by roughly zero" resolves to a large-but-finite result instead
    // of Inf/NaN. A defined, documented guard (Constitution V) — never a
    // silent fallback.
    [[nodiscard]] static double guardDenom(double d) noexcept {
        if (std::fabs(d) < kDenomFloor) {
            return (d < 0.0) ? -kDenomFloor : kDenomFloor;
        }
        return d;
    }

    // ------------------------------------------------------------------
    // dMdH(H, M, dH) — the Jiles-Atherton derivative. This one function IS
    // the physics: every stepper (RK4 here; RK2/Newton in the graduated
    // primitive) just integrates this same slope field at different (H, M)
    // sample points per their own stage recipe.
    //
    // Full step-by-step derivation: core/labs/tape-dynamics/README.md.
    // Short version:
    //   1. Effective field He = H + alpha*M (mean-field coupling to
    //      neighboring domains — self-referential: M depends on He, He on
    //      M; corrected for in step 6).
    //   2. Anhysteretic curve Man = Ms*langevin(He/a) — the memory-free
    //      target both terms below chase.
    //   3. Anhysteretic slope dMan_dHe = (Ms/a)*langevinDeriv(He/a).
    //   4. Irreversible susceptibility dMirr/dH = (Man-M) / (delta*k -
    //      alpha*(Man-M)), delta = sign(dH): how fast pinned domain walls
    //      chase Man, gated by coercivity k. Zeroed whenever it would chase
    //      backward (delta and (Man-M) disagree in sign) — domain walls
    //      don't spontaneously reverse — and clamped non-negative as a
    //      belt-and-braces safety net.
    //   5. Blend: c*dMan_dHe (fully elastic, instant) + (1-c)*dMirr_dH
    //      (pinned, lagging). c=1 -> loop area -> 0; c=0 -> widest loop.
    //   6. Divide by guardDenom(1 - alpha*c*dMan_dHe): corrects step 5's
    //      blended slope for step 1's self-reference, so it's the true
    //      dM/dH rather than a partial derivative ignoring the He feedback.
    //
    // Reference: Jiles & Atherton (1986); closed form follows Chowdhury,
    // "Real-Time Physical Modelling for Analog Tape Machines," DAFx 2019
    // (specs/tape-dynamics/research.md R1) — same form the graduated
    // primitive implements.
    [[nodiscard]] double dMdH(double H, double M, double dH) const noexcept {
        const double Ms = params_.Ms;
        const double a = params_.a;         // > 0, guaranteed by setA
        const double alpha = params_.alpha;
        const double k = params_.k;         // > 0, guaranteed by setK
        const double c = params_.c;

        const double He = H + alpha * M;               // Step 1
        const double x = He / a;
        const double Man = Ms * langevin(x);            // Step 2
        const double dMan_dHe = (Ms / a) * langevinDeriv(x);  // Step 3

        const double delta = (dH < 0.0) ? -1.0 : 1.0;   // sign(dH); dH==0 -> +1
        const double manMinusM = Man - M;

        double dMirr_dH = 0.0;                          // Step 4
        if (delta * manMinusM > 0.0) {
            const double denom = guardDenom(delta * k - alpha * manMinusM);
            dMirr_dH = manMinusM / denom;
            if (dMirr_dH < 0.0) dMirr_dH = 0.0;  // safety net: never chase backward
        }

        const double num = (1.0 - c) * dMirr_dH + c * dMan_dHe;   // Step 5
        // Step 6: feedback denom floored POSITIVE (well-posedness — must not
        // go negative and invert dM/dH; see the primitive's kFeedbackDenomFloor).
        const double den = std::fmax(1.0 - alpha * c * dMan_dHe, kFeedbackDenomFloor);
        return num / den;
    }

    // ------------------------------------------------------------------
    // stepRK4 — classical 4th-order Runge-Kutta over one step of width dH,
    // integrating dM/dH from (H0, M0). Standard numerical-ODE machinery
    // (not JA-specific): sample dMdH() at four points spanning the step,
    // combine with the classic 1:2:2:1 weighting.
    //
    //   k1 = f(H0,        M0)                     slope at step START
    //   k2 = f(H0 + dH/2, M0 + (dH/2)*k1)          midpoint, via k1 (Euler)
    //   k3 = f(H0 + dH/2, M0 + (dH/2)*k2)          midpoint again, via k2
    //   k4 = f(H0 + dH,   M0 + dH*k3)              slope at step END, via k3
    //   M1 = M0 + (dH/6)*(k1 + 2*k2 + 2*k3 + k4)   weighted average slope
    //
    // Two independent midpoint estimates buy RK4 its 4th-order accuracy
    // (local error O(dH^5) vs. O(dH^3) for the graduated primitive's 2-stage
    // RK2/Heun option), without an implicit solver's extra per-step cost
    // (Newton-Raphson, the graduated primitive's third option, trades that
    // cost for stability on the stiffest transients at low oversampling —
    // research.md R3/R5).
    //
    // As in process(), delta = sign(dH) must stay CONSTANT across all four
    // stages, which is why the WHOLE step's dH — never a stage's H offset —
    // is passed into every dMdH() call below.
    [[nodiscard]] double stepRK4(double H0, double M0, double dH) const noexcept {
        const double half = dH * 0.5;
        const double k1 = dMdH(H0, M0, dH);
        const double k2 = dMdH(H0 + half, M0 + half * k1, dH);
        const double k3 = dMdH(H0 + half, M0 + half * k2, dH);
        const double k4 = dMdH(H0 + dH, M0 + dH * k3, dH);
        return M0 + (dH / 6.0) * (k1 + 2.0 * k2 + 2.0 * k3 + k4);
    }

    // ------------------------------------------------------------------
    // Configuration.
    double sampleRate_ = 48000.0;
    double dt_ = 1.0 / 48000.0;  // captured for a composing oversampler; unused
                                  // by the field-domain integrator above.
    JAParams params_;

    // State (per-sample mutable, RT-relevant) — the two numbers that make
    // this kernel remember. Everything else in this class is stateless
    // configuration or pure math.
    double M_ = 0.0;              // current magnetization: the output.
    double Hprev_ = 0.0;          // previous applied field: dH = H - Hprev,
                                   // and its sign is delta in dMdH().
    double lastFiniteM_ = 0.0;    // the stiff-solver guard's recovery target.
};

// ============================================================================
// WHY ADAA DOES NOT APPLY HERE: ADAA (see core/labs/waveshaping/) needs a
// MEMORYLESS static function y = f(x) so its antiderivative F(x) can be
// precomputed once. HysteresisKernel has no such f — each sample advances an
// ODE whose right-hand side (dMdH, above) depends on the evolving state M,
// not H alone, so the same H can produce different outputs on different
// calls. There is no static F(H) to substitute; this is a trajectory, not a
// function. Antialiasing here instead comes from oversampling: this kernel's
// graduated twin runs as the per-high-rate-step callable inside the shipped
// Oversampler<Factor> (core/effects/tape-dynamics/tape-dynamics-core.h,
// core/primitives/oversampling/oversampler.h). Full writeup:
// core/labs/tape-dynamics/README.md and specs/tape-dynamics/research.md R4.
// ============================================================================

}  // namespace acfx::labs::tape_dynamics
