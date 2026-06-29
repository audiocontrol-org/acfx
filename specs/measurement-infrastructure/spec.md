> ‼ **acfx COMMANDMENTS — non-negotiable** ‼
> **1. COMMIT AND PUSH EARLY AND OFTEN** — version control is a distributed, journaled
> filesystem that safeguards your work, **NOT a sacred rite reserved for the blessed.**
> **2. NO GIT HOOKS, EVER** — this repo uses zero git hooks; none exist, none get added.
> **3. DESCRIPTIVE NAMES, NEVER NUMERIC PREFIXES** — datestamps excepted.
> (acfx Constitution, Principles I–III — `.specify/memory/constitution.md`.)

# Feature Specification: Measurement Infrastructure — host-side Measurable Engineering harness

**Feature Branch**: `main`

**Created**: 2026-06-29

**Status**: Draft

**Input**: Approved design `docs/superpowers/specs/2026-06-29-measurement-infrastructure-design.md` (Phase-1 sub-project of the Progressive Audio DSP program; roadmap node `design:feature/measurement-infrastructure`). Concrete enabler of Constitution Principle X (Measurable Engineering).

## User Scenarios & Testing *(mandatory)*

The "user" is a **DSP author/contributor** who needs to validate any acfx effect with
**objective measurements**, reusing one harness across every effect (SVF, modulated-delay,
and all future phases) instead of writing effect-specific test utilities. The harness is a
host-side **Stimulus → Effect → Analyzer → Metric** pipeline.

### User Story 1 - Measure an effect's response, effect-agnostically (Priority: P1)

The author drives any effect (anything satisfying the `Effect` contract, or any per-sample
callable) with a known stimulus, captures the output, and reduces it to objective response
metrics — frequency response (magnitude), impulse response, and phase response — asserting the
results against analytic truths and named tolerance bounds. The same calls work for the SVF
and the modulated-delay without per-effect code.

**Why this priority**: This is the MVP and the heart of the harness — the reusable
Stimulus→Effect→Analyzer→Metric pipeline. If only this ships, every effect already gains
objective, regression-gating response measurement.

**Independent Test**: Run the harness against the SVF (and a trivial known processor); confirm
magnitude/impulse/phase metrics match the analytic references within tolerance; confirm the
same code measures a second effect with no effect-specific changes.

**Acceptance Scenarios**:

1. **Given** an effect and a sine stimulus at a chosen frequency, **When** the magnitude analyzer runs, **Then** it reports the steady-state magnitude ratio matching the analytic reference within the named tolerance.
2. **Given** an effect and an impulse stimulus, **When** the impulse analyzer runs, **Then** it reports the effect's impulse response.
3. **Given** two different effects, **When** the same measurement calls are applied to each, **Then** no effect-specific measurement code is required (the harness is effect-agnostic via the `Effect` contract / a per-sample callable).
4. **Given** a known second-order filter, **When** phase response is measured, **Then** the reported phase matches the analytic reference within tolerance.

---

### User Story 2 - Characterize distortion, delay, and spectral content (Priority: P2)

The author measures harmonic distortion (THD) of an effect from a pure-tone stimulus, and the
effect's latency (processing/group delay). Harmonic content is measured with single-bin
spectral analysis (Goertzel) — no general FFT — sufficient for THD and harmonic checks on
known test tones.

**Why this priority**: THD and latency are core to validating nonlinear/dynamic and delay
effects; they build directly on the Story-1 pipeline (new analyzers + metrics).

**Independent Test**: Drive an effect with a pure tone; confirm THD is computed from the
harmonic bins and matches expectation (≈0 for a clean linear effect, elevated for a known
nonlinearity); drive an impulse/known-delay processor and confirm reported latency matches.

**Acceptance Scenarios**:

1. **Given** a pure-tone stimulus through a clean linear effect, **When** THD is measured, **Then** it reports near-zero harmonic distortion within tolerance.
2. **Given** a known nonlinearity, **When** THD is measured, **Then** the reported THD is elevated, consistent with the harmonic energy present.
3. **Given** an effect with a known processing delay, **When** latency is measured, **Then** the reported delay matches the known value within tolerance.

---

### User Story 3 - Guard real-time safety, stability, and cost (Priority: P2)

The author validates non-signal properties: numerical stability (no NaN/Inf/denormals, bounded
output under stress, **silence-in → silence-out**, **DC-offset** behavior, **denormal
generation**, **idle noise-floor**), the no-heap-allocation invariant (reusing the existing
allocation sentinel), and a **relative execution time** cost figure (a desktop-relative
host-time-per-block proxy, explicitly not absolute hardware/MCU cycles).

**Why this priority**: These properties are required by Principle X and Principle VI and
become critical once nonlinear/dynamic effects arrive; they round out the metric suite.

**Independent Test**: Run the stability checks against an effect (clean signal, silence, DC,
denormal-prone input); confirm the verdicts; confirm the allocation metric reports zero
allocations in `process()`; confirm a relative-execution-time figure is produced and labeled
as desktop-relative.

**Acceptance Scenarios**:

1. **Given** an effect fed silence, **When** the stability check runs, **Then** it confirms silence-out (no idle noise/denormal self-noise above the named floor).
2. **Given** an effect fed a DC offset and denormal-prone input, **When** the stability check runs, **Then** it confirms no NaN/Inf/denormal output and bounded behavior.
3. **Given** an effect's `process()`, **When** the allocation metric runs, **Then** it reports zero heap allocations (reusing the allocation sentinel).
4. **Given** an effect, **When** the relative-execution-time metric runs, **Then** it reports a desktop-relative cost figure clearly labeled as a proxy, not absolute cycles.

---

### User Story 4 - Optional CSV measurement report (Priority: P3)

Without affecting CI gating, the author can opt in to emit a CSV report of the measured
metrics (for regression visualization, trending, harmonic-spectrum inspection, and external
plotting). CI continues to gate solely on assertions.

**Why this priority**: A useful engineering artifact and the seam later labs reuse for
visualization (Principle IX), but secondary to the measurement capability itself.

**Independent Test**: Run a measurement with report emission enabled; confirm a well-formed
CSV of metric values is produced; confirm CI gating is unchanged (assertions only) when
emission is off.

**Acceptance Scenarios**:

1. **Given** report emission enabled, **When** measurements run, **Then** a well-formed CSV of the metric values is written.
2. **Given** report emission disabled (the default), **When** measurements run, **Then** no report is written and CI relies solely on the assertions.

---

### Edge Cases

- **Settling**: response metrics must discard a settling prefix so only steady-state behavior is measured (the existing `measureMagnitude` does this); the settle/measure lengths must be defined per metric.
- **Effect with latency vs measurement**: impulse/phase/latency measurement must account for an effect's own processing delay so it does not corrupt the metric.
- **Denormals**: stability checks must themselves not be defeated by the host's denormal handling; the check defines how denormal-prone input is constructed.
- **Sample-rate dependence**: frequencies/bins are expressed relative to the sample rate so measurements are consistent across rates.
- **Non-`Effect` callables**: the harness accepts both an `Effect` and a plain per-sample callable (the SVF test already measures a callable).
- **Block-based vs per-sample**: relative-execution-time is measured per processed block; the block size used must be recorded for the figure to be comparable.
- **Phase at low signal**: phase is undefined where the analyzed magnitude is near zero (e.g. deep stopband, or silence). Below a named magnitude floor the harness reports phase as not-a-number / skipped, never a spurious value (FR-007).
- **No false precision**: where an exact number is not analytically known, assert an analytic relationship + named tolerance, never a fabricated exact value.

## Requirements *(mandatory)*

### Functional Requirements

#### Architecture (Stimulus → Effect → Analyzer → Metric)

- **FR-001**: The harness MUST provide **stimulus generators** that produce known input signals, including at minimum impulse, step, sine, sweep, and noise; the design also captures multi-tone and MLS generators as forward-looking (not required in the first cut).
- **FR-002**: The harness MUST provide **analyzers** that reduce a captured effect output to raw analysis results, including at minimum an impulse analyzer, a single-bin spectral analyzer (Goertzel), and a correlation/delay analyzer; a general FFT analyzer is explicitly deferred to Phase 8.
- **FR-003**: The harness MUST keep **stimulus, analyzer, and metric as separated, single-purpose concepts** with clean interfaces, so a new measurement is a composition (stimulus → effect → analyzer → metric), not a bespoke utility. The boundary is explicit: **analyzers produce raw analysis data; metrics interpret that data into engineering quantities** (and assert/report it) — they are distinct types, not merged. Implementations are minimal-first (no speculative machinery ahead of a concrete need).
- **FR-004**: The harness MUST be **effect-agnostic**: it operates against any type satisfying the `Effect` contract AND any plain per-sample callable, with no effect-specific measurement code.

#### Metric suite (Principle X)

- **FR-005**: The harness MUST measure **frequency response** (magnitude vs frequency).
- **FR-006**: The harness MUST measure **impulse response**.
- **FR-007**: The harness MUST measure **phase response** (phase shift vs frequency), reported in **radians** as the **principal value (wrapped to (-π, π])** for a single-frequency measurement; unwrapping across a frequency sweep is a curve-level concern of the caller. Phase is **undefined at near-zero magnitude** — below a named magnitude floor the harness MUST report it as not-a-number / skipped rather than a spurious value (see Edge Cases). Processing latency is accounted for per the latency Edge Case.
- **FR-008**: The harness MUST measure **harmonic distortion (THD)** from a pure-tone stimulus using single-bin (Goertzel) harmonic analysis.
- **FR-009**: The harness MUST measure **latency** (processing/group delay), accounting for the effect's own delay.
- **FR-010**: The harness MUST report **relative execution time** — a desktop-relative host-time-per-block proxy — explicitly labeled as NOT absolute hardware/MCU cycles, with the block size recorded.
- **FR-011**: The harness MUST measure **memory allocation** in `process()` by reusing the existing allocation sentinel (the no-heap-allocation invariant).
- **FR-012**: The harness MUST measure **numerical stability**: no NaN/Inf/denormal output and bounded behavior under stress, INCLUDING explicit **silence-in → silence-out**, **DC-offset**, **denormal-generation**, and **idle-noise-floor** checks.

