# Contract: `TapeDynamicsEffect` + `TapeDynamicsCore` API

**Headers**: `core/effects/tape-dynamics/tape-dynamics-{core,effect,parameters,presets}.h`
**Namespace**: `acfx`
**Layer**: production effect (composes `Hysteresis`, `Oversampler<Factor>`, optional
`EnvelopeFollower`+`GainComputer`)
**Satisfies**: FR-008..014, SC-003, SC-004, SC-005, SC-007

Conforms to the platform `Effect` concept exactly as `SaturationEffect`/`CompressorEffect`/`SvfEffect`.
Platform-independent; RT-safe (`process()` allocation-free, lock-free, bounded).

## Effect surface (host-facing)

```cpp
class TapeDynamicsEffect {
public:
    void prepare(const ProcessContext& ctx) noexcept;   // sizes all state; selects oversampler factor
    void process(AudioBlock& block) noexcept;           // consumes lock-free param edits, then runs core
    void reset() noexcept;

    // Parameter descriptors + presets (see tape-dynamics-parameters.h / -presets.h)
    static std::span<const ParameterDescriptor> parameters() noexcept;
};
```

## Parameters (FR-010)

`drive`, `saturation`/`ceiling` (→`Ms`), `width` (→`k`), `solver` (rk2/rk4/newtonRaphson),
`oversampling` (**2×/4×/8×, default 8×**), `trim.enabled`, `trim.attack`, `trim.release`,
`trim.amount`, `mix` (0..1, default 1), `output` (makeup). Numeric ranges tuned in implementation
(OQ3). **Emergent compression is NOT a parameter** (FR-012).

## Core surface (RT kernel)

```cpp
template <int Factor>
class TapeDynamicsCore {
public:
    void prepare(double sampleRate, int channels) noexcept;
    [[nodiscard]] float processSample(float x, int ch) noexcept; // drive→OS(JA)→trim?→mix·output
    void reset() noexcept;
};
```

Signal flow per sample/channel:
`x·drive → Oversampler<Factor>::process(·, JA step) → [optional trim] → mix(dry, wet) · output`.

## Contract guarantees

- **E1 (memory)**: at moderate `drive` a full-scale sinusoid yields a saturated, band-limited output
  whose transfer traces a closed hysteresis loop (US1.1).
- **E2 (unity passthrough)**: `drive` = 0 / bypass ⇒ output ≈ input at unity gain, no artifacts
  (FR-014, US1.2, SC-005).
- **E3 (finiteness)**: no finite input, at any parameter setting or solver, produces NaN/Inf (SC-005).
- **E4 (oversampler reuse)**: the JA step runs strictly as the `evalAtHighRate` callable of
  `Oversampler<Factor>::process(x, eval)`; the shipped oversampler is unmodified (FR-009, US7.2).
- **E5 (aliasing)**: increasing `oversampling` monotonically reduces the alias-sweep metric on a hot
  tone (SC-004, US7.1).
- **E6 (emergent compression)**: with `trim.enabled = false`, the measured output-vs-input level curve
  is monotonic and compressive above a threshold, and the dynamic-range-reduction metric increases with
  `drive` (FR-020, SC-003, US4).
- **E7 (trim no-op equivalence)**: with `trim.enabled = false`, the signal path is **bit-exact** the
  magnetics-only core; enabling it applies envelope-driven gain following the attack/release controls
  (FR-011, US6).
- **E8 (RT-safety)**: `prepare()` allocates all state; `process()` performs no allocation and takes no
  locks; output is continuous/click-free across block sizes incl. 1-sample and large blocks (FR-008,
  US5, SC-007).

## Validation hooks

Measured host-side via `host/analysis/` (`thdn.h`, `alias-sweep.h`) plus the harness's loop-area and
dynamic-range-reduction metrics; see `quickstart.md`.
