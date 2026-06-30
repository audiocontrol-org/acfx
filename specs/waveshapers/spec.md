> ‼ **acfx COMMANDMENTS — non-negotiable** ‼
> **1. COMMIT AND PUSH EARLY AND OFTEN** — version control is a distributed, journaled
> filesystem that safeguards your work, **NOT a sacred rite reserved for the blessed.**
> Small atomic commits, pushed promptly; never hoard unpushed work.
> **2. NO GIT HOOKS, EVER** — this repo uses zero git hooks; none exist, none get added.
> **3. DESCRIPTIVE NAMES, NEVER NUMERIC PREFIXES** — names carry information; fake sequence
> numbers (`001-`) imply false order and false precision (datestamps excepted).
> (acfx Constitution, Principles I–III — `.specify/memory/constitution.md`.)

# Feature Specification: Waveshapers — Nonlinear Memoryless Primitive

**Feature Branch**: `phase-nonlinear-dsp`

**Created**: 2026-06-30

**Status**: Draft

**Roadmap item**: `design:primitive/waveshapers` (`part-of: multi:feature/phase-nonlinear-dsp`)

**Design record**: `docs/superpowers/specs/2026-06-30-waveshapers-design.md` (operator-approved; source of truth)

**Input**: User description: "Waveshapers — the first nonlinear primitive of phase-nonlinear-dsp, authored from the operator-approved design record. Capture everything; do not cut scope."

## User Scenarios & Testing *(mandatory)*

### User Story 1 - Apply a memoryless transfer function to audio (Priority: P1)

An effect author selects a waveshaping transfer function (e.g. `tanh`, hard-clip, cubic
soft-clip) and runs an audio signal through it, with control over input drive, asymmetry
bias, and output gain. The shaped signal exhibits the expected harmonic character without a
DC offset leaking downstream.

**Why this priority**: This is the irreducible core of the feature — a working memoryless
nonlinearity with gain-staging. Every other story builds on it; on its own it already
delivers a usable nonlinear primitive that downstream effects (saturation, distortion) can
compose.

**Independent Test**: Drive a known sine tone through the wrapper at a fixed drive/bias and
assert, via the existing harmonic (THD) measurement, that the produced harmonic series
matches the analytic prediction for that transfer function within a named tolerance, and that
the output is DC-free.

**Acceptance Scenarios**:

1. **Given** a wrapper configured with a symmetric soft-saturating shape, a unit-amplitude
   sine stimulus, and unity drive/zero bias, **When** the signal is processed, **Then** the
   measured harmonic series contains only odd harmonics in the analytically-predicted ratios
   (within the named tolerance) and the output mean (DC) is within tolerance of zero.
2. **Given** the same wrapper with a nonzero asymmetry bias, **When** the signal is processed,
   **Then** even harmonics appear in the predicted ratios AND the DC-blocker keeps the output
   mean within tolerance of zero.
3. **Given** a low-amplitude signal well inside the shape's linear region, **When** processed
   with gain-compensation enabled, **Then** the output level is restored toward unity within
   the named tolerance.
4. **Given** silence (all-zero input), **When** processed, **Then** the output is silence
   (within tolerance), with no denormal or NaN/Inf generation.

---

### User Story 2 - Select any transfer function from a documented catalog (Priority: P1)

The catalog of memoryless transfer functions is available as pure, independently testable
functions and is runtime-selectable on the wrapper, so an author can switch shaping character
(a "character" parameter) without rebuilding.

**Why this priority**: The breadth of the catalog is the substance of "waveshapers." Pure
functions make each shape individually verifiable and teachable; runtime selection is what
downstream effects need. Co-equal P1 with Story 1 because the wrapper is only meaningful
against a catalog.

**Independent Test**: For each catalogued shape, call the pure transfer function across a
swept input domain and assert it matches its closed-form definition (monotonicity, range,
symmetry/asymmetry, and key analytic points) within tolerance — independently of the wrapper.

**Acceptance Scenarios**:

1. **Given** the catalog of pure transfer functions, **When** each is evaluated across the
   input domain, **Then** each matches its closed-form definition (output range, symmetry
   class, and named anchor points) within the named tolerance.
2. **Given** the wrapper set to one shape and then switched to another at runtime, **When**
   the signal is processed after each selection, **Then** the output reflects the currently
   selected shape with no residual state from the previous shape.
3. **Given** an asymmetric/biased shape and a memoryless diode-style curve, **When** each is
   evaluated, **Then** both produce the even-harmonic content characteristic of asymmetry,
   and the diode-style curve is documented as a transfer-function approximation distinct from
   a circuit-solved diode model.

---

### User Story 3 - Choose between exact and table-based evaluation (Priority: P2)

