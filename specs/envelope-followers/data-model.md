# Data Model — Envelope Followers (Dynamics Level-Detector Primitive)

**Feature**: `specs/envelope-followers` | **Date**: 2026-07-02 | **Phase**: 1 (Design & Contracts)

A DSP primitive's "data model" is its state and its configuration types (no persistence). Entities
below map to the spec's Key Entities and the API contract (`contracts/envelope-follower-api.md`).

## Enumerations

### `DetectMode` (uint8)
| Value | Meaning |
|---|---|
| `peak` | Instantaneous rectified level `|x|`. |
| `rms` | Moving root-mean-square (one-pole mean-square → `sqrt`). |
| `peakHold` | Latched peak held for `hold` seconds before release resumes. |

### `Ballistics` (uint8)
| Value | Meaning |
|---|---|
| `branching` | Single-state smoother; attack coeff on rising level, release coeff on falling. |
| `decoupled` | Release smoother feeds an attack smoother (no release-then-attack artifact). |

### `DetectDomain` (uint8)
| Value | Meaning |
|---|---|
| `linear` | Envelope in linear amplitude (base contract). |
| `decibel` | Level clamped to −120 dBFS, converted to dB before smoothing; envelope returned in dB. |

## Entity — `EnvelopeFollower`

The stateful level-detector primitive. All fields are scalars (no heap, no buffer).

### Configuration (set via `init` / `set*`; changes recompute cached coefficients, no reset)
| Field | Type | Units / Range | Notes |
|---|---|---|---|
| `sampleRate` | float | Hz, > 0 | Set by `init`; guarded (FR-018). |
| `mode` | DetectMode | enum | Default `peak`. |
| `ballistics` | Ballistics | enum | Default `branching`. |
| `smooth` | bool | — | Smooth variant: attack coeff in both smoothing stages. Default `false`. |
| `domain` | DetectDomain | enum | Default `linear`. |
| `attackSeconds` | float | s, ≥ 0 | Time to 1 − 1/e of a step; → `aAtk`. |
| `releaseSeconds` | float | s, ≥ 0 | Time to 1 − 1/e of a step; → `aRel`. |
| `holdSeconds` | float | s, ≥ 0 | Peak-hold only; → `holdSamples`. |
| `rmsWindowSeconds` | float | s, ≥ 0 | RMS one-pole mean-square time constant; → `aRms`. Independent of attack/release. |

### Derived / cached (computed in setters, never in `process()`)
| Field | Type | Derivation |
|---|---|---|
| `aAtk` | float [0,1) | `exp(−1/(attackSeconds·sampleRate))`, guarded. |
| `aRel` | float [0,1) | `exp(−1/(releaseSeconds·sampleRate))`, guarded. |
| `aRms` | float [0,1) | `exp(−1/(rmsWindowSeconds·sampleRate))`, guarded. |
| `holdSamples` | int ≥ 0 | `round(holdSeconds·sampleRate)`. |

### Runtime state (cleared by `reset()`; RT-mutated in `process()`)
| Field | Type | Notes |
|---|---|---|
| `env` | float | Current envelope (linear amplitude, or dB when `domain==decibel`). |
| `meanSquare` | float | RMS one-pole accumulator (rms mode). |
| `y1` | float | Decoupled topology's release-stage state. |
| `heldPeak` | float | peak-hold latched value. |
| `holdCounter` | int | Samples remaining in the current hold window. |

### Invariants
- `aAtk, aRel, aRms ∈ [0, 1)`; `holdSamples ≥ 0` (FR-018).
- After `init()`/`reset()`: `env = meanSquare = y1 = heldPeak = 0`, `holdCounter = 0`; the linear
  envelope of silence is exactly 0, the dB envelope of silence is −120 dBFS.
- No `process()` output is NaN/Inf for any finite input in any configuration (SC-008).

## Entity — Envelope signal (output)
The per-sample `float` returned by `process(x)`: a control-rate amplitude (linear) or a dB value
(decibel domain), tracking the input level per the configured mode/topology.

## Entity — Lab (`core/labs/envelope-follower/`)
Non-code deliverable: `README.md` (ballistics theory + graduation-target walkthrough) and a host-only
`harness/` that drives the kernel to produce attack/release + RMS/hold measurement evidence. Persists
after graduation as living documentation (Constitution IX).

## State transitions (per-sample, conceptual)
```
detect: d = (peak) |x|
          | (rms)  sqrt(meanSquare ← aRms·meanSquare + (1−aRms)·x²)
          | (peakHold) latch/hold(|x|, holdCounter, heldPeak)
domain: s = (decibel) toDb(clamp(d, −120dBFS)) : d
smooth: env = (branching) branch(s, aAtk, aRel)
             | (decoupled) attackStage(releaseStage(s, aRel), aAtk[, smooth])
return env
```
