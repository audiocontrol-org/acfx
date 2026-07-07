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

### `dynamics/`

Amplitude-envelope processors (peak/RMS detectors, gain computers, envelope-to-parameter modulation mappers for compressors, limiters, gates, and dynamic effects).

| Primitive | Description |
|---|---|
| `dynamics/envelope-follower.h` | Level detector — peak/RMS/peak-hold modes, branching/decoupled ballistics (smooth-capable), linear/dB detection; RT-safe, allocation-free. First inhabitant of dynamics/. |
| `dynamics/gain-computer.h` | Static gain-reduction curve — compress/limit/expand/gate modes, unified quadratic C1 knee straddling the threshold (hard corner at knee 0); stateless, RT-safe, branch-only arithmetic. Maps an externally supplied level (dB) to a gain change (dB); holds no runtime state and applies no ballistics. Second inhabitant of dynamics/. |
| `dynamics/dynamics-modulator.h` | Envelope-to-signed-offset mapper — maps a normalized envelope [0,1] to a signed parameter offset shaped by a signed depth (direction + amount) and a selectable response curve (linear/logarithmic/exponential, each anchored at (0,0)/(1,1)); stateless, RT-safe, branch-only. The modulation analog of the gain computer (offset, not gain); composed by envelope-driven effects. Third inhabitant of dynamics/. |

Consumers: `core/effects/compressor/` (composes envelope-follower + gain-computer), `core/effects/program-dependent-saturation/` (composes envelope-follower + dynamics-modulator over SaturationCore).
Labs: `core/labs/envelope-follower/`, `core/labs/compressor/`, `core/labs/program-dependent-saturation/` (each persists as README + host-only harness driving its graduated primitive).
Tests: `tests/core/envelope-follower-*.cpp`, `tests/core/gain-computer-test.cpp`, `tests/core/dynamics-modulator-test.cpp` (added by later tasks).

### `nonlinear/`

Waveshapers, saturators, and distortion kernels. Graduated from `core/labs/waveshaping/` (T024).

| Primitive | Description |
|---|---|
| `nonlinear/waveshaper-shapes.h` | Memoryless transfer-function catalog (`namespace acfx::shape`): 10 pure `float->float` shapes (tanhShape, arctanShape, cubicSoftClip, algebraic, hardClip, softKnee, chebyshev, diodeCurve, sineFold, triangleFold), their antiderivatives (one per covered shape), `Shape`/`Evaluation` enums, and the `hasAntiderivative()` predicate. `Shape::biasedAsym` is a Shape enum member realized in the wrapper (no pure `acfx::shape::biasedAsym` function exists) and has no antiderivative; `hasAntiderivative(Shape::biasedAsym)` returns false. |
| `nonlinear/waveshaper.h` | Stateful `Waveshaper` wrapper: drive/bias/DC-block/gain-comp staging around the memoryless catalog; supports `closedForm` and `lut` evaluation backends; RT-safe. |
| `nonlinear/waveshaper-lut.h` | `WaveshaperLut` fixed-size table (512 points, linear interpolation, edge-clamp); built in `init()`, never in `process()`; error bound `kMaxDeviation = 1e-3`. |
| `nonlinear/adaa-waveshaper.h` | `ADAAWaveshaper` first-order antiderivative anti-aliasing variant; refuses uncovered shapes with a descriptive error; same drive/bias/DC-block/gain-comp staging as `Waveshaper`. |
| `nonlinear/hysteresis.h` | Jiles-Atherton magnetic hysteresis with curve-state memory; selectable solver (RK2/RK4/Newton); state carries across samples; antialiasing via oversampling (ADAA not applicable); RT-safe, allocation-free. First stateful inhabitant of `nonlinear/`. |

Consumers: `core/effects/tape-dynamics/` (hysteresis model). Waveshaper family: none yet — planned for saturation/distortion effects in future phases.
Tests: `tests/core/waveshaper-test.cpp`, `waveshaper-harmonics-test.cpp`, `waveshaper-shapes-test.cpp`, `waveshaper-lut-test.cpp`, `waveshaper-adaa-test.cpp`, `waveshaper-antiderivatives-test.cpp`, `tests/core/hysteresis-test.cpp`.
Labs: `core/labs/waveshaping/`, `core/labs/tape-dynamics/` (each persists as README + harness driving its graduated primitive).

**Diode-curve altitude boundary (research.md Decision 6)**: `diodeCurve` is a memoryless transfer curve — a pure `float → float` closed form in `namespace acfx::shape`, explicitly distinct from the stateful circuit-solved diode clipper in `phase-circuit-modeling`'s `diode-clippers` item (FR-004). See `core/labs/waveshaping/README.md` for the complete altitude boundary explanation.

### `circuit/`

Electronic circuit-element abstractions — the solver-neutral typed vocabulary (components own their physics; solvers are adapters). First deliverable of Phase 4 (Circuit Modeling).

