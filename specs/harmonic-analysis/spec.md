> ‼ **acfx COMMANDMENTS — non-negotiable** ‼
> **1. COMMIT AND PUSH EARLY AND OFTEN** — version control is a distributed, journaled
> filesystem that safeguards your work, **NOT a sacred rite reserved for the blessed.**
> **2. NO GIT HOOKS, EVER** — this repo uses zero git hooks; none exist, none get added.
> **3. DESCRIPTIVE NAMES, NEVER NUMERIC PREFIXES** — datestamps excepted.
> (acfx Constitution, Principles I–III — `.specify/memory/constitution.md`.)

# Feature Specification: Harmonic Analysis — Nonlinear Characterization Tooling

**Feature Branch**: `harmonic-analysis`

**Created**: 2026-07-01

**Status**: Draft

**Input**: Approved design `docs/superpowers/specs/2026-07-01-harmonic-analysis-design.md` (Phase-2 Nonlinear-DSP sub-project of the Progressive Audio DSP program; roadmap node `design:gap/harmonic-analysis`, `part-of: multi:feature/phase-nonlinear-dsp`). Closes the "measurement gap" named and deferred by the waveshapers, saturation, and oversampling designs. Concrete deepening of Constitution Principle X (Measurable Engineering).

## User Scenarios & Testing *(mandatory)*

The "user" is a **DSP author/contributor** validating and exploring nonlinear acfx effects. Today the shipped measurement infrastructure (`acfx::measure`, `tests/support/measurement/`) offers exactly one spectral method — **single-bin Goertzel on a known integer-cycle test tone** — and three labs (waveshaping, saturation, oversampling) each carry their own copy of that readout. This feature deepens harmonic characterization and gives it a live runtime face, over **one shared analysis engine** used identically by offline tests and the live workbench readout.

### User Story 1 - Deep harmonic characterization of any effect, offline (Priority: P1)

The author drives any effect (anything satisfying the `Effect` contract or a per-sample callable) with a known stimulus and reduces the captured output to a **rich harmonic picture**: a full harmonic spectrum at an arbitrary number of harmonics with per-harmonic **magnitude and phase**, harmonic rolloff, and **THD+N** (harmonics plus noise, relative to the fundamental) alongside a **noise floor / SNR** figure. Assertions use analytic truths and named tolerance bounds (the `svf-reference` pattern), and the **exact single-bin Goertzel is retained** for known-bin checks where an integer-cycle window makes it leakage-free.

**Why this priority**: This is the MVP and the heart of the offline face — the deepened analysis engine. If only this ships, every nonlinear effect immediately gains objective harmonic characterization far beyond today's fixed-6-bin Goertzel readout.

**Independent Test**: Run the engine against a known analytic nonlinearity (e.g. a symmetric shape → odd harmonics only; a biased shape → even+odd + DC) and confirm the full-spectrum magnitude/phase, THD+N, and noise-floor figures match the analytic references within named tolerances; confirm the retained Goertzel path reproduces today's exact known-bin values.

**Acceptance Scenarios**:

1. **Given** an effect and a pure-tone stimulus, **When** the full-spectrum analyzer runs, **Then** it reports per-harmonic magnitude and phase for an arbitrary requested harmonic count, matching the analytic harmonic signature within tolerance.
2. **Given** a nonlinearity driven by a pure tone plus a known noise component, **When** THD+N and noise-floor/SNR are computed, **Then** the reported figures match the analytic references within named tolerances.
3. **Given** a known integer-cycle test tone, **When** the retained single-bin Goertzel path runs, **Then** it reproduces the exact leakage-free per-bin amplitudes the current infrastructure produces (no regression).
4. **Given** two different effects, **When** the same analysis calls are applied to each, **Then** no effect-specific analysis code is required (the engine is effect-agnostic via the `Effect` contract / a per-sample callable).

---

### User Story 2 - Intermodulation and aliasing characterization (Priority: P2)