An author targeting a CPU-constrained platform selects a lookup-table (LUT) evaluation backend
instead of the exact closed-form one, trading a bounded, known interpolation error for uniform
per-sample cost. On a desktop target they use the exact closed-form backend as the reference.

**Why this priority**: The MCU targets are a stated, present constraint, so both backends are
in scope. It is P2 because the closed-form backend alone already satisfies Story 1; the LUT is
an additional, separately-testable evaluation path measured against the closed-form reference.

**Independent Test**: For a given shape, evaluate both backends across the input domain and
assert the LUT output deviates from the closed-form output (the ground-truth reference) by no
more than the named interpolation-error bound for the configured table resolution.

**Acceptance Scenarios**:

1. **Given** a shape evaluated with the closed-form backend and with the LUT backend at a
   stated table resolution, **When** both are swept across the input domain, **Then** the
   maximum LUT-vs-closed-form deviation is within the named interpolation-error bound.
2. **Given** the LUT backend, **When** the wrapper is prepared (`init`), **Then** the table is
   built at preparation time and no table allocation or rebuild occurs during per-sample
   processing.

---

### User Story 4 - Reduce aliasing with an opt-in anti-aliased variant (Priority: P2)

An author who hears aliasing from hard nonlinear shaping opts into an antiderivative
anti-aliasing (ADAA) variant, which reduces aliased energy relative to the naive memoryless
shaper, without changing the base memoryless transfer-function contract.

**Why this priority**: Anti-aliasing materially improves audible quality for aggressive
shapes, and ADAA is the in-primitive option (oversampling is a separate sibling item). P2
because the naive memoryless path is independently complete and the ADAA variant is strictly
additive/optional.

**Independent Test**: Process a high-frequency tone (whose harmonics exceed Nyquist) through
the naive shaper and through the ADAA variant; assert the ADAA variant exhibits measurably
lower aliased (inharmonic) energy than the naive shaper, by at least a named margin.

**Acceptance Scenarios**:

1. **Given** a high-frequency stimulus that drives a shape into aliasing, **When** processed by
   the naive memoryless shaper versus the ADAA variant, **Then** the ADAA variant shows lower
   inharmonic/aliased energy by at least the named margin.
2. **Given** the ADAA variant, **When** inspected, **Then** the base memoryless transfer
   functions and the base wrapper contract are unchanged — ADAA is layered around them, not
   folded into them.

---

### User Story 5 - Learn the concept from the waveshaping laboratory (Priority: P2)

A learner opens the `waveshaping` laboratory and finds the theory + walkthrough, the RT-safe
kernel, and a host-only harness that produces objective harmonic evidence for the shapes — the
first concept to walk the Theory→Lab→Primitive pattern from scratch, then graduate its kernel
into the reusable primitive layer.

**Why this priority**: The progressive-curriculum mission (Constitution Principle IX) requires
the lab + graduation, and this is the first greenfield exercise of that pattern. P2 because
the primitive can be validated before the lab prose is final, but graduation is the proof the
pattern works forward.

**Independent Test**: From the lab harness alone, regenerate the per-shape harmonic evidence
(harmonic signatures and the naive-vs-ADAA aliasing comparison) and confirm the kernel it
drives is the same code that, post-graduation, lives in the primitive layer.

**Acceptance Scenarios**:

1. **Given** the `waveshaping` lab, **When** its harness is run host-side, **Then** it emits
   per-shape harmonic evidence and a naive-vs-ADAA aliasing comparison using the shared
   measurement infrastructure.
2. **Given** the lab kernel, **When** the concept graduates, **Then** the kernel is relocated
   (not re-derived) into the nonlinear primitive category and the lab persists as theory +
   harness now driving the graduated primitive.
3. **Given** the portability gate, **When** it runs in CI, **Then** it confirms no portable
   code includes a lab harness, the dependency direction holds, and the new lab/primitive
   locations meet the platform-independence and file-size checks.

---

### Edge Cases

- **Extreme drive** pushing the input far outside the nominal range: output stays bounded
  (no NaN/Inf), and clipping/fold behavior matches the selected shape's definition.
- **DC / near-DC input** with an asymmetric shape: the DC-blocker removes the offset; very-low
  frequency content interaction with the blocker cutoff is bounded and documented.
- **Denormal-prone decays** (signal trailing to silence): no denormal stalls; output settles to
  clean silence.
- **Shape switch mid-stream**: no audible discontinuity beyond the inherent shape difference;
  no stale state carried from the prior shape (the memoryless core has none; the wrapper's
  DC-blocker state is the only carried state and is documented).
- **LUT domain edges**: input beyond the table's modelled domain is handled by a defined,
  bounded policy (no out-of-bounds read), matching the closed-form behavior at the edges within
  tolerance.
- **Wavefolder high fold counts**: bounded output and defined harmonic behavior at large fold
  depths.
