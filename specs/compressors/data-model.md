# Data Model — Compressors (Gain Computer + Compressor Effect)

**Feature**: `specs/compressors` | **Date**: 2026-07-02 | **Phase**: 1 (Design & Contracts)

A DSP feature's "data model" is its state and configuration types (no persistence). Entities below map
to the spec's Key Entities and the API contracts (`contracts/gain-computer-api.md`,
`contracts/compressor-effect-api.md`).

## Enumerations

### `GainMode` (uint8) — GainComputer
| Value | Meaning |
|---|---|
| `compress` | Above threshold, slope `1/ratio`; unity below. |
| `limit` | Above threshold, slope 0 (ratio → ∞ / brickwall at threshold); unity below. |
| `expand` | Below threshold, downward expansion by ratio, bounded by `range`; unity above. |
| `gate` | Below (knee-rounded) threshold, attenuate toward `range` floor; unity above. |

### `Detection` (uint8) — CompressorCore
| Value | Meaning |
|---|---|
| `feedForward` | Detector reads the (keyed/filtered) input. |
| `feedBack` | Detector reads the previous **post-makeup, pre-mix** output sample. |

### `BallisticsSite` (uint8) — CompressorCore
| Value | Meaning |
|---|---|
| `level` | Attack/release smooth the detected level (EnvelopeFollower); the curve is instantaneous. |
| `gain` | The curve maps the ~instantaneous level; a second smoother applies attack/release to the gain reduction. |

### `StereoLink` (uint8) — CompressorEffect
| Value | Meaning |
|---|---|
| `perChannel` | Independent detection per channel. |
| `linked` | One detector value (max across linked channels) drives a common gain. |

*(Detector `peak`/`rms` reuses the shipped `acfx::DetectMode`; the level path uses the shipped
`acfx::DetectDomain::decibel` for level-independent time constants.)*

## Entity — `GainComputer` (stateless primitive)

Pure static curve. **No runtime state** — all fields are configuration; `computeGainDb` is `const`.

### Configuration (set via `set*`; no cached per-sample coefficients needed — the map is direct arithmetic)
| Field | Type | Units / Range | Notes |
|---|---|---|---|
| `mode` | GainMode | enum | Default `compress`. |
| `thresholdDb` | float | dBFS | Level above/below which the curve bends. Default −18 (tuning-pass placeholder). |
| `ratio` | float | ≥ 1 | Compression/expansion slope denominator. `limit` mode ignores it (∞). Default 4. |
| `kneeDb` | float | dB, ≥ 0 | Unified quadratic knee width straddling threshold; 0 = hard. Default 6. |
| `rangeDb` | float | dB, ≤ 0 | Expander/gate max attenuation (floor). Default −40. |

### Behavior
`computeGainDb(levelDb) → gainReductionDb (≤ 0)`, the analytic piecewise curve with the unified
quadratic C¹ knee (see `contracts/gain-computer-api.md`). Branch-only arithmetic; no transcendental; no
allocation; no state.

### Invariants
- Output ≤ 0 dB for compress/limit (attenuation) and ≤ 0 dB bounded by `rangeDb` for expand/gate.
- C¹-continuous across the knee for all modes; reduces to the hard corner at `kneeDb = 0`.
- Guarded degenerate inputs (`ratio < 1` clamped to 1; non-finite level handled) → no NaN/Inf (FR-024).
- Call-order independent (stateless): `computeGainDb(a)` then `computeGainDb(b)` == `computeGainDb(b)`
  then `computeGainDb(a)`.

## Entity — `CompressorCore` (per-channel composition kernel)

Composes the shipped primitives; holds per-channel runtime state.

### Composed primitives (owned, one set per channel)
| Member | Type | Role |
|---|---|---|
| `detector` | EnvelopeFollower | Level detection + (level-site) ballistics; decibel domain. |
| `gain` | GainComputer | The static curve. |
| `scFilter` | SvfPrimitive | Sidechain highpass (bypassed when cutoff 0). |
| `lookahead` | DelayLine | Main-path pre-delay (bypassed when lookahead 0). |
| `gainSmoother` | EnvelopeFollower (or one-pole) | Gain-site ballistics on the gain-reduction signal. |

