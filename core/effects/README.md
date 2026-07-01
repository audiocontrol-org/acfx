> ============================================================================
> ‼ acfx COMMANDMENTS (non-negotiable) ‼
> 1. COMMIT AND PUSH EARLY AND OFTEN
> 2. NO GIT HOOKS, EVER
> 3. DESCRIPTIVE NAMES, NEVER NUMERIC PREFIXES
> (acfx Constitution, Principles I-III -- `.specify/memory/constitution.md`)
> ============================================================================

# Core Effects Taxonomy

`core/effects/` holds Production Effects (stage 4 of the graduation model):
composed, Effect-contract-compliant DSP effects. Each effect composes one or
more primitives from `core/primitives/` and exposes a unified parameter surface
via the Effect contract (constexpr `ParameterDescriptor` table, lock-free
cross-thread parameter handoff, RT-safe per-block `process()`).

## Governing Rule: Inhabit Before Creating (FR-008, SC-006)

> An effect folder is created **only** when it has a real inhabitant --
> at least one header that lives inside it. Uninhabited effect categories are
> documented here as **prospectus families** and are **never** materialized
> as empty directories on disk.

This means `ls core/effects/` shows only effects that contain code.
An empty `.gitkeep` directory is explicitly forbidden (SC-006). When a new
effect is ready to land, add the folder *in the same commit* as the
effect itself; never pre-create it.

---

## Inhabited Effects

These folders **exist on disk** because each contains at least one production
effect (FR-009).

### `svf/`

State-Variable Filter effect — a simple, single-primitive composition proving
the Effect contract and the cross-platform spine.

| Effect | Description |
|---|---|
| `svf/svf-effect.h` | State-Variable Filter Effect wrapper: constexpr `ParameterDescriptor` table (cutoff/resonance/mode), lock-free cross-thread parameter handoff, per-channel `SvfPrimitive` state. RT-safe; allocation-free per-block. |

Composed Primitive: `core/primitives/filters/svf-primitive.h`.
Lab: `core/labs/state-variable-filter/` (graduated state).
Tests: `tests/core/svf-effect-test.cpp`.

### `modulated-delay/`

Modulated Delay effect — a multi-primitive composition (delay line + LFO +
state-variable filter for tone).

| Effect | Description |
|---|---|
| `modulated-delay/modulated-delay-effect.h` | Modulated Delay Effect wrapper: constexpr parameter table (delay-time/feedback/mod-rate/mod-depth/tone), lock-free cross-thread handoff, per-channel state (delay buffer, LFO, SVF). RT-safe; allocation-free per-block. |
| `modulated-delay/wow-flutter.h` | Wow & Flutter characterization helper — not an effect, but used in tests and validation. |

Composed Primitives: `core/primitives/delays/delay-line.h`, `core/primitives/modulation/lfo.h`, `core/primitives/filters/svf-primitive.h`.
Tests: `tests/core/modulated-delay-*-test.cpp`.

### `saturation/`

Saturation — the first Production Effect born from the lab→effect graduation
pathway. A voiced saturation effect composing the Waveshaper (with naive/ADAA
anti-aliasing modes) between two emphasis filters.

| Effect | Description |
|---|---|
| `saturation/saturation-core.h` | RT-safe composition kernel: pre-emphasis → Waveshaper(drive,bias) → post-de-emphasis → tone → dry/wet mix → output. Signal chain is gain-staged with per-voicing emphasis curves and a parallel dry/wet blend. |
| `saturation/saturation-voicings.h` | Voicing table: four voicings (Soft Clip, Tape, Console, Tube Preamp), each fixing a Waveshaper shape and per-voicing pre/post-emphasis SVF coefficients. Runtime-selectable; bias remains a user control. |
| `saturation/saturation-effect.h` | Saturation Effect wrapper: constexpr `ParameterDescriptor` table (drive/voicing/tone/mix/output/bias/quality), lock-free cross-thread parameter handoff, per-channel `SaturationCore` state. RT-safe; allocation-free per-block. |

Composed Primitives: `core/primitives/nonlinear/waveshaper.h` (or `adaa-waveshaper.h` per quality), `core/primitives/filters/svf-primitive.h`.
Lab: `core/labs/saturation/` (persists as README + host-only harness driving the graduated effect).
Tests: `tests/core/saturation-*-test.cpp`.

---

## Dependency Contract

Every header in `core/effects/` follows these rules:

- **Allowed dependencies**: `core/dsp/` (the substrate), `core/primitives/**`
  (the composed building blocks).
- **Forbidden**: `core/labs/*/harness/**`, any platform header (JUCE,
  libDaisy, Teensy), other effects.
- **Effect contract**: a class or struct exposing `prepare(Spec)`, `reset()`,
  `process(AudioBlock&)`, `setParameter(ParamId, float)`, and a constexpr
  `kParams` array of `ParameterDescriptor` with a compile-time validation
  `isValidDescriptor` static_assert (mirroring `core/effects/svf/svf-effect.h`).
- **RT-safety**: no heap allocation, locks, or unbounded work in any
  `process()` / audio-callback path (Constitution Principle VI).
- **Size budget**: each file stays within ~300-500 lines (Constitution
  Principle VII).

Violations are caught mechanically by `scripts/check-portability.sh`
(FR-016, FR-017), which is run on purpose -- locally and in CI -- never as
a git hook (FR-018, Constitution Principle II).

---

## Relationship to the Three Layers

```
core/
  labs/        -- Theory + Laboratory stage: README + kernel + host-only harness
  primitives/  -- Reusable Primitive stage
  effects/     -- Production Effect stage: composes primitives (this directory)
  dsp/         -- Shared substrate (contracts only; not one of the three layers)
```

An effect is typically born as a concept in a lab (`core/labs/<name>/`), where
the composition kernel and voicing table are authored and validated. The kernel
and voicing table are then **graduated** (moved via `git mv`) into
`core/effects/<name>/`, where the Effect-contract wrapper is added. The
originating lab persists as README + host-only harness; it never disappears
(Constitution Principle IX). This path represents stage 4 of the graduation
model.

See `specs/three-layer-structure/contracts/lab-folder.md` for the full lab
lifecycle contract and `specs/three-layer-structure/spec.md` (FR-007 through
FR-012, SC-003, SC-006, SC-007) for the authoritative requirements.