The author measures **intermodulation distortion (IMD)** from twin-tone stimuli — the **SMPTE** pair (60 Hz + 7000 Hz) and the **CCIF** pair (19 kHz + 20 kHz) — reporting difference and sum intermodulation products; and characterizes **aliasing versus frequency** by sweeping a tone and plotting inharmonic (folded) energy across the sweep, generalizing today's single-tone integer-cycle inharmonic measure.

**Why this priority**: IMD and alias-vs-frequency are exactly the measures that expose nonlinear misbehavior a single-tone THD reading misses; they build directly on the Story-1 engine (new stimuli + new reductions).

**Independent Test**: Drive a known nonlinearity with each twin-tone pair; confirm the reported difference/sum product amplitudes match analytic references within tolerance. Sweep a tone through a naive nonlinearity; confirm the alias-vs-frequency curve rises where harmonics fold past Nyquist, and is lower for a band-limited arm.

**Acceptance Scenarios**:

1. **Given** an effect and the SMPTE twin-tone stimulus, **When** the IMD analyzer runs, **Then** it reports the difference/sum intermodulation product amplitudes within named tolerance of the analytic reference.
2. **Given** an effect and the CCIF twin-tone stimulus, **When** the IMD analyzer runs, **Then** it reports the difference-frequency (1 kHz) product within tolerance.
3. **Given** a naive (non-bandlimited) nonlinearity, **When** an alias-vs-frequency sweep runs, **Then** the reported inharmonic-energy curve rises as the tone's harmonics exceed Nyquist and fold back.

---

### User Story 3 - Drive-dependent harmonic curves as a first-class series (Priority: P2)

The author sweeps an effect's **drive** (or any amplitude/character control) and obtains a **first-class series** of harmonic metrics across the sweep — drive→THD and drive→per-harmonic curves — replacing today's ad-hoc `driveThdSeries` helper open-coded in the saturation lab.

**Why this priority**: Drive-dependent behavior is the defining trait of nonlinear effects; a reusable series turns a per-lab one-off into a shared measurement any effect can request.

**Independent Test**: Sweep drive across a known nonlinearity; confirm the drive→THD series is monotonic where the analytic model predicts, and that the per-harmonic curves match analytic references at sampled drive points within tolerance.

**Acceptance Scenarios**:

1. **Given** an effect and a range of drive settings, **When** the drive→THD series runs, **Then** it returns one THD figure per drive point, matching the analytic model's monotonicity/values within tolerance.
2. **Given** an effect and a range of drive settings, **When** the drive→harmonic series runs, **Then** it returns per-harmonic amplitude curves across the drive range.

---

### User Story 4 - Consolidate duplicated harmonic tooling (Priority: P2)

The three labs' **self-contained Goertzel harmonic readouts** (waveshaping, saturation, oversampling harnesses) and the `meastest::` harmonic helpers are folded into **one shared harmonic toolkit**, so existing suites and harnesses call the toolkit instead of re-deriving the readout, and future nonlinear effects reuse rather than copy.

**Why this priority**: Consolidation is the larger half of the "measurement gap" — without it every new effect duplicates the pattern again. It is P2 because it depends on the Story-1 engine existing to consolidate onto.

**Independent Test**: Repoint the three labs' harnesses and the harmonic-signature test helpers at the shared toolkit; confirm all existing harmonic/aliasing suites remain green with no per-lab Goertzel copy left behind.

**Acceptance Scenarios**:

1. **Given** the shared toolkit exists, **When** the waveshaping, saturation, and oversampling harnesses are repointed at it, **Then** each produces the same harmonic tables/aliasing comparisons as before with no self-contained Goertzel remaining.
2. **Given** the `meastest::` harmonic helpers, **When** they are consolidated into the toolkit, **Then** existing call sites keep compiling and all harmonic-signature suites stay green.

---

### User Story 5 - Live harmonic readout in the workbench (Priority: P3)