### Configuration
| Field | Type | Units / Range | Notes |
|---|---|---|---|
| `detection` | Detection | enum | feedForward / feedBack. Default feedForward. |
| `ballisticsSite` | BallisticsSite | enum | level / gain. Default level. |
| `detectorMode` | DetectMode | enum | peak / rms. Default (tuning) — see Deferred (per-topology defaults). |
| `attackSeconds` / `releaseSeconds` | float | s, ≥ 0 | Fed to the active smoother (level or gain site). |
| `scHpfHz` | float | Hz, ≥ 0 | 0 = bypass. |
| `lookaheadSamples` | int | ≥ 0 | `round(lookaheadMs·fs)`; buffer sized in `prepare()`. |
| `makeupDb` | float | dB | Manual makeup. |
| `autoMakeup` | bool | — | When on, `makeupDb` derived `= −gain.computeGainDb(0)` (0 for expand/gate). |
| `mix` | float | 0..1 | Dry/wet (parallel). |
| `outputGain` | float (linear) | — | `dbToLin(outputDb)`. |

### Runtime state (per channel; cleared by `reset()`, RT-mutated in `process()`)
| Field | Type | Notes |
|---|---|---|
| `prevOutput` | float | Feedback tap (post-makeup, pre-mix) from the previous sample; cold-start = floor. |
| (delegated) | — | detector/gainSmoother/scFilter/lookahead internal state. |

### Per-sample chain (conceptual)
```
key   = externalKey ? key : x
key   = (scHpfHz>0) ? scFilter(key) : key
level = (detection==feedBack) ? detect(prevOutput) : detect(key)   // detector in dB domain
grDb  = gain.computeGainDb(level)                                   // static curve (instantaneous)
grDb  = (site==gain) ? gainSmoother(grDb) : grDb                    // gain-site ballistics
gLin  = dbToLin(grDb + makeupDb)                                    // makeup folded in
main  = (lookaheadSamples>0) ? lookahead.read(x) : x
comp  = main * gLin
prevOutput = comp                                                   // feedback tap: post-makeup, pre-mix
y     = mix*comp + (1−mix)*x                                        // parallel
y     = y * outputGain
return y
```
*(level-site: `detect(...)` applies attack/release; gain-site: the detector runs ~instantaneous and
`gainSmoother` supplies attack/release.)*

### Invariants
- `process()` allocation-free, lock-free, bounded (Constitution VI); the only buffer (lookahead) is
  sized in `prepare()`.
- Feedback loop stable (no divergence/oscillation) for bounded input; `prevOutput` cold-start defined.
- No NaN/Inf for any finite input/config (SC-013).

## Entity — `CompressorEffect` (host-facing wrapper)

Owns `kMaxChannels` `CompressorCore` instances; the `SaturationEffect` idiom (constexpr descriptor
table, lock-free atomic handoff, `static_assert` validation). Reports lookahead latency to the host.

### Parameter table (constexpr `ParameterDescriptor[]` — single source of truth; dense ids)
Descriptor **shapes are normative**; exact numeric ranges are a **tuning-pass placeholder** (mirroring
`SaturationEffect`).

| id | name | kind | units | range (placeholder) | default |
|---|---|---|---|---|---|
| 0 | threshold | continuous | dB | −60..0 | −18 |
| 1 | ratio | continuous | ratio | 1..20 (∞ via limit) | 4 |
| 2 | knee | continuous | dB | 0..24 | 6 |
| 3 | attack | continuous | ms | 0.1..200 | 10 |
| 4 | release | continuous | ms | 1..2000 | 100 |
| 5 | mode | discrete | — | {compress,limit,expand,gate} | compress |
| 6 | detection | discrete | — | {feedForward,feedBack} | feedForward |
| 7 | detector | discrete | — | {peak,rms} | rms |
| 8 | ballisticsSite | discrete | — | {level,gain} | level |
| 9 | range | continuous | dB | −80..0 | −40 |
| 10 | scHpf | continuous | Hz | 0..500 | 0 |
| 11 | lookahead | continuous | ms | 0..20 | 0 |
| 12 | makeup | continuous | dB | −24..24 | 0 |
| 13 | autoMakeup | discrete | — | {off,on} | off |
| 14 | stereoLink | discrete | — | {perChannel,linked} | linked |
| 15 | mix | continuous | — | 0..1 | 1 |
| 16 | output | continuous | dB | −24..24 | 0 |

### Invariants
- Every descriptor passes `isValidDescriptor` (build-time `static_assert`): max>min; discrete ⇒
  count≥2 and labels.size()==count.
- `setParameter` callable from any thread; publishes a lock-free atomic pending value consumed at the
  top of `process()` (FR-020). `prepare`/`reset` mutate coefficients directly (stream stopped).
- Reported latency = `round(lookaheadMs·fs)`, established in `prepare()`.

## Entity — Lab (`core/labs/compressor/`)
Non-code deliverable: `README.md` (gain-computer curve math, unified knee, feedforward/feedback,
level-vs-gain ballistics, auto-makeup theory + graduation-target walkthrough) and a host-only
`harness/` that drives the kernel/effect to produce static-curve + step + latency measurement evidence.
Persists after graduation as living documentation (Constitution IX).
