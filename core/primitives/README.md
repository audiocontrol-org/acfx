> ============================================================================
> ‼ acfx COMMANDMENTS (non-negotiable) ‼
> 1. COMMIT AND PUSH EARLY AND OFTEN
> 2. NO GIT HOOKS, EVER
> 3. DESCRIPTIVE NAMES, NEVER NUMERIC PREFIXES
> (acfx Constitution, Principles I-III -- `.specify/memory/constitution.md`)
> ============================================================================

# Core Primitive Taxonomy

`core/primitives/` holds reusable, RT-safe, platform-independent DSP building
blocks organized into **category subdirectories**. No loose (un-categorized)
header lives directly in this root (FR-007).

## Governing Rule: Inhabit Before Creating (FR-008, SC-006)

> A category folder is created **only** when it has a real inhabitant --
> at least one header that lives inside it. Uninhabited categories are
> documented here as **prospectus families** and are **never** materialized
> as empty directories on disk.

This means `ls core/primitives/` shows only categories that contain code.
An empty `.gitkeep` directory is explicitly forbidden (SC-006). When a new
primitive is ready to land, add the folder *in the same commit* as the
primitive itself; never pre-create it.

---

## Inhabited Categories

These folders **exist on disk** because each already contains at least one
primitive (FR-009).

### `filters/`

Frequency-selective processing kernels.

| Primitive | Description |
|---|---|
| `filters/svf-primitive.h` | State-Variable Filter -- low/band/high/notch modes, resonance-stable, parameterized by normalized cutoff and Q. Reference primitive for the three-layer structure. |

Consumers: `core/effects/svf/`, `core/effects/modulated-delay/`.
Lab: `core/labs/state-variable-filter/` (graduated state).

### `delays/`

Sample-accurate delay memory and interpolation.

| Primitive | Description |
|---|---|
| `delays/delay-line.h` | Fixed-capacity circular delay buffer with linear interpolation; no heap allocation in `process()`. |

Consumers: `core/effects/modulated-delay/`.
Tests: `tests/core/delay-line-test.cpp`.

### `modulation/`

Time-varying control signal generators.

| Primitive | Description |
|---|---|
| `modulation/lfo.h` | Low-frequency oscillator (sine/triangle/square waveforms); phase-accumulator implementation, no allocation. |

Consumers: `core/effects/modulated-delay/`.
Tests: `tests/core/lfo-test.cpp`.

---

## Prospectus Families (documented only -- no folder on disk yet)

The families below are the full intended taxonomy (FR-009). They have no
inhabitant yet, so they have no directory yet (FR-008, SC-006). When the
first primitive in a family is ready, create the folder and the primitive
together in one atomic commit.

A reviewer reading this document can determine where any planned concept
family is intended to land (SC-007).

### `nonlinear/`

Waveshapers, saturators, and distortion kernels. Intended inhabitants:
soft-clippers, hard-clippers, polynomial waveshapers, tanh approximations.

### `dynamics/`

Amplitude-envelope processors. Intended inhabitants: peak/RMS detectors,
gain computers, VCA envelopes for compressors, limiters, gates.

### `analog/`

Analog-circuit-inspired building blocks that do not rise to full circuit
simulation. Intended inhabitants: one-pole smoothers, leaky integrators,
analog-style filter prototypes.

### `circuit/`

Nodal or component-level circuit models. Intended inhabitants: transistor
stages, op-amp models, RC/LC ladder networks derived from component
simulation rather than transfer-function approximation.

### `convolution/`

Convolution engines and impulse-response utilities. Intended inhabitants:
overlap-add/overlap-save partitioned convolution blocks, IR loaders.

### `wdf/`

Wave Digital Filter primitives. Intended inhabitants: WDF adaptors
(series/parallel junctions), one-port elements (resistor, capacitor,
inductor), and composed WDF trees for passive-circuit emulation.

### `physical/`

Physical-modelling primitives. Intended inhabitants: Karplus-Strong
synthesis building blocks, waveguide delay sections, reflection functions
for string/tube/plate models.

---

## Category-Boundary Notes

`delays/` and `modulation/` are first-class categories even though they did
not appear in the original prospectus example list; that list was
illustrative, not exhaustive (spec assumption, FR-009). Their long-term
scope boundaries are flagged in the design record's open questions for
revisit when a second inhabitant arrives in each.

---

## Dependency Contract

Every header in `core/primitives/` follows the same rules as a lab kernel:

- **Allowed dependencies**: `core/dsp/` (the substrate), other primitives,
  vendored pure-DSP libs (e.g. DaisySP).
- **Forbidden**: `core/effects/**`, `core/labs/*/harness/**`, any platform
  header (JUCE, libDaisy, Teensy).
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
  primitives/  -- Reusable Primitive stage: this directory
  effects/     -- Production Effect stage: composes primitives
  dsp/         -- Shared substrate (contracts only; not one of the three layers)
```

A primitive reaches this layer by **graduating** from a lab: the kernel
header is moved into the appropriate category folder and refined in place.
The originating lab folder persists (README + harness); it never disappears
on graduation (Constitution Principle IX).

See `specs/three-layer-structure/contracts/lab-folder.md` for the full lab
lifecycle contract and `specs/three-layer-structure/spec.md` (FR-007 through
FR-012, SC-003, SC-006, SC-007) for the authoritative requirements.
