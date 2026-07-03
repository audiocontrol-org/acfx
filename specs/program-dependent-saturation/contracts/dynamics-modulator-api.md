# Contract — `acfx::DynamicsModulator` public API

**Feature**: `specs/program-dependent-saturation` | **Date**: 2026-07-03 | **Phase**: 1

The graduated primitive's contract is its public C++ interface, mirroring the sibling `GainComputer` /
`EnvelopeFollower`. Header: `core/primitives/dynamics/dynamics-modulator.h`, namespace `acfx`. It is a
**pure, stateless** envelope→signed-offset mapper: `modulate` is `const`, `noexcept`, allocation-free,
lock-free, bounded arithmetic (no runtime state, no per-sample transcendental beyond the fixed curve
law) — FR-001/019.

## Types

```cpp
enum class ModCurve : std::uint8_t { linear, logarithmic, exponential };
```

## Class

```cpp
class DynamicsModulator {
public:
    // Configuration — store parameters; guarded against degenerate input (FR-021).
    void  setDepth(float signedDepth) noexcept;  // clamped to [-1, +1]; sign = direction
    void  setCurve(ModCurve) noexcept;           // response shaping law

    // Pure map: normalized envelope [0,1] → signed offset in NORMALIZED units.
    // The caller multiplies the result by the target parameter's native span.
    // No runtime state; identical inputs → identical outputs, call-order independent.
    float modulate(float envNorm) const noexcept;
};
```

## Behavioral contract

| Aspect | Guarantee | Spec ref |
|---|---|---|
| Statelessness | `modulate` is `const`, holds no runtime state; call-order independent. | FR-001, SC-007 |
| Signed depth | `depth ∈ [-1,+1]`; sign selects direction (positive grows with envelope, negative falls), magnitude scales. `depth` clamped to range. | FR-002, SC-004 |
| Orthogonality identity | `depth == 0` ⇒ `modulate(env) == 0` for all `env` (the zero-depth guarantee). | FR-007, SC-002 |
| Curve law | `linear`: `n`. `logarithmic`: concave (early onset). `exponential`: convex (late onset). All map `[0,1]→[0,1]`, pass through (0,0)/(1,1), monotone. | FR-003, SC-004 |
| Endpoints finite | Finite and bounded at `envNorm = 0` and `envNorm = 1` (no `log(0)`/overflow). | FR-003, SC-014 |
| Normalized output | Result is in normalized units; caller scales by the target's native span (drive dB, bias/tone ±1, mix 0..1). | FR-002 (Clarified 2026-07-03) |
| Numerical safety | No NaN/Inf for any `envNorm ∈ [0,1]`, `depth ∈ [-1,1]`. | FR-021, SC-014 |
| RT-safety | Bounded arithmetic; zero allocation; suitable for MCU. | FR-019, SC-013 |

## Dependency contract
- **Allowed**: `<cmath>`, `<cstdint>`, `core/dsp/`. **Forbidden**: `core/effects/**`, any harness, any
  platform header (JUCE/libDaisy/Teensy). Enforced by `scripts/check-portability.sh` (FR-024).

## Graduation
Authored in `core/labs/program-dependent-saturation/`, graduated via `git mv` into
`core/primitives/dynamics/dynamics-modulator.h` in one atomic commit that moves the modulation mapper
from prospectus to inhabited in `core/primitives/README.md` (FR-022/023).
