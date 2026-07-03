#pragma once

#include <cmath>

// ============================================================================
// hysteresis-kernel.h — the tape-dynamics LAB kernel (T011, FR-015).
//
// This is the "read me first" implementation of Jiles-Atherton (JA) magnetic
// hysteresis: the same physics and the same math as the GRADUATED, production
// primitive at core/primitives/nonlinear/hysteresis.h, but written and
// commented for a reader meeting the model for the first time. Per acfx's
// three-layer architecture (Constitution IX — lab -> primitive -> effect),
// this file is the lab's permanent "graduation source": it stays here as
// living documentation, so a reader can study ONE focused, heavily-annotated
// stepper (RK4) before opening the production header, which additionally
// offers a cheaper RK2 stepper and an implicit Newton-Raphson stepper
// (better stiff-stability at low oversampling factors) with terser comments,
// because by then the physics below is assumed background.
//
// See also:
//   core/labs/tape-dynamics/README.md                    (T010 — full theory)
//   core/primitives/nonlinear/hysteresis.h                (graduated primitive)
//   specs/tape-dynamics/research.md, R1/R3/R5              (design rationale)
//   specs/tape-dynamics/data-model.md, "Entity: Hysteresis" (state/behavior)
//
// -----------------------------------------------------------------------
// THE BIG IDEA: a nonlinearity WITH MEMORY.
// -----------------------------------------------------------------------
// Every other nonlinear stage in this codebase (core/labs/waveshaping/,
// core/labs/saturation/) is MEMORYLESS: the output is a pure function of the
// current input sample, y = f(x), evaluated fresh every call. Feed the same
// x twice — even ten years apart — and you get the same y both times.
//
// Magnetic tape does NOT work that way. A tape head magnetizes iron-oxide
// particles on the tape by nudging microscopic magnetic domains into
// alignment. Those domains do not fully relax back when the field is
// removed — some of the alignment is "pinned" by material imperfections
// (coercivity) and persists. So the tape's magnetization M at this instant
// depends not just on the CURRENT applied field H, but on the PATH H took to
// get here. Feed the same instantaneous H after a rising sweep versus after
// a falling sweep and you get two DIFFERENT M values. That path-dependence
// is exactly what "hysteresis" means, and it is why this primitive is
// STATEFUL (holds M and the previous field H_prev across calls) while every
// waveshaper in this codebase is not.
//
// Plotted as M (vertical) against H (horizontal), that path-dependence
// traces out a closed LOOP with nonzero enclosed area — rising H and falling
// H follow two different curves that meet only at the loop's tips. A
// memoryless waveshaper, by contrast, traces a single-valued curve no matter
// how you sweep the input: its "loop area" is zero. That closed-loop-area
// test (area > 0 here, area ~= 0 for a static shaper) is the primitive's
// defining acceptance measurement (spec.md SC-001) — the numerical
// signature of "this thing remembers." That area is also dissipated
// energy (magnetic hysteresis loss) — one physical source of tape's
// "soft glue" compression, independent of the level-dependent compression
// that falls out of the anhysteretic curve saturating at high drive (see
// dMdH() below and research.md R6).
// ============================================================================

namespace acfx::labs::tape_dynamics {

// ----------------------------------------------------------------------------
// JAParams — the five physical numbers that define one flavor of magnetic
// material / tape formulation. All are plain scalars; the struct itself
// carries no runtime state. HysteresisKernel's setters clamp assignments
// into the constraints below (guarded, never silently substituted or
// defaulted away — acfx never uses mock/fallback values, only defined,
// documented guards).
// ----------------------------------------------------------------------------
struct JAParams {
    // Saturation magnetization: the ceiling |M| approaches as H -> +-infinity.
    // Physically, this is "every domain in the material is aligned — there is
    // no more magnetization to gain." Constraint: Ms > 0.
    double Ms = 1.0;

    // Anhysteretic shape parameter: sets how gently or sharply the IDEALIZED
    // (memory-free) M-vs-H curve bends over as it approaches Ms — see
    // langevin() below. Larger a -> a softer knee; smaller a -> a harder
    // knee. Constraint: a > 0.
    double a = 1.0;

    // Inter-domain (mean-field) coupling: how much a domain's neighbors'
    // magnetization "helps" it align, feeding back into the EFFECTIVE field
    // the material sees (He = H + alpha*M below) rather than the raw applied
    // field alone. Constraint: alpha >= 0, and in practice small — this is a
    // second-order correction, not the dominant term.
    double alpha = 0.0;

    // Coercivity: the field strength needed to overcome the pinning sites
    // that hold domain walls in place. This is the material's "resistance to
    // being demagnetized" and it is what makes the loop WIDE (more memory,
    // more energy dissipated per cycle) or NARROW (less memory, closer to
    // memoryless). Constraint: k > 0.
    double k = 1.0;