The author plays audio through an effect in the **workbench** and sees a **live** harmonic readout — a broadband spectrum plus a running THD figure — updating while a control is swept, without any heavy analysis math running on the audio callback. The audio thread only performs a bounded, lock-free copy of samples into a ring buffer; a separate analysis thread drains the ring and runs the **same** engine the offline tests use, so a live number and a test number agree.

**Why this priority**: The runtime face answers the author's need to *see and hear* harmonic behavior live. It is P3 because it depends on the shared engine (Stories 1–2) and the RT capture boundary being in place, and it exercises a host adapter rather than core measurement.

**Independent Test**: In the workbench, run a known nonlinearity and confirm the live spectrum + running-THD readout reflects the effect's harmonic signature, and that the audio path performs only a bounded lock-free copy (no allocation, no locks, no analysis math on the callback). Confirm a live-captured metric matches the offline engine's figure for the same stimulus within tolerance.

**Acceptance Scenarios**:

1. **Given** an effect playing in the workbench, **When** the live readout runs, **Then** a broadband spectrum and a running THD figure update from the drained ring on the analysis thread while audio continues glitch-free.
2. **Given** the RT capture probe, **When** the audio callback runs, **Then** it performs only a bounded lock-free ring push — no heap allocation, no locks, no analysis math.
3. **Given** the same stimulus and effect, **When** measured live and measured offline, **Then** the two harmonic figures agree within named tolerance (one shared engine).

---

### Edge Cases

- **No fundamental / dead effect**: THD, THD+N, and SNR on silence or a signal with no measurable fundamental return the established NaN "unmeasurable" sentinel (Constitution V), never a misleading `0.0`.
- **Harmonics above Nyquist**: requested harmonics whose frequency reaches or exceeds Nyquist are reported as not-measured (established convention), not fabricated.
- **Ring overrun/underrun**: when the analysis thread cannot keep up (ring would overflow) or the ring is empty (underrun), the runtime probe drops/holds deterministically without blocking or allocating on the audio thread, and the condition is observable to the readout (no silent corruption).
- **Non-power-of-two capture length for the FFT**: the windowed radix-2 FFT requires a power-of-two length; a non-conforming capture length is handled by a defined rule (reject with a descriptive error or zero-pad to the next power of two) — resolved in clarification, never silently truncated.
- **Twin-tone products colliding with a harmonic bin**: IMD product bins that coincide with a harmonic of either tone are reported unambiguously (documented attribution), not double-counted.

## Requirements *(mandatory)*

### Functional Requirements

**Offline analysis engine (host-side, `acfx::measure`)**

- **FR-001**: The engine MUST compute a **full harmonic spectrum** at an arbitrary caller-specified harmonic count, reporting **per-harmonic magnitude and phase**.
- **FR-002**: The engine MUST compute **THD+N** (total harmonic distortion plus noise, relative to the fundamental) and a **noise floor / SNR** figure.
- **FR-003**: The engine MUST compute **intermodulation distortion (IMD)** from twin-tone stimuli, supporting the **SMPTE** (60 Hz + 7000 Hz) and **CCIF** (19 kHz + 20 kHz) pairs, and reporting **difference and sum** intermodulation products.
- **FR-004**: The engine MUST compute an **aliasing-vs-frequency sweep** — inharmonic (folded) energy as a function of a swept tone's frequency — generalizing the existing single-tone integer-cycle inharmonic measure.
- **FR-005**: The engine MUST provide **drive→THD and drive→harmonic series** as first-class reductions returning one metric (or per-harmonic curve) per drive point across a caller-specified range.
- **FR-006**: The engine MUST be **effect-agnostic**, operating on any `Effect`-contract implementation or a per-sample callable, with no effect-specific analysis code.
- **FR-007**: The engine MUST retain the **exact single-bin Goertzel** path for known-bin regression tests, preserving today's leakage-free per-bin values (no regression to existing suites).
- **FR-008**: The engine MUST report **unmeasurable** quantities (no fundamental; harmonics above Nyquist) using the established NaN sentinel convention rather than a misleading `0.0`.

