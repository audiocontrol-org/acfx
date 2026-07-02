# Contract — `acfx::GainComputer` public API

**Feature**: `specs/compressors` | **Date**: 2026-07-02 | **Phase**: 1

The graduated primitive's contract is its public C++ interface, mirroring the sibling
`EnvelopeFollower`. Header: `core/primitives/dynamics/gain-computer.h`, namespace `acfx`. It is a
**pure, stateless** static curve: `computeGainDb` is `const`, `noexcept`, allocation-free, lock-free,
branch-only arithmetic (no transcendental, no runtime state) — FR-001/023.

## Types

```cpp
enum class GainMode : std::uint8_t { compress, limit, expand, gate };
```

## Class

```cpp
class GainComputer {
public:
    // Configuration — store parameters; guarded against degenerate input (FR-024).
    void  setMode(GainMode) noexcept;
    void  setThreshold(float dB) noexcept;
    void  setRatio(float ratio) noexcept;   // ratio < 1 guarded to 1; limit mode treats as ∞
    void  setKnee(float dB) noexcept;        // 0 = hard corner; > 0 = unified quadratic C¹ knee
    void  setRange(float dB) noexcept;       // expander/gate max attenuation (floor), ≤ 0

    // Pure static curve: input level in dB → gain change in dB (≤ 0 = attenuation).
    // No runtime state; identical inputs → identical outputs, call-order independent.
    float computeGainDb(float levelDb) const noexcept;
};
```

## Behavioral contract

| Aspect | Guarantee | Spec ref |
|---|---|---|
| Statelessness | `computeGainDb` is `const`, holds no runtime state; call-order independent. | FR-001, SC (US2) |
| Compress curve | `level > thr`: `out = thr + (level−thr)/ratio`; `gainDb = out − level ≤ 0`. Unity below. | FR-003, SC-001 |
| Limit curve | `level > thr`: output held at `thr` (ratio → ∞) within the knee. Unity below. | FR-004, SC-001 |
| Expand curve | `level < thr`: downward by ratio, bounded by `range`. Unity above. | FR-005, SC-005 |
| Gate curve | `level < thr − knee`: attenuate toward `range` floor. Unity above. | FR-006, SC-005 |
| Unified knee | One quadratic C¹ interpolation straddling threshold in every mode; hard corner at `knee = 0`. | FR-007, SC-003 |
| Range bound | expand/gate attenuation never exceeds `rangeDb`. | FR-005/006, SC-005 |
| Numerical safety | No NaN/Inf for any finite level/params; `ratio` guarded ≥ 1. | FR-024, SC-013 |
| RT-safety | Branch-only arithmetic; zero allocation; suitable for MCU. | FR-023, SC-012 |

## Dependency contract
- **Allowed**: `<cmath>`, `<cstdint>`, `core/dsp/`. **Forbidden**: `core/effects/**`, any harness, any
  platform header (JUCE/libDaisy/Teensy). Enforced by `scripts/check-portability.sh` (FR-027).

## Graduation
Authored in `core/labs/compressor/`, graduated via `git mv` into
`core/primitives/dynamics/gain-computer.h` in one atomic commit that moves "gain computers" from
prospectus to inhabited in `core/primitives/README.md` (FR-025/026).