- **Shapes without a clean antiderivative** (relevant to the ADAA variant): the variant's
  coverage is explicit; a shape lacking an analytic antiderivative is documented as
  naive-only rather than silently mis-shaped.

## Requirements *(mandatory)*

### Functional Requirements

**Memoryless core (the fixed shape contract)**

- **FR-001**: The system MUST provide a catalog of **memoryless transfer functions** as pure,
  stateless operations that map one input sample to one output sample with no sample-rate
  dependence, no history, and no DC-blocking.
- **FR-002**: The memoryless transfer-function contract MUST be fixed: drive, bias,
  gain-compensation, DC-blocking, and anti-aliasing MUST NOT be part of it.
- **FR-003**: The catalog MUST include, captured in full (sequencing of a first implemented
  cut is a planning decision, not a scope cut): symmetric soft saturators (hyperbolic-tangent,
  arctangent, cubic soft-clip, algebraic `x/√(1+x²)`); a hard-clip and a polynomial soft-knee;
  Chebyshev-style harmonic generators (targeting a chosen harmonic); asymmetric/biased curves
  (even-harmonic); a memoryless diode-style curve; and wavefolders (sine-fold and
  triangle-fold).
- **FR-004**: The memoryless diode-style curve MUST be documented as a transfer-function
  approximation at a distinct altitude from the circuit-solved diode clipper owned by the
  later circuit-modeling phase, so the two do not read as duplicates.

**Stateful wrapper (gain-staging + selection)**

- **FR-005**: The system MUST provide a runtime shape-selectable wrapper that applies a selected
  catalog shape to an audio signal.
- **FR-006**: The wrapper MUST expose controls for input **drive** (pre-gain), **bias**
  (asymmetry offset), and an optional **gain-compensation** (output makeup toward unity), plus
  preparation (`init` with sample rate), `reset`, and per-sample processing.
- **FR-007**: The wrapper's signal chain MUST be: scale the input by drive, then add bias as a
  **fixed offset applied after drive** (a constant asymmetry point independent of drive), then
  apply the selected memoryless shape, then apply a **DC-blocker**, then apply optional
  gain-compensation.
- **FR-008**: The **DC-blocker** MUST be a member of the wrapper, never of the memoryless
  transfer-function contract.
- **FR-009**: The wrapper MUST carry no stale state across a shape switch beyond the documented
  DC-blocker state, and `reset` MUST clear that state.

**Evaluation backends**

- **FR-010**: The wrapper MUST support two selectable evaluation backends: an exact
  **closed-form** backend and a **lookup-table (LUT)** backend (precomputed table with
  interpolation).
- **FR-011**: Any LUT MUST be built at preparation time; no table allocation or rebuild may
  occur on the per-sample processing path.
- **FR-012**: **Closed-form evaluation MUST be the ground-truth reference** against which LUT
  interpolation error is measured; the table resolution and the resulting interpolation-error
  bound MUST be validated quantities asserted against named tolerances.

**Anti-aliasing**

- **FR-013**: The system MUST provide an **opt-in antiderivative anti-aliasing (ADAA)**
  variant (first/second-order) as a **separate, stateful wrapper layered around** the
  memoryless contract — it MUST NOT modify the memoryless transfer-function contract or the
  base wrapper.
- **FR-014**: The ADAA variant MUST reduce aliased (inharmonic) energy relative to the naive
  memoryless shaper for stimuli that drive a shape into aliasing, by at least a named margin,
  for the shapes it covers; shapes it does not cover MUST be documented as naive-only.
- **FR-015**: This feature MUST NOT build a reusable oversampling primitive; oversampling is a
  separate sibling item and remains the orthogonal "wrap any callable" anti-aliasing layer.

**Validation / measurement**

- **FR-016**: Each catalog shape MUST be validated by **objective harmonic measurement**
  (harmonic distortion / harmonic signatures from a pure-tone stimulus) asserted against
  analytic harmonic truths and named tolerances, reusing the shipped measurement
  infrastructure (single-bin harmonic analysis + sine stimulus) — listening tests complement
  but never replace measurements.
- **FR-017**: The validation suite MUST assert real-time-safety invariants on the processing
  path: silence-in→silence-out, no DC offset reaching the output for asymmetric shapes, and no
  NaN/Inf/denormal generation under stress.
- **FR-018**: The harness's promised anti-aliasing comparison is **naive-vs-ADAA**. An
  **oversampled comparison arm is contingent**, not a promised deliverable — included only if
  the oversampling sibling has landed, or via a throwaway, explicitly non-reusable,
  non-graduated in-harness resampler.

**Layering, portability, real-time safety**

- **FR-019**: The concept MUST be authored as a `waveshaping` laboratory (theory + walkthrough
  README naming the graduation target; an RT-safe kernel held to the primitive bar; a
  host-only harness) and then **graduated** by relocating the kernel (not re-deriving it) into
  the nonlinear primitive category.