**Spectral engine (hybrid — resolved in design)**

- **FR-009**: A **windowed radix-2 FFT** MUST exist in the host-side/off-thread analysis layer, serving the broadband magnitude+phase spectrum, THD+N, IMD, and the live display. It MUST NOT run on any audio-callback path.
- **FR-010**: The FFT and the retained Goertzel MUST **coexist**: FFT for breadth (unknown/broadband content), Goertzel for exact known-bin regression checks. FFT MUST NOT replace Goertzel in the known-bin path.

**RT capture probe (portable core, `core/primitives/analysis/`)**

- **FR-011**: A new `core/primitives/analysis/` category MUST provide an **RT-safe capture probe**: a lock-free single-producer/single-consumer ring buffer whose audio-path operation is a **bounded `push(block)` only** — no heap allocation, no locks, no analysis math in the audio callback.
- **FR-012**: The capture probe MUST be **platform-independent and embeddable** (no host-only or platform headers), consistent with the portable-core rule; the host-only analysis engine MUST NOT be reachable from it.
- **FR-013**: The ring buffer MUST behave **deterministically under overrun/underrun** (drop/hold without blocking or allocating on the audio thread), and the condition MUST be observable to the consumer.

**Live runtime readout (host adapter, `adapters/workbench/`)**

- **FR-014**: The workbench MUST provide a **live readout** — a broadband spectrum plus a running THD figure — computed on a separate analysis/UI thread that drains the capture-probe ring and runs the **same** offline analysis engine.
- **FR-015**: A metric measured **live** and the **same** metric measured **offline** for an identical stimulus/effect MUST agree within a named tolerance (single-engine guarantee).
- **FR-016**: The live readout MUST be **desktop-host only**; it MUST NOT be pulled into the embedded audio path.

**Cross-cutting**

- **FR-017**: The optional CSV **report** surface (`report.h`, opt-in, assertions-not-gated-on-it) MUST be extendable to the new metrics; CI gates rely exclusively on assertions vs analytic/reference bounds (the shipped convention).
- **FR-018**: This feature MUST **measure** aliasing only; it MUST NOT perform anti-aliasing/oversampling (the oversampling sibling's charter) and MUST NOT build convolution (Phase 8's charter). The introduced FFT is a forward seam Phase 8 reuses/supersedes.
- **FR-019**: An **amendment note** MUST be written back to `docs/superpowers/specs/2026-06-29-measurement-infrastructure-design.md` recording that harmonic-analysis introduces an off-thread FFT in Phase 2, amending that design's Decision A (general FFT deferred to Phase 8) for this scope.
- **FR-020**: The portability gate `scripts/check-portability.sh` (CI, never a git hook) MUST learn `core/primitives/analysis/**` so its harness-isolation, dependency-direction, platform-independence, and file-size checks cover the new category.
- **FR-021**: All new units MUST honor Constitution Principles VI (RT safety on the audio path), IV (platform-independent core / thin adapters), VII (strict typing, ~300–500-line modules, no unchecked casts), X (measurable engineering), and XI (one concept at a time).

*Deliberately open — routed to `/speckit-clarify`, captured not cut (design Open Questions §2–§6):*

- **FR-022**: The engine MUST apply a defined **window function and default transform size(s)** for the FFT-based analyses. [NEEDS CLARIFICATION: window (Hann vs Blackman-Harris vs selectable) and default size(s) trading resolution against latency/update-rate — design Open Question §2/§4]
- **FR-023**: **THD+N** MUST use a defined notion of "noise" and SNR reference. [NEEDS CLARIFICATION: "noise" = all energy not at a harmonic bin vs a modeled noise floor; and the SNR reference level — design Open Question §5]
- **FR-024**: The live readout's adapter reach MUST be defined. [NEEDS CLARIFICATION: workbench-only in this item, or also the `plugin` adapter — design Open Question §3]

