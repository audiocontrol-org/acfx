# Contract — `acfx::EnvelopeFollower` public API

**Feature**: `specs/envelope-followers` | **Date**: 2026-07-02 | **Phase**: 1

The primitive's contract is its public C++ interface (the deliverable IS an API), mirroring the
sibling primitives (`SvfPrimitive`, `Waveshaper`). Header: `core/primitives/dynamics/envelope-follower.h`,
namespace `acfx`. All audio-path methods are `noexcept`, allocation-free, lock-free, bounded (FR-016).

## Types

```cpp
enum class DetectMode   : std::uint8_t { peak, rms, peakHold };
enum class Ballistics   : std::uint8_t { branching, decoupled };
enum class DetectDomain : std::uint8_t { linear, decibel };
```

## Class

```cpp
class EnvelopeFollower {
public:
    // Prepare for a sample rate; caches fs, recomputes coefficients, clears state.
    // sampleRate <= 0 is guarded to a defined finite state (FR-018).
    void  init(float sampleRate) noexcept;

    // Configuration — recompute cached coefficients; do NOT reset runtime state.
    void  setMode(DetectMode) noexcept;
    void  setBallistics(Ballistics) noexcept;
    void  setSmooth(bool) noexcept;            // attack coeff applied in both stages
    void  setDomain(DetectDomain) noexcept;
    void  setAttack(float seconds) noexcept;   // time to 1 − 1/e (~63%) of a step
    void  setRelease(float seconds) noexcept;
    void  setHold(float seconds) noexcept;     // peakHold only; ignored otherwise
    void  setRmsWindow(float seconds) noexcept;// rms one-pole window; independent of attack/release

    // Clear all runtime state to the defined initial condition (env = 0).
    void  reset() noexcept;

    // Process one input sample; return the current envelope:
    //   linear amplitude, or dB (clamped at −120 dBFS) when domain == decibel.
    float process(float x) noexcept;
};
```

## Behavioral contract

| Aspect | Guarantee | Spec ref |
|---|---|---|
| Default config | After `init(fs)` only: peak, branching, non-smooth, linear — a working peak follower. | Assumptions |
| Attack timing | Peak-mode step reaches ~63% of target within `attackSeconds` (± tolerance), every topology. | SC-001 |
| Release timing | 1→0 step decays to ~37% within `releaseSeconds` (± tolerance). | SC-002 |
| Peak level | Steady sine amplitude A → peak envelope ≈ A. | SC-003 |
| RMS level | Steady sine amplitude A → RMS envelope ≈ A/√2. | SC-003 |
| RMS ripple | Settled RMS ripple below a named peak-to-peak bound. | SC-004 |
| Peak-hold dwell | Detected peak held ~`holdSeconds` (± one control period) before release; higher peak restarts hold. | SC-005, FR-015 |
| Topology independence of hold | Hold composes with both branching and decoupled. | FR-015 |
| dB level-independence | dB-domain attack time equal across levels ≥ 20 dB apart (± tolerance); linear does not. | SC-006 |
| dB floor | Level ≤ −120 dBFS → returns −120 dB, never −∞. | FR-012, SC-008 |
| Time-constant convention | `a = exp(−1/(τ·fs))`, identical across modes; RMS averaging is a separate stage. | FR-013 |
| RT-safety | Zero heap allocation on `process()` across all configs; coefficients cached in setters. | SC-007, FR-016 |
| Numerical safety | No NaN/Inf for any finite input; coefficients ∈ [0,1); silence → linear 0 / dB −120. | FR-018, SC-008 |
| Reset | `reset()` clears env/meanSquare/y1/heldPeak/holdCounter to 0. | FR-010 |

## Dependency contract
- **Allowed**: `<cmath>`, `<cstdint>`, `core/dsp/`. **Forbidden**: `core/effects/**`, any harness,
  any platform header (JUCE/libDaisy/Teensy). Enforced by `scripts/check-portability.sh` (FR-021).

## Out of scope (consumer's responsibility — `design:feature/compressors`)
Static gain computer (threshold/ratio/knee/makeup), VCA / gain application, sidechain EQ/filtering,
lookahead (FR-023).
