# Data Model — Program-Dependent Saturation (Dynamics Modulator + Effect)

**Feature**: `specs/program-dependent-saturation` | **Date**: 2026-07-03 | **Phase**: 1 (Design & Contracts)

A DSP feature's "data model" is its state and configuration types (no persistence). Entities below map
to the spec's Key Entities and the API contracts (`contracts/dynamics-modulator-api.md`,
`contracts/program-dependent-saturation-effect-api.md`).

## Enumerations

### `ModCurve` (uint8) — DynamicsModulator
| Value | Meaning |
|---|---|
| `linear` | `curve(n) = n` — offset proportional to normalized envelope. |
| `logarithmic` | Concave: reaches offset quickly at low level, flattens (early onset). |
| `exponential` | Convex: stays low until high level, then rises fast (late onset). |

*(All three map `[0,1]→[0,1]`, pass through (0,0) and (1,1), monotone, bounded.)*

### `ModTarget` (uint8) — ProgramDependentSaturationCore
| Value | Native span (SaturationCore setter) | Update rate |
|---|---|---|
| `drive` | dB pre-gain (via `setDrive`, scalar) | per-sample |
| `bias` | −1..+1 asymmetry (via `setBias`, scalar) | per-sample |
| `tone` | −1..+1 tilt (via `setTone`, **SVF recompute**) | **per-block** |
| `mix` | 0..1 dry/wet (via `setMix`, scalar) | per-sample |

### `Detection` (uint8) — ProgramDependentSaturationCore
| Value | Meaning |
|---|---|
| `feedForward` | Detector reads the (keyed/filtered) input. |
| `feedBack` | Detector reads the previous **final output sample `y`** (post-mix, post-output). |

### `DynamicPreset` (uint8) — ProgramDependentSaturationEffect
| Value | Meaning |
|---|---|
| `none` | All target depths 0 — the static saturator (orthogonality baseline). |
| `opto` | Slow, level-smoothed downward drive softening (optical character). |
| `variMu` | Level-dependent bias + drive push (vari-mu tube character). |
| `tapeComp` | Drive push + mix/self-compression, tape-ish ballistics. |

### `StereoLink` (uint8) — ProgramDependentSaturationEffect
| Value | Meaning |
|---|---|
| `perChannel` | Independent detection/modulation per channel. |
| `linked` | One detector value (max across linked channels) drives common modulation. |

*(Detector `peak`/`rms`/`peakHold` and `branching`/`decoupled` reuse the shipped `acfx::DetectMode` /
`acfx::Ballistics`; the envelope uses the shipped `acfx::DetectDomain::decibel`.)*

## Entity — `DynamicsModulator` (stateless primitive)

Pure envelope→signed-offset mapper. **No runtime state** — all fields are configuration; `modulate` is
`const`.

### Configuration (set via `set*`; no cached per-sample coefficients — the map is direct arithmetic)
| Field | Type | Units / Range | Notes |
|---|---|---|---|
| `depth` | float | −1..+1 | Signed; sign = direction, magnitude = amount. Default 0. |
| `curve` | ModCurve | enum | Response shaping. Default `linear`. |

### Behavior
`modulate(envNorm) → signedOffsetUnit`, where `envNorm ∈ [0,1]` is the normalized-dB-window envelope and
the result is `depth · curve(envNorm)` in **normalized units** (the caller multiplies by the target's
native span). Bounded arithmetic; no transcendental beyond the fixed curve law; no allocation; no state.
See `contracts/dynamics-modulator-api.md`.

### Invariants
- `modulate` is `const`, holds no runtime state; call-order independent (SC-007).
- `depth = 0` ⇒ `modulate` returns 0 for any envelope (the orthogonality identity, FR-007).
- Sign of the result follows `sign(depth)`; magnitude monotone in `envNorm`.
- Finite for `envNorm ∈ [0,1]` and any `depth ∈ [-1,1]` — no NaN/Inf (FR-003, SC-014).

## Entity — `ProgramDependentSaturationCore` (per-channel composition kernel)

Composes the shipped units **unchanged**; holds per-channel runtime state.

### Composed units (owned, one set per channel)
| Member | Type | Role |
|---|---|---|
| `saturation` | SaturationCore | The nonlinearity (voicings, drive/bias/tone/mix/output) — **unchanged**. |
| `detector` | EnvelopeFollower | Shared level detection + ballistics; decibel domain. |
| `scFilter` | SvfPrimitive | Sidechain highpass (bypassed when cutoff 0). |
| `driveMod`, `biasMod`, `toneMod`, `mixMod` | DynamicsModulator ×4 | Per-target signed-offset mappers, fed the shared envelope. |

### Configuration
| Field | Type | Units / Range | Notes |
|---|---|---|---|
| `staticDrive/Bias/Tone/Mix` | float | native per target | The base values the offsets add to. |
| `detection` | Detection | enum | feedForward / feedBack. Default feedForward. |
| `detectorMode` | DetectMode | enum | peak / rms / peakHold. |
| `ballistics` | Ballistics | enum | branching / decoupled. |
| `attackSeconds` / `releaseSeconds` | float | s, ≥ 0 | Fed to the shared `EnvelopeFollower`. |
| `refWindowLoDb` / `refWindowHiDb` | float | dBFS | Normalization window (default −60 / 0). |
| `scHpfHz` | float | Hz, ≥ 0 | 0 = bypass. |
| `externalKey` | bool | — | When set, detection reads the key input. |

### Runtime state (per channel; cleared by `reset()`, RT-mutated in `process()`)
| Field | Type | Notes |
|---|---|---|
| `prevOutput` | float | Feedback tap (final output `y`) from the previous sample; cold-start = floor. |
| `toneBlockOffset` | float | The per-block tone offset applied for the current block. |
| (delegated) | — | detector / scFilter / saturation internal state. |