### Key Entities

- **Harmonic spectrum**: per-harmonic magnitude and phase at integer multiples of a fundamental, arbitrary harmonic count; out-of-band harmonics flagged not-measured.
- **THD+N / noise-floor / SNR figures**: scalar distortion+noise metrics with the unmeasurable-NaN convention.
- **IMD result**: difference/sum intermodulation product amplitudes for a twin-tone pair (SMPTE / CCIF).
- **Alias-vs-frequency curve**: inharmonic-energy series indexed by swept tone frequency.
- **Drive-series**: THD (or per-harmonic) metric indexed by drive/control value.
- **Capture-probe ring**: lock-free SPSC sample buffer bridging the audio thread and the analysis thread; carries overrun/underrun state.
- **Analysis engine**: the single host-side reduction library (FFT + retained Goertzel + metrics) shared by offline tests and the live readout.

## Success Criteria *(mandatory)*

### Measurable Outcomes

- **SC-001**: A DSP author can obtain a full harmonic spectrum (magnitude + phase, arbitrary N), THD+N, noise floor/SNR, IMD (SMPTE and CCIF), an alias-vs-frequency sweep, and a drive-series for any `Effect`/callable **without writing effect-specific analysis code**.
- **SC-002**: Every new offline analysis is validated against an **analytic reference within a named tolerance** (no metric ships asserting against itself), and the retained Goertzel path reproduces existing known-bin values with **zero regression** in current harmonic/aliasing suites.
- **SC-003**: The three labs (waveshaping, saturation, oversampling) contain **zero self-contained Goertzel readouts** after consolidation; all harmonic readout flows through the shared toolkit and all previously green suites remain green.
- **SC-004**: The audio callback in the runtime path performs **only a bounded lock-free ring push** — verified to allocate nothing, take no locks, and run no analysis math — while the live workbench readout updates without audio glitches.
- **SC-005**: A metric measured live in the workbench and offline in a test for the same stimulus/effect **agree within the named tolerance**, demonstrating the single-engine guarantee.
- **SC-006**: The portability gate passes with `core/primitives/analysis/**` covered, and no host-only analysis code is reachable from portable core.

## Assumptions

- The shipped measurement infrastructure (`acfx::measure`: `stimulus.h`, `analyzers.h`, `metrics.h`, `aliasing.h`, `report.h`) and the `meastest::` helpers are the base this feature extends and consolidates; the `Effect` contract and per-sample-callable capture seam are reused as-is.
- The spectral-engine choice is **decided** (hybrid FFT + retained Goertzel, per the approved design); it is not re-opened in clarification. What remains open is window/size, THD+N noise definition, and live-readout adapter reach (FR-022–FR-024).
- The runtime probe uses the standard live-analyzer pattern (audio thread copies; analysis off-thread) fixed by the design; the in-`process()` alternative is rejected and not revisited.
- The workbench is the desktop host adapter that hosts the live readout; embedded targets (Daisy/Teensy) receive only the portable capture probe, never the analysis engine.
- CI enforcement is via explicit commands and the portability gate, **never git hooks** (acfx Commandment II); the CSV report stays opt-in and non-gating.

## Dependencies

- **Roadmap**: `design:gap/harmonic-analysis` — `depends-on: multi:feature/phase-digital-fundamentals`; `part-of: multi:feature/phase-nonlinear-dsp`. Design approved; pointer set via `stackctl workflow link-design`.
- **Reused**: `design:feature/measurement-infrastructure` (shipped) and its headers; the waveshaping/saturation/oversampling labs whose harmonic readouts are consolidated.
- **Forward seam**: Phase 8 (Convolution) reuses/supersedes the FFT introduced here — not a dependency of this feature, a note for that one.
