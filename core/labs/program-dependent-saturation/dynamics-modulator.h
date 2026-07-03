#pragma once

#include <cmath>
#include <cstdint>

// DynamicsModulator — RT-safe stateless envelope-to-signed-offset mapper.
//
// This is the graduation target for
// core/primitives/dynamics/dynamics-modulator.h: a pure, stateless mapper
// from a normalized envelope level [0,1] to a signed, normalized offset,
// shaped by a signed depth and a selectable response curve (linear /
// logarithmic / exponential). It holds NO runtime state — `modulate()` is
// `const`, `noexcept`, allocation-free, lock-free, bounded arithmetic (no
// per-sample transcendental beyond the fixed curve law); identical inputs
// always yield identical outputs, independent of call order. It does not
// itself detect envelope level (see EnvelopeFollower) or drive a target
// parameter — the caller multiplies the normalized result by the target
// parameter's native span (drive dB, bias/tone +-1, mix 0..1).
//
// This file is task T002: the declaration SKELETON only. setDepth()/
// setCurve() store their (to-be-guarded) parameters; modulate() is a stub
// that returns 0.0f as a placeholder. The full curve-law implementation
// (clamping, zero-depth identity, per-curve shaping, numerical-safety
// guards) is deferred to task T008 per
// specs/program-dependent-saturation/contracts/dynamics-modulator-api.md.
//
// Constitution refs:
//   IV   — platform-independent core: no JUCE / Daisy SDK / Teensy / effects /
//          harness includes here, ever.
//   VI   — real-time safety: no heap allocation, no locks, bounded work in
//          modulate().
//   VII  — strict typing & small modules: no `any`-equivalents, file stays
//          well under the 300-500 line guideline.
//
// See also: specs/program-dependent-saturation/spec.md,
//           specs/program-dependent-saturation/data-model.md,
//           specs/program-dependent-saturation/contracts/dynamics-modulator-api.md

namespace acfx {

enum class ModCurve : std::uint8_t { linear, logarithmic, exponential };

class DynamicsModulator {
public:
    // Configuration — store parameters; guarded against degenerate input
    // (clamping to [-1,+1]) is deferred to T008.
    void setDepth(float signedDepth) noexcept {
        depth_ = signedDepth;
    }
    void setCurve(ModCurve curve) noexcept {
        curve_ = curve;
    }

    // Pure map: normalized envelope [0,1] -> signed offset in NORMALIZED
    // units. No runtime state; identical inputs -> identical outputs,
    // call-order independent. Stub: returns 0.0f as a placeholder — the
    // curve-law evaluation (linear / logarithmic / exponential, zero-depth
    // identity, numerical-safety guards) is deferred to T008.
    float modulate(float envNorm) const noexcept {
        (void)envNorm;
        return 0.0f;
    }

private:
    // -----------------------------------------------------------------------
    // Configuration only — DynamicsModulator holds no runtime state; it is a
    // stateless mapper (per the contract).
    // -----------------------------------------------------------------------
    float    depth_ = 0.0f;
    ModCurve curve_ = ModCurve::linear;
};

} // namespace acfx