    // Reversibility fraction: the proportion of domain-wall motion that is
    // purely elastic (reversible — it snaps back the instant the field
    // relaxes) versus plastic (irreversible — pinned, contributing to the
    // loop). c = 1 means fully reversible (a single-valued anhysteretic
    // curve, loop area -> 0); c = 0 means fully irreversible (the widest
    // possible loop for a given k). Constraint: 0 <= c <= 1.
    double c = 0.5;
};

// ----------------------------------------------------------------------------
// HysteresisKernel — the RT-safe, platform-independent lab kernel.
//
// RT-safety (Constitution VI): every method is noexcept; nothing here
// allocates, locks, or loops an unbounded/data-dependent number of times —
// process() below is a single, fixed 4-stage Runge-Kutta step, always the
// same amount of work regardless of the input. <cmath> is the only
// dependency (Constitution IV: platform-independent — no vendor MCU audio
// framework or plugin-host framework headers anywhere in this file), so this
// header compiles unmodified on the desktop workbench, inside a DAW plugin,
// or on embedded firmware.
// ----------------------------------------------------------------------------
class HysteresisKernel {
public:
    // Sets the sample rate. dt_ itself is not used by the field-domain
    // integrator below (process() steps in H, not in time — see process()'s
    // comment); it is captured here because a real host wrapper composing
    // this kernel under an oversampler (tape-dynamics-core.h) needs it to
    // configure that oversampler's high-rate callback cadence. Guarded: a
    // non-positive or non-finite rate is ignored rather than poisoning dt_
    // with zero/negative/NaN.
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

