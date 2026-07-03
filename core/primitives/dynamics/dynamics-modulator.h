#pragma once

#include <cmath>
#include <cstdint>

// DynamicsModulator — RT-safe stateless envelope-to-signed-offset mapper.
//
// A pure, stateless mapper from a normalized envelope level [0,1] to a signed,
// normalized offset, shaped by a signed depth and a selectable response curve
// (linear / logarithmic / exponential). It holds NO runtime state —
// `modulate()` is `const`, `noexcept`, allocation-free, lock-free, bounded
// arithmetic (no per-sample transcendental beyond the fixed curve law);
// identical inputs always yield identical outputs, independent of call order.
// It does not itself detect envelope level (see EnvelopeFollower) or drive a
// target parameter — the caller multiplies the normalized result by the target
// parameter's native span (drive dB, bias/tone +-1, mix 0..1).
//
// The map is `modulate(env) = depth * curve(clamp(env, 0, 1))`, evaluated fresh
// on every call from the two stored fields (depth clamped to [-1,+1]; the
// selected curve law). The three curve laws all map [0,1]->[0,1], pass through
// (0,0) and (1,1) EXACTLY, are monotone increasing, bounded, and finite at the
// endpoints (per specs/.../research.md Decision 2 and the API contract):
//   linear      : curve(n) = n
//   logarithmic : curve(n) = log1p(kLogShape * n) / log1p(kLogShape)  (concave,
//                 early onset — reaches full offset quickly at low level then
//                 flattens). log1p(0) = 0 anchors (0,0); the denominator anchors
//                 (1,1); k*n >= 0 so log1p is always finite (never log(0)).
//   exponential : curve(n) = (exp(kExpShape * n) - 1) / (exp(kExpShape) - 1)
//                 (convex, late onset — stays low until high level then rises
//                 fast). exp(0)-1 = 0 anchors (0,0); the denominator anchors
//                 (1,1); both terms are bounded for k, n in [0,1].
//
// Numerical safety (FR-021 / SC-014): the shape constants are nonzero
// compile-time constants, so both denominators are strictly positive finite
// constants (never zero) — no division-by-zero path. `envNorm` is clamped to
// [0,1] before shaping, so log1p receives a nonnegative argument (never
// log(0)) and exp receives a bounded argument. The result is finite for every
// `envNorm` in [0,1] and every `depth` in [-1,+1]. When `depth_ == 0` the map
// is exactly 0 for ALL env (the orthogonality identity, FR-007 / SC-002): the
// curve laws are finite for every clamped env, so `0 * curve(env)` is a true
// numeric 0 with no NaN path to violate it.
//
// Constitution refs:
//   IV   — platform-independent core: no JUCE / Daisy SDK / Teensy / effects /
//          harness includes here, ever.
//   VI   — real-time safety: no heap allocation, no locks, bounded work in
//          modulate() (branch + the fixed curve transcendental only).
//   VII  — strict typing & small modules: no `any`-equivalents, file stays
//          well under the 300-500 line guideline.
//
// See also: specs/program-dependent-saturation/spec.md,
//           specs/program-dependent-saturation/data-model.md,
//           specs/program-dependent-saturation/research.md (Decision 2),
//           specs/program-dependent-saturation/contracts/dynamics-modulator-api.md

namespace acfx {

enum class ModCurve : std::uint8_t { linear, logarithmic, exponential };

class DynamicsModulator {
public:
    // Configuration — store parameters; guarded against degenerate input
    // (FR-021). depth is clamped to [-1,+1]; its sign selects direction
    // (positive grows with envelope, negative falls) and its magnitude scales.
    void setDepth(float signedDepth) noexcept {
        depth_ = signedDepth < -1.0f ? -1.0f
               : signedDepth >  1.0f ?  1.0f
               : signedDepth;
    }
    void setCurve(ModCurve curve) noexcept { curve_ = curve; }

    // Pure map: normalized envelope [0,1] -> signed offset in NORMALIZED units.
    // The caller multiplies the result by the target parameter's native span.
    // No runtime state; identical inputs -> identical outputs, call-order
    // independent. depth * curve(clamp(env, 0, 1)); zero-depth => exactly 0.
    float modulate(float envNorm) const noexcept {
        return depth_ * curve(envNorm);
    }

private:
    // Response-curve shape constants — TUNING-PASS PLACEHOLDERS (research.md
    // Decision 2, "Open (tuning-pass)"). Both are strictly positive nonzero
    // compile-time constants, so the curve denominators below are strictly
    // positive finite constants (never zero): no division-by-zero path exists.
    //
    // kLogShape controls the concavity (early onset) of the logarithmic law;
    // ~9 makes the concavity clearly visible yet musical. kExpShape controls
    // the convexity (late onset) of the exponential law; ~3.5 gives an audible
    // convex bend without an extreme near-flat toe. Both are subject to the
    // deferred tuning pass.
    static constexpr float kLogShape = 9.0f;
    static constexpr float kExpShape = 3.5f;

    // The selected response law applied to the clamped envelope. Every branch
    // maps [0,1]->[0,1], passes through (0,0)/(1,1) exactly, is monotone
    // increasing, and is finite at every point in [0,1].
    float curve(float envNorm) const noexcept {
        // Clamp first so log1p receives a nonnegative argument (never log(0))
        // and exp a bounded one — every downstream value is finite.
        const float n = envNorm < 0.0f ? 0.0f
                      : envNorm > 1.0f ? 1.0f
                      : envNorm;
        switch (curve_) {
        case ModCurve::linear:
            return n;
        case ModCurve::logarithmic:
            // Concave (early onset). log1p(0)=0 -> curve(0)=0;
            // log1p(k)/log1p(k)=1 -> curve(1)=1. Denominator is a positive
            // finite constant; k*n >= 0 keeps log1p finite for all n in [0,1].
            return std::log1p(kLogShape * n) / std::log1p(kLogShape);
        case ModCurve::exponential:
            // Convex (late onset). (exp(0)-1)=0 -> curve(0)=0;
            // (exp(k)-1)/(exp(k)-1)=1 -> curve(1)=1. Denominator is a positive
            // finite constant; both terms bounded for k, n in [0,1].
            return (std::exp(kExpShape * n) - 1.0f) / (std::exp(kExpShape) - 1.0f);
        }
        return n; // unreachable: all ModCurve values handled above
    }

    // -----------------------------------------------------------------------
    // Configuration only — DynamicsModulator holds no runtime state; it is a
    // stateless mapper (per the contract, FR-001).
    // -----------------------------------------------------------------------
    float    depth_ = 0.0f;         // clamped to [-1,+1]; sign = direction
    ModCurve curve_ = ModCurve::linear;
};

} // namespace acfx
