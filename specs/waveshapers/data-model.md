# Phase 1 Data Model — Waveshapers

The "entities" of a DSP primitive are its types and their state/contracts, not
persisted records. This documents each unit, its state, its invariants, and the
validation rules drawn from the requirements.

## Entity: Shape (memoryless transfer function)

- **Form**: a pure function `float shape(float u)` plus a `Shape` enum naming the
  catalog member; optionally a paired antiderivative `float shapeAntideriv(float u)`
  where one exists.
- **State**: none (memoryless — no sample rate, no history). *Invariant*: identical
  output for identical input regardless of call history.
- **Attributes**: output range, symmetry class (odd / even+odd), monotonicity, and
  documented anchor points (see research.md Decision 1).
- **Validation rules** (FR-001/002/003):
  - No DC-blocking, drive, bias, gain-comp, or anti-aliasing inside a shape.
  - Bounded output for all finite inputs (no NaN/Inf for finite `u`).
  - Asymmetric/diode/biased members produce even harmonics; symmetric members do not.
- **Antiderivative coverage** (FR-014): a shape either exposes an analytic
  antiderivative (usable by ADAA) or is flagged naive-only.

## Entity: Waveshaper (stateful wrapper)

- **State**: selected `Shape`; selected `Evaluation` backend; `drive`; `bias`;
  `gainCompensation` enabled flag (+ derived makeup factor); DC-blocker state
  (`xPrev`, `yPrev`); sample rate; optional owned LUT (when `Evaluation::lut`).
- **Operations**: `init(sampleRate)`, `setShape(Shape)`, `setEvaluation(Evaluation)`,
  `setDrive(float)`, `setBias(float)`, `setGainCompensation(bool)`, `reset()`,
  `float process(float x)`.
- **Signal chain** (FR-007): `u = drive·x + bias` → `shape(u)` (via selected backend)
  → `dcBlock` → `gainComp`.
- **Validation rules**:
  - `process()` allocates nothing and takes no lock; all work bounded (FR-020).
  - DC-blocker is owned here, never in the shape contract (FR-008).
  - `reset()` clears DC-blocker state; no stale state survives a shape switch
    beyond the documented DC-blocker state (FR-009).
  - With an asymmetric shape, steady-state output DC is within tolerance of zero
    (SC-002).
  - LUT, if selected, is built in `init()` only (FR-011).

## Entity: Evaluation backend

- **Variants**: `closedForm` (direct math) and `lut` (precomputed table +
  interpolation).
- **State** (`lut`): table samples, domain bounds, resolution; an out-of-domain
  policy (clamp to edge).
- **Validation rules** (FR-010/011/012): closed-form is the reference; max
  LUT-vs-closed-form deviation ≤ named interpolation-error bound at the stated
  resolution (SC-004); table built at `init()`, never in `process()`.

## Entity: ADAAWaveshaper (anti-aliased variant)

- **State**: an underlying shape (with its antiderivative) + the per-sample history
  `uPrev` (and `FPrev`); the same drive/bias/DC-block/gain-comp staging as
  `Waveshaper`.
- **Operations**: same surface as `Waveshaper` plus an ADAA order selector
  (first-order initially; second-order is an Open Question).
- **Validation rules** (FR-013/014):
  - Layered around the memoryless contract; does not modify `Shape` or `Waveshaper`.
  - For a covered shape and an aliasing-prone stimulus, measured inharmonic energy is
    lower than the naive shaper by ≥ named margin (SC-003).
  - For a shape with no analytic antiderivative, construction/selection raises a
    descriptive error (naive-only) — never silently mis-shapes (Constitution V).
  - The `(F(u)−F(uPrev))/(u−uPrev)` evaluation uses the documented small-denominator
    fallback to the direct midpoint to avoid the 0/0 singularity.

## Entity: Waveshaping laboratory

- **Parts**: `README.md` (theory + walkthrough naming the graduation target);
  RT-safe kernel header(s); host-only `harness/`.
- **Lifecycle** (FR-019, research.md Decision 8): pre-graduation the kernel lives in
  the lab; at graduation the kernel headers `git mv` into
  `core/primitives/nonlinear/` and the lab persists as README + harness driving the
  graduated primitive.
- **Validation rules**: nothing portable includes the harness; dependency direction
  holds; platform-independence + file-size checks pass for the new locations (FR-021/022).

## Entity: Harmonic evidence (validation output)

- **Form**: per-shape harmonic-bin magnitudes (Goertzel) from a pure-tone stimulus;
  the naive-vs-ADAA inharmonic-energy comparison; RT-safety verdicts (silence-in→out,
  DC-free, no NaN/Inf/denormal).
- **Validation rules** (FR-016/017): asserted against analytic harmonic truths +
  named tolerances; listening tests complement but never replace these (Constitution X).
- **Optional artifact**: a CSV harmonic-spectrum dump (Open Question — the standardized
  lab/harness output contract).

## State transitions

The only runtime "state machine" is the wrapper lifecycle:

```
constructed → init(sampleRate) → [ set* / process ]* → reset() → [ set* / process ]*
```

- `init` (re)builds any LUT and clears DC-blocker state.
- `set*` mutate configuration (control-thread); `process` runs on the audio thread.
- `reset` clears DC-blocker state without rebuilding the LUT.