#### Output & gating

- **FR-013**: CI gating MUST rely **exclusively on doctest assertions** of metrics against analytic/reference bounds (the `svf-reference` analytic-truths + named-tolerance pattern); the harness MUST NOT fabricate exact reference numbers (no false precision).
- **FR-014**: The harness MUST support an **opt-in CSV report** of metric values (off by default); when off, gating relies solely on assertions and no report is written. When on, the CSV MUST follow the **single canonical schema** defined in `contracts/metrics.md` (effect identifier, metric, stimulus, sample rate, block size, value, units, tolerance, pass/fail) so reports are interoperable rather than ad-hoc per call site.

#### Cross-cutting constraints

- **FR-015**: The harness MUST be **host-side analysis only** — no runtime cost and no code in any `process()`/audio-callback path (Principles VI, VIII).
- **FR-016**: The harness MUST be **platform-independent** — no platform headers leak into `core/`; it lives in the host test/support layer (and `core/primitives/` only if a first-party primitive is ever introduced, which is out of scope here).
- **FR-017**: The harness MUST build on the existing `tests/support/allocation-sentinel` and the `svf-reference` measurement pattern rather than duplicating them.
- **FR-018**: Source modules MUST follow the project's strict-typing and small-module budget (no `any`-equivalent/unchecked casts; ~300–500-line files).
- **FR-019**: Scope is **measurement tooling only** — the feature MUST NOT add any new effect, MUST NOT add runtime/audio-path code, and MUST NOT introduce a general FFT (deferred to Phase 8) or any new third-party dependency.
- **FR-020**: The harness's analyzer/metric outputs MUST be structured so later laboratory exercises can reuse them for visualization (Bode plots, spectra, impulse/step responses, waterfall) without separate measurement code (Principle IX) — a reusability constraint, not lab code in this feature. **Rationale**: this is *why* the harness preserves intermediate analyzer results (raw magnitude/phase/impulse data) rather than exposing only final pass/fail verdicts — the same intermediate data a future lab needs to plot is what a test asserts against.

### Key Entities *(include if feature involves data)*

- **Stimulus generator**: produces a known input signal (impulse/step/sine/sweep/noise; multi-tone/MLS forward-looking).
- **Analyzer**: reduces a captured output to raw results (impulse / Goertzel single-bin / correlation; FFT forward-looking).
- **Metric**: a derived, reported/asserted quantity (magnitude, phase, group-delay/latency, THD, relative execution time, allocation count, stability verdict).
- **Measurement run**: a stimulus → effect → analyzer → metric composition, optionally emitting a CSV row.
- **Reference bound**: an analytic truth + named tolerance a metric is asserted against.

## Success Criteria *(mandatory)*

### Measurable Outcomes

- **SC-001**: The same measurement calls validate at least two different effects (SVF and modulated-delay, or a callable) with **zero effect-specific measurement code** (effect-agnostic, SC for FR-004).
- **SC-002**: Magnitude, impulse, and phase response are measured and asserted against analytic references within named tolerances for at least the SVF (no fabricated exact numbers).
- **SC-003**: THD reports near-zero for a clean linear effect and elevated for a known nonlinearity; latency matches a known processing delay within tolerance.
- **SC-004**: The stability check produces correct verdicts for silence-in/silence-out, DC-offset, denormal-prone, and idle cases; the allocation metric reports zero `process()` allocations; a relative-execution-time figure is produced and labeled desktop-relative.
- **SC-005**: With report emission on, a well-formed CSV of metric values is produced; with it off, no report is written and CI passes on assertions alone.
- **SC-006**: The full host-side test suite (existing + the new measurement tests) passes; no `core/` platform headers, no runtime/audio-path code, no new dependency, no general FFT introduced (verifiable from the diff and the portability gate).
- **SC-007**: All eight Principle-X metrics are represented in the harness (frequency/impulse/phase response, THD, latency, relative execution time, allocation, stability) — coverage of the captured suite.

## Assumptions

- **First-cut sequencing (non-blocking)**: not every captured generator/analyzer must ship at once; impulse/step/sine/sweep/noise generators and impulse/Goertzel/correlation analyzers are the first cut; multi-tone/MLS generators and the FFT analyzer are forward-looking. Exact ordering is a planning decision.
- **Per-metric reference bounds (non-blocking)**: the precise analytic reference + named tolerance for each metric is finalized during planning/implementation, following the `svf-reference` philosophy (analytic truths, not fabricated numbers).
- **Relative execution time is a proxy**: it is a desktop-relative host-time-per-block figure for regression/trending, not an absolute hardware or MCU measurement.
- **Spectral analysis without FFT**: THD/harmonic analysis uses Goertzel on known tones; a general FFT is intentionally deferred to Phase 8 (Convolution).
- **Host-side test layer**: the harness lives with the doctest host-side suite / `tests/support`, consistent with Principle VIII; it is not shipped in any adapter or audio path.
- **Builds on existing infra**: reuses `tests/support/allocation-sentinel` and the `svf-reference` measurement helper rather than replacing them.