- **FR-020**: No code on the per-sample processing path may allocate heap memory or take locks;
  all per-sample work MUST be bounded.
- **FR-021**: The portable kernel and primitive MUST contain no platform-specific (desktop or
  embedded host framework) headers and MUST be compilable for the embedded targets; host-only
  harness/visualization code MUST never be included by portable code.
- **FR-022**: Enforcement MUST extend the existing explicit portability check (run in CI, never
  a git hook) to cover the new lab and primitive locations for harness-isolation,
  dependency-direction, platform-independence, and module-size — consistent with the
  three-layer-structure precedent.
- **FR-023**: Source modules MUST stay within the project's small-module size guidance
  (~300–500 lines); the catalog functions, the wrapper, the ADAA variant, and LUT support are
  organized as separate units.

### Key Entities

- **Transfer function (shape)**: a pure, memoryless input→output mapping with a defined output
  range and symmetry class; the unit the lab teaches and the suite unit-tests.
- **Waveshaper wrapper**: a stateful, runtime shape-selectable processor owning drive, bias,
  gain-compensation, DC-blocker, and evaluation-backend selection.
- **Evaluation backend**: the means of computing a shape — exact closed-form, or table-based
  with a resolution and a bounded interpolation error.
- **ADAA variant**: an opt-in stateful anti-aliased processor layered around a shape's
  antiderivative.
- **Waveshaping laboratory**: theory + walkthrough + RT-safe kernel + host-only harness; the
  origin of the graduated primitive.
- **Harmonic evidence**: per-shape harmonic signatures and the naive-vs-ADAA aliasing
  comparison, derived from the shared measurement infrastructure.

## Success Criteria *(mandatory)*

### Measurable Outcomes

- **SC-001**: Every catalogued transfer function's harmonic signature matches its analytic
  prediction within the named per-shape tolerance for a standard pure-tone stimulus.
- **SC-002**: For every asymmetric/biased shape, the output DC offset is held within the named
  tolerance of zero by the wrapper's DC-blocker.
- **SC-003**: For at least one aggressive (aliasing-prone) shape, the ADAA variant reduces
  measured aliased/inharmonic energy by at least the named margin versus the naive shaper.
- **SC-004**: LUT evaluation deviates from closed-form (the reference) by no more than the
  named interpolation-error bound at the stated table resolution.
- **SC-005**: The processing path performs zero heap allocations and holds no locks under the
  measurement suite's allocation/real-time checks, and generates no NaN/Inf/denormal under
  stress (including silence-in→silence-out and DC-input cases).
- **SC-006**: The portable kernel and primitive compile for the embedded target toolchain(s)
  with no platform-framework headers, and the portability check passes for the new lab and
  primitive locations.
- **SC-007**: The waveshaping lab's harness regenerates the per-shape harmonic evidence
  host-side, and the graduated primitive is the relocated lab kernel (verified by the
  portability/graduation checks), proving the Theory→Lab→Primitive pattern forward.

## Assumptions

- The shipped measurement infrastructure (single-bin/Goertzel harmonic analysis, sine
  stimulus, allocation sentinel, analytic-bound assertion pattern) is reused as-is; this
  feature adds no new general spectral engine (a full FFT remains a later-phase concern).
- "Named tolerance / named margin / named bound" means an explicit analytic threshold chosen
  per shape during planning/clarification (the existing reference-bound philosophy), not a
  fabricated exact figure.
- Embedded-target compilation is validated to the extent the existing build/CI exercises those
  toolchains; full on-hardware measurement is a separate, later concern.
- The first implemented cut of the catalog (which shapes land first) is a **planning** decision;
  the spec captures the full catalog without cutting it.
- Oversampling, the circuit-solved diode clipper, deeper nonlinear-specific harmonic-analysis
  tooling, and the composed saturation effect are **out of scope** here (separate roadmap
  items), per one-concept-at-a-time.

## Open Questions *(carried from the design record — captured, not blockers)*

- First implemented cut of the catalog (planning/sequencing).
- ADAA order (first vs second) and which shapes get an analytic antiderivative (some — e.g.
  wavefolders — have awkward or piecewise antiderivatives).
- LUT resolution and interpolation scheme per shape; which shapes actually benefit from a LUT
  versus are already cheap closed-form.
- Gain-compensation law (peak-normalize, RMS-match at a reference level, or analytic
  unity-at-low-signal) and whether it is per-shape.
- DC-block cutoff: fixed value versus a parameter.
- Wavefolder parameterization (fold count / fold gain) and its mapping to the drive control.
- Lab/harness standardized output contract (e.g. CSV harmonic spectra) so nonlinear labs are
  comparable — shared with the open question already recorded for the lab layer.
