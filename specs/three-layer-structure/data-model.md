# Data Model: Three-Layer DSP Core Structure

This feature has no runtime data model. Its "entities" are **source-tree structural
units** and the **dependency rules** that relate them — the model a reviewer and the
portability gate reason over.

## Structural entities

### Layer

- **Identity**: one of `labs`, `primitives`, `effects` (subdirectories of `core/`).
- **Substrate (not a layer)**: `core/dsp/` — contracts only (`Effect`, `ProcessContext`, `AudioBlock`, parameter model). Sits beneath all three layers; never counted as one.
- **Attributes**: name; allowed-dependency set (see Dependency rules).

### Laboratory

- **Identity**: directory `core/labs/<concept>/` (e.g. `state-variable-filter`).
- **Required members**: `README.md` (theory + walkthrough + named graduation target); a portable RT-safe **kernel** (pre-graduation) OR a reference to the graduated primitive (post-graduation); a `harness/` subdirectory (host-only).
- **Lifecycle states**:
  - `pre-graduation` — kernel header lives inside the lab; harness drives the in-lab kernel.
  - `graduated` — kernel relocated to `core/primitives/<category>/`; lab retains README + harness; harness now drives the graduated primitive.
- **Invariant**: educational code evolves; the lab folder is never deleted on graduation (Principle IX).

### Kernel

- **Identity**: the portable, RT-safe algorithmic header of a lab/primitive.
- **Constraint**: no heap/locks/unbounded work in `process()` (Principle VI); no platform headers (Principle IV); ≤ 500 lines (Principle VII).
- **Allowed dependencies**: `core/dsp/` only.

### Harness

- **Identity**: source under `core/labs/<concept>/harness/`.
- **Constraint**: host-only; never compiled into `acfx_core`; never in an MCU cross-compile set; may allocate/plot/measure freely.
- **Allowed dependencies**: `core/dsp/`, `core/primitives/**`, the lab's own kernel. **Nothing portable may depend on a harness** (the load-bearing reverse-prohibition).

### Primitive

- **Identity**: a header under `core/primitives/<category>/` (e.g. `filters/svf-primitive.h`).
- **Constraint**: same RT-safety/platform/size constraints as a kernel.
- **Allowed dependencies**: `core/dsp/`, other primitives, vendored pure-DSP libs (DaisySP). **Never** an effect.

### Taxonomy category

- **Identity**: a subdirectory of `core/primitives/` (e.g. `filters`, `delays`, `modulation`).
- **Rule**: created only when it has ≥ 1 inhabitant; uninhabited intended categories are listed in `core/primitives/README.md`, not materialized.
- **Known set (this feature)**: inhabited — `filters` (svf-primitive), `delays` (delay-line), `modulation` (lfo). Documented-only — `nonlinear`, `dynamics`, `analog`, `circuit`, `convolution`, `wdf`, `physical` (prospectus families).

### Effect

- **Identity**: a directory under `core/effects/<name>/`.
- **Allowed dependencies**: `core/primitives/**`, `core/dsp/`. Composes primitives.

## Dependency rules (the relation the gate enforces)

```
effects/  ──►  primitives/  ──►  dsp/
   │              │
   └──────────────┴──────────────►  dsp/        (all portable code may use the substrate)

labs/<c>/kernel  ──►  dsp/                       (kernel: substrate only)
labs/<c>/harness ──►  primitives/, dsp/, kernel  (host-only, one-directional consumer)

FORBIDDEN (any of these is a gate failure):
  primitives/**        includes  effects/**
  any portable core    includes  labs/*/harness/**     (core = dsp, primitives, effects, lab kernels)
  any MCU target       compiles  labs/*/harness/**
```

## Migration map (state transition applied by this feature)

| Entity | From | To | Consumers re-pointed |
|---|---|---|---|
| SVF primitive | `core/primitives/svf-primitive.h` | `core/primitives/filters/svf-primitive.h` | `core/effects/svf/svf-effect.h`, `core/effects/modulated-delay/modulated-delay-effect.h` |
| Delay line | `core/primitives/delay-line.h` | `core/primitives/delays/delay-line.h` | `core/effects/modulated-delay/{wow-flutter.h,modulated-delay-effect.h}`, `tests/core/delay-line-test.cpp` |
| LFO | `core/primitives/lfo.h` | `core/primitives/modulation/lfo.h` | `core/effects/modulated-delay/{wow-flutter.h,modulated-delay-effect.h}`, `tests/core/lfo-test.cpp` |
| SVF lab | (none) | `core/labs/state-variable-filter/{README.md,harness/svf-harness.cpp}` | new host-only target |
| Taxonomy doc | (none) | `core/primitives/README.md` | — |