    // Bulk assignment, routed through the same per-parameter setters below
    // so a JAParams struct handed in wholesale gets exactly the same
    // clamping as setting each field individually (guarded, not mocked).
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
    // A crucial mental shift for anyone new to this model: we are NOT
    // integrating an ODE in TIME. We are integrating dM/dH — magnetization
    // as a function of the FIELD VARIABLE H — and each audio sample simply
    // supplies the next H the "tape head" sees: one call = one step of
    // width dH = H - H_prev along that field axis (here: a single fixed RK4
    // step; the graduated primitive also offers RK2 and an implicit
    // Newton-Raphson step for stiffer settings).
    //
    // The STEP DIRECTION (sign of dH) is held fixed across every RK
    // sub-stage: the irreversible term in dMdH() below only makes physical
    // sense as "domain walls advancing in the direction the field is
    // currently driving them," so flipping that sign mid-step (from a
    // stage's intermediate, predicted M) would be unphysical. delta =
    // sign(dH) is computed once, from the WHOLE step, and reused at every
    // stage.
    [[nodiscard]] float process(float H) noexcept {
        // A non-finite input field is not something the ODE can be stepped
        // over meaningfully; the defined behavior is to hold the previous
        // field (dH = 0, i.e. "the tape head didn't move") rather than let
        // a NaN/Inf poison H_prev_/M_ for every sample after it.
        const double Hnew = std::isfinite(H) ? static_cast<double>(H) : Hprev_;
        const double Hprev = Hprev_;
        const double dH = Hnew - Hprev;

        double Mnext = stepRK4(Hprev, M_, dH);

        // Stiff-solver stability guard (mirrors the graduated primitive's
        // FR-006/contract C3): the JA ODE is STIFF near the coercive field,
        // and a hot transient can drive an explicit stepper like RK4 to
        // overshoot into a non-finite or absurdly large M. Resolve that to a
        // defined, stable value instead of leaking a NaN/Inf/pop:
        //   * non-finite -> fall back to the last M known finite (not to 0
        //     — snapping to "erased tape" mid-signal is itself audible).
        //   * |M| beyond a small multiple of Ms -> clamp to that bound, sign
        //     preserved (a large but PHYSICAL excursion should not clip at
        //     Ms itself; only a diverged one should be caught).
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
    // Numerical thresholds. All dimensionless, living in the argument space
    // of the Langevin function (see langevin() below).
    // ------------------------------------------------------------------

    // Below this |x|, evaluate the Langevin function via its Taylor series
    // instead of coth(x) - 1/x directly: as x -> 0, coth(x) -> 1/x, so the
    // direct formula is a difference of two large, nearly-equal numbers —
    // exactly the situation where floating-point subtraction cancels away
    // all the meaningful digits. The series sidesteps that cancellation.
    static constexpr double kLangevinSmall = 1.0e-3;

    // Above this |x|, coth(x) is indistinguishable from sign(x) in double
    // precision and sinh(x) is already large enough that computing it
    // directly risks overflow for no benefit — use the asymptotic form.
    static constexpr double kLangevinLarge = 20.0;

    // Floor applied to any denominator before dividing, so a ~0 denominator
    // yields a large-but-FINITE quotient instead of Inf/NaN.
    static constexpr double kDenomFloor = 1.0e-12;

    // The stiff-solver guard's clamp bound is this multiple of the runtime
    // Ms (not a fixed constant — Ms is caller-configurable). 4x sits well
    // above any physically-expected excursion (the anhysteretic and
    // irreversible terms are themselves Ms-bounded at steady state) while
    // still catching a diverging transient long before it reaches float
    // overflow.
    static constexpr double kMBoundMultiplier = 4.0;

    // ------------------------------------------------------------------
    // langevin(x) = coth(x) - 1/x — the classical Langevin function from
    // statistical mechanics of paramagnetism (the same function describes
    // how a population of magnetic dipoles subject to thermal agitation
    // settles into partial alignment under an applied field: full alignment
    // is 1, zero alignment is 0, and the function eases smoothly between the
    // two rather than switching abruptly). Jiles-Atherton borrows this
    // exact shape as the material's ANHYSTERETIC curve — see dMdH() — i.e.
    // "the memory-free curve the magnetization would follow if there were
    // no pinning at all."
    //
    // langevin(x) is defined for every real x (it has a removable
    // singularity at x = 0, handled below by the small-x series), is odd
    // (langevin(-x) = -langevin(x)), and asymptotes to +-1 as x -> +-infinity
    // — which is exactly the S-shaped saturating curve we want: nearly
    // linear near the origin, smoothly bending over and flattening out
    // toward the saturation ceiling Ms as the effective field grows.
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

    // d/dx [coth(x) - 1/x] = 1/x^2 - 1/sinh^2(x). This is the SLOPE of the
    // anhysteretic curve — how steeply M_an rises with the effective field —
    // and it appears both directly (the reversible contribution to dM/dH,
    // scaled by c) and inside the "effective-field feedback" denominator in
    // dMdH() (because alpha couples M back into the field the material
    // itself sees, the slope has to be corrected for that feedback loop).
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

    // Clamp a denominator's MAGNITUDE away from zero while preserving its
    // sign, so "divide by roughly zero" resolves to a large-but-finite
    // result instead of Inf/NaN. This is a defined, documented guard
    // (Constitution V) — never a silent fallback.
    [[nodiscard]] static double guardDenom(double d) noexcept {
        if (std::fabs(d) < kDenomFloor) {
            return (d < 0.0) ? -kDenomFloor : kDenomFloor;
        }
        return d;
    }

    // ------------------------------------------------------------------
    // dMdH(H, M, dH) — the Jiles-Atherton derivative. This one function IS
    // the physics: every stepper (RK4 here; RK2/Newton in the graduated
    // primitive) just integrates this same slope field, evaluated at
    // different (H, M) sample points per their own stage recipe.
    //
    // Reading the model top to bottom:
    //
    //   1. EFFECTIVE FIELD.  He = H + alpha*M.
    //      The domains inside the material do not just feel the field the
    //      tape head applies (H) — they also feel a contribution from their
    //      NEIGHBORS' alignment, proportional to the current magnetization
    //      itself (alpha*M). This mean-field coupling is what makes the
    //      model implicitly self-referential: M depends on He, but He
    //      depends on M. That feedback loop reappears explicitly in the
    //      "den" term at the very end of this function.
    //
    //   2. ANHYSTERETIC CURVE.  x = He/a,  M_an = Ms * langevin(x).
    //      This is "the magnetization the material would settle to at this
    //      instant if it had NO memory at all" — a smooth, single-valued,
    //      Ms-bounded S-curve in the effective field. It is the target the
    //      irreversible and reversible terms below both chase.
    //
    //   3. ANHYSTERETIC SLOPE.  dMan_dHe = (Ms/a) * langevinDeriv(x).
    //      How fast that target curve rises with the effective field —
    //      needed both as the fully-reversible contribution (step 5) and
    //      inside the feedback correction (step 6).
    //
    //   4. IRREVERSIBLE SUSCEPTIBILITY, with the JA sign fix.
    //      dMirr/dH = (M_an - M) / (delta*k - alpha*(M_an - M)), where
    //      delta = sign(dH) (the field's current drive direction — computed
    //      ONCE per step; see process()'s comment). This is "how fast the
    //      PINNED domain walls chase the anhysteretic target," gated by the
    //      coercivity k: larger k means the walls resist moving more, so
    //      this susceptibility — and the resulting loop WIDTH — shrinks.
    //      The classic JA pathology this guards against: taken literally,
    //      the raw formula can imply the irreversible term should push M
    //      AWAY from M_an when the field reverses right at the loop's tip.
    //      Domain walls do not spontaneously advance backward, so the term
    //      is zeroed whenever it would point the wrong way (delta and
    //      (M_an - M) disagree in sign) and clamped non-negative as a
    //      belt-and-braces safety net.
    //
    //   5. BLEND irreversible and reversible. The reversible fraction c
    //      contributes the FULLY elastic term dMan_dHe directly (snaps
    //      instantly to the anhysteretic slope, no lag); the remaining
    //      (1 - c) goes through the pinned, lagging irreversible term from
    //      step 4. c = 1 collapses onto the single-valued anhysteretic
    //      curve (loop area -> 0); c = 0 is the "stickiest," widest-loop
    //      case for a given k.
    //
    //   6. EFFECTIVE-FIELD FEEDBACK CORRECTION. Dividing by
    //      (1 - alpha*c*dMan_dHe) accounts for step 1's self-reference:
    //      since He depends on M and M depends on He, the blended slope
    //      from step 5 needs this correction to be the TRUE dM/dH, not a
    //      partial derivative that ignores the loop back through He.
    //      guardDenom() keeps it finite even near alpha*c*dMan_dHe = 1.
    //
    // Reference: Jiles & Atherton (1986); the closed form used here follows
    // Chowdhury, "Real-Time Physical Modelling for Analog Tape Machines,"
    // DAFx 2019 (specs/tape-dynamics/research.md, R1) — the same form the
    // graduated primitive implements.
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
        const double den = guardDenom(1.0 - alpha * c * dMan_dHe); // Step 6
        return num / den;
    }

