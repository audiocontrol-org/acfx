# Phase 1 Data Model: tape-dynamics

Entities, fields, relationships, and validation rules derived from the spec
(FR-001..024) and research (R1..R9). No runtime persistence — these are in-memory
DSP types. "State" = per-sample mutable audio-path state (RT-relevant).

## Entity: `Solver` (enum)

The numerical integration method for the JA ODE. Accuracy-vs-CPU selector.

| Member | Kind | Stages/step | Notes |
|---|---|---|---|
| `rk2` | explicit | 2 derivative evals | cheapest; conditionally stable, stabilized by oversampling |
| `rk4` | explicit | 4 derivative evals | higher order; default |
| `newtonRaphson` | implicit | bounded fixed iterations | best stiff-stability at low oversampling; capped iteration count + divergence bail |

Validation: unknown value is unreachable (strongly-typed enum); all three implemented in the first cut
(FR-005).

## Entity: `JAParams` (value struct)

The five Jiles-Atherton physical parameters (R1/R2). Plain scalars; no state.

| Field | Meaning | Constraint | Effect macro |
|---|---|---|---|
| `Ms` | saturation magnetization (ceiling) | `> 0` | `saturation`/`ceiling` |
| `a` | anhysteretic shape | `> 0` | (default; advanced) |
| `alpha` | inter-domain coupling | `≥ 0`, small | (default; advanced) |
| `k` | coercivity (loop width / memory) | `> 0` | `width` |
| `c` | reversibility (loop openness) | `0 ≤ c ≤ 1` | (default; advanced) |

Validation: setters clamp to the constrained domain (guarded, not mocked — Constitution V). Concrete
numeric ranges tuned during implementation (OQ3).

## Entity: `Hysteresis` (stateful primitive) — `core/primitives/nonlinear/hysteresis.h`

The graduated Jiles-Atherton primitive; **first stateful inhabitant of `nonlinear/`** (FR-001/016).

**Configuration (set outside the hot path)**
- `JAParams params` — the five physical parameters (per-parameter setters, FR-002).
- `Solver solver` — selected integration method (FR-005).
- `double sampleRate` — set in `prepare(sampleRate)` (FR-003); configures the integrator step size.

**State (per-sample mutable, RT)**
- `double M` — current magnetization (the output-bearing state).
- `double Hprev` — previous applied field (for `dH = H − Hprev` and `δ = sign(dH)`).
- (derived per step: `dH`, `H_e = H + α·M`, `M_an`, `dM_irr/dH`).

**Behavior**
- `void prepare(double sampleRate)` — set sample rate; allocate nothing on the hot path.
- `void reset()` — `M = 0`, `Hprev = 0` (defined initial condition, FR-003).
- `float process(float H)` — advance one step under the selected `Solver`; apply the stability guard
  (FR-006); return the magnetization-derived output. **No heap/locks; O(solver-stages) bounded**
  (FR-007).
- Internal: `double dMdH(double H, double M, double dH) const` — the shared JA derivative reused by all
  solvers (R3).

**Invariants**
- Holds state → identical inputs after `reset()` are reproducible (FR-003).
- Never returns NaN/Inf on finite input (guard, FR-006/SC-005).
- Standard library only; no platform headers (FR-007/Constitution IV).

**Relationships**: composed by `TapeDynamicsCore` (one instance per channel, run under the oversampler);
usable standalone by external primitive consumers.

## Entity: `TapeDynamicsCore` — `core/effects/tape-dynamics/tape-dynamics-core.h`

The RT kernel composing the primitives (FR-008/009/011). Template on the active oversampling `Factor`
(the effect holds one core per exposed factor and dispatches — R4).

**Owns**
- `Oversampler<Factor>` (shipped, verbatim) — per channel.
- `Hysteresis[channels]` — the magnetics state.
- optional `EnvelopeFollower[channels]` + `GainComputer` — the explicit trim (R6).
- cached macro→physics coefficients (drive gain, `Ms`, `k`, mix, output), trim config.

**Behavior**
- `prepare(...)` — size per-channel state; configure oversampler + hysteresis sample rate.
- `float processSample(float x, int ch)` — `x·drive → Oversampler<Factor>.process(·, JA step) →`
  optional trim `→ mix(dry, wet) · output`. Emergent compression happens inside the magnetics (not a
  control step — FR-012).
- `reset()` — clears all composed state.

## Entity: `TapeDynamicsParameters` — `core/effects/tape-dynamics/tape-dynamics-parameters.h`

The host-facing parameter surface (FR-010). Each is a `ParameterDescriptor` (id, range, default) per the
platform model, handed off lock-free.

| Param | Type | Range / values | Default | Maps to |
|---|---|---|---|---|
| `drive` | float (dB or linear) | tuned (OQ3) | unity | input gain into `H` |
| `saturation` (`ceiling`) | float | tuned (OQ3) | — | `Ms` |
| `width` | float | tuned (OQ3) | — | `k` |
| `solver` | enum | rk2 / rk4 / newtonRaphson | rk4 | `Hysteresis::solver` |
| `oversampling` | enum | 2× / 4× / 8× / 16× | **8×** | active `Oversampler<Factor>` |
| `trim.enabled` | bool | on/off | off | explicit trim gate |
| `trim.attack` | float (ms) | tuned | — | `EnvelopeFollower` attack |
| `trim.release` | float (ms) | tuned | — | `EnvelopeFollower` release |
| `trim.amount` | float | tuned | — | trim gain depth |
| `mix` | float | 0..1 | 1.0 | dry/wet |
| `output` | float (dB) | tuned | 0 dB | makeup |

Validation: out-of-range values clamped to the descriptor domain; `drive`=0 ⇒ defined unity passthrough
(FR-014). Emergent compression is deliberately **absent** from this table (FR-012).

## Entity: `TapeDynamicsEffect` — `core/effects/tape-dynamics/tape-dynamics-effect.h`

The host wrapper conforming to the platform `Effect` concept (FR-008), mirroring
`SaturationEffect`/`CompressorEffect`/`SvfEffect`.

**Behavior**
- `prepare(ProcessContext)` — allocate/size all state (per-channel cores, selected oversampler); no
  later allocation.
- `process(AudioBlock)` — consume lock-free parameter edits at the top; dispatch to the
  `TapeDynamicsCore` matching the selected oversampling factor; write output in place.
- parameter descriptors + presets exposed via `TapeDynamicsParameters` / `tape-dynamics-presets.h`.

**Relationships**: composes `TapeDynamicsCore` (which composes `Hysteresis`, `Oversampler`, and the
optional `EnvelopeFollower`+`GainComputer`).

## Entity: `TapeDynamicsPresets` — `core/effects/tape-dynamics/tape-dynamics-presets.h`

Named starting points (FR-013), e.g. gentle "glue" vs aggressive "saturate", each a fixed parameter set.
Exact preset list finalized in `/speckit-tasks`.

## State transitions

`Hysteresis`: `constructed → prepare(sr) → [process*] → reset() → [process*]`. `reset()` is the only
transition that clears memory; `prepare()` reconfigures step size (and implies a reset of derived
state). No other lifecycle states.