| Primitive | Description |
|---|---|
| `circuit/node.h` | NodeId node handle (ground = node 0) + validators. |
| `circuit/components.h` | `Component` = std::variant of the six element types + isLinear/isReactive/isNonlinear classifiers (heap-free, vtable-free). |
| `circuit/netlist.h` | `Netlist<MaxNodes,MaxComponents>` fixed-capacity container + prepare()-time topology validation (missing-ground / floating-node / over-capacity). |
| `circuit/models/resistor.h` | Linear resistor, admittance() = 1/R. |
| `circuit/models/capacitor.h` | Capacitor with backward-Euler companion; uses shared Companion return type (defined in `models/companion.h`). |
| `circuit/models/inductor.h` | Inductor (dual of capacitor). |
| `circuit/models/sources.h` | Ideal independent voltage + current sources. |
| `circuit/models/diode.h` | Shockley diode (nonlinear), evaluate()->{current,conductance} + pnjlim voltage limiter. |
| `circuit/tone-stack/taper.h` | Potentiometer as build-time math: `Taper{Linear,Log}`, `wiper()`/`rheostat()`, fixed 10 Ω end-resistance floor. Emits frozen-vocabulary `Resistor` legs — no new element. |
| `circuit/tone-stack/tone-stack.h` | Solver-neutral passive tone-stack builders: `toneStackFMV` (3-band FMV) and `toneStackBaxandall` (passive James 2-band) → a prepared `Netlist` (topology only; no solve, no audio path). Second deliverable of Phase 4. |
| `circuit/diode-clipper/clipper-config.h` | Diode-clipper BOM/value vocabulary: `DiodeSpec`, per-topology `SymmetricShuntValues` / `AsymmetricShuntValues` / `SeriesValues`, the `Clipper<>` return struct + per-topology capacity aliases, and fail-loud BOM validation helpers. |
| `circuit/diode-clipper/diode-clipper.h` | Solver-neutral diode-clipper builders: `symmetricShuntClipper`, `asymmetricShuntClipper`, `seriesClipper` → a prepared `Netlist` of frozen-vocabulary components + input/output/port node handles (topology only; no solve, no audio path). Third deliverable of Phase 4. |
| `circuit/models/opamp.h` | Ideal operational amplifier (nullor): nullator across inputs (imposes virtual short V(inPlus) = V(inMinus), zero input current) + norator at output (sources feedback-determined current). Realized exactly by the solver via constraint augmentation, not by a large-but-finite gain. No admittance/companion; carries no reactive state. Sanctioned single extension to the frozen circuit vocabulary (opamp-stages feature). |
| `circuit/opamp-stage/opamp-config.h` | Op-amp-stage BOM/value vocabulary: per-topology structures (`NonInvertingGainBom`, `InvertingGainBom`, `ActiveFirstOrderBom`, `OpAmpDiodeClipperBom`), the `OpAmpStageResult<>` return struct + per-topology fixed Netlist capacities, diode parameters, and fail-loud BOM validation helpers. Mirrors `circuit/diode-clipper/clipper-config.h` structure closely (sibling primitive). |
| `circuit/opamp-stage/opamp-stage.h` | Solver-neutral op-amp-stage builders: `nonInvertingGain`, `invertingGain`, `activeFirstOrder`, `opAmpDiodeClipper` → a prepared `Netlist` of frozen-vocabulary components + the OpAmp element + input/output node handles (topology only; no solve, no audio path). Fourth deliverable of Phase 4. |

Consumers: (Phase 4 deliverables consume the vocabulary; `circuit/tone-stack/` is the first, `circuit/diode-clipper/` the third, `circuit/opamp-stage/` the fourth).
Lab: `core/labs/component-abstractions/` (reference solver + harness); `core/labs/passive-tone-stacks/` (complex `.ac` solver + harness); `core/labs/diode-clippers/` (bounded transient nonlinear solver + harness); `core/labs/opamp-stages/` (op-amp-stage builder + harness).
Tests: `tests/core/circuit-components-test.cpp`, `circuit-netlist-test.cpp`, `circuit-solver-test.cpp`, `tone-stack-taper-test.cpp`, `tone-stack-builder-test.cpp`, `tone-stack-ac-test.cpp`, `diode-clipper-builder-test.cpp`, `diode-clipper-transient-test.cpp`, `opamp-stage-builder-test.cpp`, `opamp-stage-solve-test.cpp`, `opamp-stage-invariants-test.cpp`.

---

## Prospectus Families (documented only -- no folder on disk yet)

The families below are the full intended taxonomy (FR-009). They have no
inhabitant yet, so they have no directory yet (FR-008, SC-006). When the
first primitive in a family is ready, create the folder and the primitive
together in one atomic commit.

A reviewer reading this document can determine where any planned concept
family is intended to land (SC-007).

### `analog/`

Analog-circuit-inspired building blocks that do not rise to full circuit
simulation. Intended inhabitants: one-pole smoothers, leaky integrators,
analog-style filter prototypes.

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