### Per-sample chain (conceptual)
```
src   = externalKey ? key : x
src   = (scHpfHz>0) ? scFilter(src) : src
det   = (detection==feedBack) ? prevOutput : src
env   = detector.process(det)                         // dB-domain envelope
norm  = clamp((env − refLo) / (refHi − refLo), 0, 1)  // normalized-dB window
drive'= clamp(staticDrive + driveMod.modulate(norm)*driveSpan, driveRange)   // per-sample
bias' = clamp(staticBias  + biasMod.modulate(norm)*biasSpan,   biasRange)    // per-sample
mix'  = clamp(staticMix   + mixMod.modulate(norm)*mixSpan,     mixRange)     // per-sample
saturation.setDrive(drive'); saturation.setBias(bias'); saturation.setMix(mix')
// tone' applied ONCE per block (see block boundary below), not per-sample
y     = saturation.process(x)                         // runs the unchanged nonlinearity
prevOutput = y                                        // feedback tap = final output
return y
```
Block boundary (once per block): `tone' = clamp(staticTone + toneMod.modulate(normBlock)*toneSpan,
toneRange); saturation.setTone(tone')` — where `normBlock` is the block's representative envelope
(e.g. first/last sample); skipped entirely when `toneMod.depth == 0` (orthogonality guard).

### Invariants
- `process()` allocation-free, lock-free, bounded (Constitution VI); no dynamic buffers.
- **Zero-depth orthogonality**: all target depths 0 ⇒ every `modulated == static base` ⇒ output identical
  to the standalone `SaturationCore`/`SaturationEffect` (FR-007, SC-002).
- Feedback loop stable (no divergence/oscillation) for bounded input; `prevOutput` cold-start defined.
- Every modulated parameter clamped into `SaturationCore`'s valid range (FR-010); no NaN/Inf for any
  finite input/config (SC-014).
- Single shared detector feeds all four modulators (MCU-viable, FR-020).

## Entity — `ProgramDependentSaturationEffect` (host-facing wrapper)

Owns `kMaxChannels` `ProgramDependentSaturationCore` instances; the `SaturationEffect` idiom (constexpr
descriptor table, lock-free atomic handoff, `static_assert` validation).

### Parameter table (constexpr `ParameterDescriptor[]` — single source of truth; dense ids)
Descriptor **shapes are normative**; exact numeric ranges are a **tuning-pass placeholder** (mirroring
`SaturationEffect`). The static passthrough block mirrors `SaturationEffect` exactly so orthogonality is
a like-for-like comparison.

| id | name | kind | units | range (placeholder) | default |
|---|---|---|---|---|---|
| 0 | drive | continuous | dB | 0..48 | 0 |
| 1 | voicing | discrete | — | {softClip,tape,console,tubePreamp} | softClip |
| 2 | tone | continuous | — | −1..1 | 0 |
| 3 | mix | continuous | — | 0..1 | 1 |
| 4 | output | continuous | dB | −24..24 | 0 |
| 5 | bias | continuous | — | −1..1 | 0 |
| 6 | quality | discrete | — | {naive,adaa,oversampled} | adaa |
| 7 | detector | discrete | — | {peak,rms,peakHold} | rms |
| 8 | ballistics | discrete | — | {branching,decoupled} | branching |
| 9 | attack | continuous | ms | 0.1..200 | 10 |
| 10 | release | continuous | ms | 1..2000 | 100 |
| 11 | detection | discrete | — | {feedForward,feedBack} | feedForward |
| 12 | driveDepth | continuous | — | −1..1 | 0 |
| 13 | driveCurve | discrete | — | {linear,log,exp} | linear |
| 14 | biasDepth | continuous | — | −1..1 | 0 |
| 15 | biasCurve | discrete | — | {linear,log,exp} | linear |
| 16 | toneDepth | continuous | — | −1..1 | 0 |
| 17 | toneCurve | discrete | — | {linear,log,exp} | linear |
| 18 | mixDepth | continuous | — | −1..1 | 0 |
| 19 | mixCurve | discrete | — | {linear,log,exp} | linear |
| 20 | dynamicPreset | discrete | — | {none,opto,variMu,tapeComp} | none |
| 21 | externalSidechain | discrete | — | {off,on} | off |
| 22 | scHpf | continuous | Hz | 0..500 | 0 |
| 23 | stereoLink | discrete | — | {perChannel,linked} | perChannel |

*(~24 parameters — larger than `SaturationEffect`'s 7; FR-025 file split is likely and is a
`/speckit-tasks` decision. `dynamicPreset` is an apply-once convenience that writes the matrix
parameters; its interaction with subsequent manual edits is the US9 Deferred Decision.)*

### Invariants
- Every descriptor passes `isValidDescriptor` (build-time `static_assert`): max>min; discrete ⇒ count≥2
  and labels.size()==count.
- `setParameter` callable from any thread; publishes a lock-free atomic pending value consumed at the top
  of `process()` (FR-017). `prepare`/`reset` mutate coefficients directly (stream stopped).
- Default configuration (`dynamicPreset=none`, all depths 0, feedForward, perChannel, no key, no SC HPF)
  is exactly the static saturator (US3).

## Entity — Lab (`core/labs/program-dependent-saturation/`)
Non-code deliverable: `README.md` (envelope-modulates-nonlinearity theory — dynamic drive/bias/tone/mix,
signed depth + response curves, feedforward/feedback, the normalized-dB-window mapping, opto/vari-mu/
tape-comp character theory + graduation-target walkthrough) and a host-only `harness/` that drives the
kernel/effect to produce orthogonality + THD-vs-level + step-response measurement evidence. Persists after
graduation as living documentation (Constitution IX).