    // ------------------------------------------------------------------
    // stepRK4 — classical 4th-order Runge-Kutta over one step of width dH,
    // integrating dM/dH starting from (H0, M0). This is standard
    // numerical-ODE machinery (not JA-specific): sample the slope function
    // dMdH() at four cleverly-chosen points spanning the step, then combine
    // them with the classic 1:2:2:1 weighting.
    //
    //   k1 = f(H0,        M0)                     slope at the step's START
    //   k2 = f(H0 + dH/2, M0 + (dH/2)*k1)          slope at the midpoint,
    //                                              predicted via k1 (Euler)
    //   k3 = f(H0 + dH/2, M0 + (dH/2)*k2)          slope at the midpoint
    //                                              again, refined via k2
    //   k4 = f(H0 + dH,   M0 + dH*k3)              slope at the step's END,
    //                                              predicted via k3
    //   M1 = M0 + (dH/6)*(k1 + 2*k2 + 2*k3 + k4)   weighted average slope
    //
    // Using two independent midpoint estimates (k2 refined by k3) is what
    // buys RK4 its 4th-order accuracy: local error per step shrinks as
    // O(dH^5), versus O(dH^3) for the 2-stage RK2/Heun method the graduated
    // primitive also offers — "spend more evaluations of the SAME slope
    // function to buy a higher-order approximation of the true trajectory,"
    // without yet introducing the extra complexity of an IMPLICIT solver
    // (Newton-Raphson, the third option in the graduated primitive), which
    // trades even more per-step work for stability on the stiffest
    // transients at low oversampling (research.md R3/R5).
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
// WHY ADAA DOES NOT APPLY HERE.
//
// The waveshaping lab (core/labs/waveshaping/) and the ADAA (antiderivative
// antialiasing) technique it teaches both rely on one property: the
// nonlinearity is a MEMORYLESS, static function of the CURRENT sample alone,
// y = f(x). Because f is static, its antiderivative F(x) = integral of f is
// ALSO a static function you can precompute once (in closed form or a table)
// and reuse forever — ADAA replaces a naive per-sample f(x) with a
// finite-difference of F evaluated at consecutive samples, which
// mathematically cancels the aliasing energy a naive memoryless nonlinearity
// would otherwise generate, with no extra per-sample cost beyond evaluating
// F instead of f.
//
// HysteresisKernel has no such f. What it computes each sample is not "some
// fixed curve evaluated at the current H" — it is the NEXT STATE of an ODE
// whose right-hand side (dMdH, above) is itself a function of the evolving
// state M, not of H alone. The same H can and does produce different
// outputs on different calls, depending on Hprev_ and M_ — there is no
// single static F(H) whose derivative reproduces this output. An
// antiderivative substitution trick fundamentally requires a function; this
// is a trajectory, not a function.
//
// So the antialiasing route taken here is the other one available to any
// nonlinear process: run the stateful step at a HIGHER sample rate than the
// audio rate (oversampling), pushing the harmonic energy the nonlinearity
// generates up past the folding frequency of a subsequent decimation
// filter. That is exactly why core/effects/tape-dynamics/tape-dynamics-core.h
// composes this kernel's graduated twin as the per-high-rate-step callable
// INSIDE the shipped Oversampler<Factor>
// (core/primitives/oversampling/oversampler.h), rather than reaching for
// ADAA. See specs/tape-dynamics/research.md R4 for the full writeup.
// ============================================================================

}  // namespace acfx::labs::tape_dynamics
