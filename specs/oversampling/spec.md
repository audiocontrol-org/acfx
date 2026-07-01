> ‼ **acfx COMMANDMENTS — non-negotiable** ‼
> **1. COMMIT AND PUSH EARLY AND OFTEN** — version control is a distributed, journaled
> filesystem that safeguards your work, **NOT a sacred rite reserved for the blessed.**
> Small atomic commits, pushed promptly; never hoard unpushed work.
> **2. NO GIT HOOKS, EVER** — this repo uses zero git hooks; none exist, none get added.
> **3. DESCRIPTIVE NAMES, NEVER NUMERIC PREFIXES** — names carry information; fake sequence
> numbers (`001-`) imply false order and false precision (datestamps excepted).
> (acfx Constitution, Principles I–III — `.specify/memory/constitution.md`.)

# Feature Specification: Oversampling — Reusable Anti-Aliasing Primitive

**Feature Branch**: `oversampling`

**Created**: 2026-07-01

**Status**: Draft

**Roadmap item**: `design:primitive/oversampling` (`part-of: multi:feature/phase-nonlinear-dsp`, `depends-on: multi:feature/phase-digital-fundamentals`)

**Design record**: `docs/superpowers/specs/2026-07-01-oversampling-design.md` (operator-approved; source of truth)

**Input**: User description: "Oversampling primitive — a generic, reusable, RT-safe block-wrapper that upsamples an input sample, runs a caller-supplied nonlinearity at N× the rate, then band-limits and decimates back, using linear-phase polyphase halfband FIR filtering; its first client closes the reserved FR-015 saturation oversampled seam. Capture everything; do not cut scope."

## User Scenarios & Testing *(mandatory)*

### User Story 1 - Oversample a nonlinearity transparently (Priority: P1)

An effect author wraps a per-sample nonlinearity (e.g. a waveshaper) in the oversampling
primitive so it runs at a higher internal rate, obtaining an output whose aliasing is
suppressed while the passband signal is otherwise unchanged. The author supplies only the
nonlinearity; the primitive owns all rate conversion and band-limiting. With a pass-through
(identity) nonlinearity the primitive is effectively transparent — it colors the signal only by
a fixed, reported latency, not by phase distortion or audible passband change.

**Why this priority**: The irreducible core — a working, transparent oversampling wrapper that
every other story and every future nonlinear client builds on, and that already delivers value
on its own (any nonlinearity can be run anti-aliased through it).

**Independent Test**: Drive a passband sine through the primitive with an identity nonlinearity
and assert the output reproduces the input (delayed by the reported latency) within the named
passband ripple tolerance; then drive a supra-Nyquist-harmonic-producing nonlinearity and
assert, via the existing aliasing measurement, that inharmonic energy is measurably lower than
the same nonlinearity run without oversampling.

**Acceptance Scenarios**:

1. **Given** the primitive prepared for a sample rate with an identity nonlinearity, a
   unit-amplitude passband sine stimulus, **When** the signal is processed, **Then** the output
   matches the input delayed by the reported latency within the named passband ripple tolerance,
   with no NaN/Inf or denormal generation.
2. **Given** the primitive wrapping a hard-nonlinearity that produces harmonics above the base
   Nyquist, a high-fundamental sine stimulus, **When** the signal is processed, **Then** the
   measured inharmonic (aliased) energy is lower than the same nonlinearity evaluated at the base
   rate by at least the named margin.
3. **Given** all-zero (silent) input, **When** processed, **Then** the output is silence within
   tolerance, with no denormal or NaN/Inf generation.
4. **Given** a `reset()` after processing, **When** processing resumes, **Then** the primitive
   behaves as freshly prepared (filter state cleared) without discarding its configuration.

---

### User Story 2 - Choose the oversampling factor (Priority: P1)

The author selects the oversampling factor — 2×, 4×, or 8× — appropriate to the CPU/quality
budget of the target (desktop vs embedded). Each factor cascades the correct number of 2×
band-limiting stages and is independently correct: transparent in the passband, alias-suppressing,
and with a well-defined reported latency.

**Why this priority**: The factor is part of the primitive's core contract — a single fixed
factor would not serve both desktop (headroom for 4×/8×) and embedded (2×) targets. Co-equal P1:
the primitive is only meaningful across its supported factors.

**Independent Test**: For each of 2×/4×/8×, run the transparency, anti-aliasing, and
latency checks and assert each passes with that factor's expected latency and stopband.

**Acceptance Scenarios**:

1. **Given** each supported factor (2×/4×/8×) in turn, **When** the transparency and
   anti-aliasing tests are run, **Then** each factor passes both within the named tolerances.
2. **Given** a higher factor, **When** aliasing is measured on the same driven nonlinearity,
   **Then** the residual inharmonic energy is no worse than at a lower factor (more oversampling
   never increases aliasing).

---

### User Story 3 - Report processing latency (Priority: P2)

The author queries the primitive for its integer processing latency (group delay), referred to
the base sample rate, so a host can later compensate for it (plugin delay compensation) and so
composed signal chains can align wet/dry paths.

**Why this priority**: Latency reporting is required for correct integration into larger chains
and hosts, but the primitive is usable and testable without a host consuming the value — so it
ranks below the core anti-aliasing behavior.

**Independent Test**: For each factor, measure the group delay from an impulse or a known
reference and assert it equals the reported `latencySamples()` exactly.

**Acceptance Scenarios**:

1. **Given** the primitive prepared at each supported factor, **When** its reported latency is
   compared to the measured group delay, **Then** they are equal to the sample.
2. **Given** the reported latency, **When** the identity-nonlinearity output is aligned by that
   latency, **Then** it matches the input within the passband ripple tolerance (US1 scenario 1
   consistency).

---

### User Story 4 - Close the reserved saturation oversampled tier (Priority: P1)

The shipped saturation effect exposes a reserved, unwired `oversampled` quality tier (FR-015)
that currently resolves to the ADAA path. Using this primitive, the `oversampled` tier becomes a
real oversampled signal path — pre-emphasis at the base rate → the nonlinearity run oversampled →
post-de-emphasis at the base rate — and becomes user-selectable. This is the primitive's first
client and its end-to-end proof.

**Why this priority**: A primitive with no consumer is unproven. Wiring saturation exercises the
whole primitive against real, measured audio and closes a seam the shipped effect explicitly left
open. The approved design recommends shipping the primitive proven, so this is co-equal P1.

**Independent Test**: Select the saturation `oversampled` tier and assert, via the existing
saturation aliasing test, that it produces measurably lower inharmonic energy than the `naive`
tier, and that it is exposed as a selectable quality option.

**Acceptance Scenarios**:

1. **Given** the saturation effect with the `oversampled` quality tier selected, an identical
   stimulus, **When** processed, **Then** the measured inharmonic energy is lower than the
   `naive` tier by at least the named margin (no longer identical to the ADAA path).
2. **Given** the effect's quality parameter surface, **When** inspected, **Then** `oversampled`
   is a user-selectable option alongside `naive` and `adaa`.
3. **Given** runtime switching among `naive` / `adaa` / `oversampled`, **When** the quality is
   changed during processing, **Then** the switch is real-time-safe and produces no artifact
   beyond a bounded transient (no stale-state corruption, no NaN/Inf).

---

### Edge Cases

- **Sub-Nyquist-only content**: input already band-limited well below Nyquist → the primitive is
  transparent within tolerance (no spurious coloration from the filters).
- **Full-scale / clipping input into the nonlinearity**: the wrapped nonlinearity may saturate;
  the primitive must remain total (no NaN/Inf) and its filters must not overflow.
- **Impulse / discontinuity**: transient input exercises the FIR ring buffers at their edges;
  output stays bounded and the group delay is stable.
- **`reset()` mid-stream**: clears filter delay lines without discarding factor/configuration.
- **Sample-rate change via re-`init()`**: re-preparing at a new base rate updates the reported
  oversampled rate and latency consistently.
- **Caller nonlinearity that itself introduces DC**: the primitive does not silently correct it;
  DC handling remains the caller's concern (documented boundary), matching the existing waveshaper
  DC-blocker ownership.

## Requirements *(mandatory)*

### Functional Requirements

#### Core primitive

- **FR-001**: The primitive MUST provide a generic block-wrapper that, per input sample,
  upsamples to the oversampled rate, invokes a caller-supplied nonlinearity on each oversampled
  sample, then band-limits and decimates back to exactly one output sample.
- **FR-002**: The caller MUST supply the nonlinearity as a real-time-safe, `noexcept` callable
  evaluated at the oversampled rate; the primitive MUST NOT embed any specific nonlinearity.
- **FR-003**: The oversampling factor MUST be selectable among 2×, 4×, and 8× (power-of-two),
  fixed for the lifetime of a primitive instance (selected at construction/compile time).
- **FR-004**: Anti-alias (decimation) and anti-image (interpolation) filtering MUST be
  linear-phase, introducing no phase distortion across the passband.
- **FR-005**: The primitive MUST expose the effective oversampled rate (base rate × factor) so
  the caller can prepare its rate-dependent DSP at that rate.
- **FR-006**: The primitive MUST report its processing latency as an integer number of samples
  (group delay) referred to the base sample rate.
- **FR-007**: With an identity nonlinearity, the round-trip output MUST reproduce the input
  (delayed by the reported latency) within a named passband ripple tolerance.
- **FR-008**: For a nonlinearity that produces harmonics above the base Nyquist, the oversampled
  output MUST exhibit lower inharmonic (aliased) energy than the same nonlinearity evaluated at
  the base rate, by at least a named margin.
- **FR-009**: The band-limiting filters MUST achieve at least a named stopband rejection with
  passband ripple within a named bound (analytic FIR truths, asserted against tolerances in the
  `svf-reference` pattern).
- **FR-010**: A higher oversampling factor MUST NOT increase residual aliasing relative to a
  lower factor on the same stimulus (monotone-or-better).
- **FR-011**: `reset()` MUST clear all filter state without discarding the primitive's
  configuration; a subsequent `process()` MUST behave as freshly prepared.
- **FR-012**: The reported latency MUST equal the measured group delay to the sample, for every
  supported factor.

#### Real-time safety & platform independence

- **FR-013**: `process()` MUST NOT allocate heap memory or take locks; all buffers and filter
  coefficients MUST be sized/baked at construction or `prepare()` time.
- **FR-014**: All primitive state MUST be value-typed with compile-time-sized storage (no dynamic
  allocation), giving embedded targets a statically-known RAM footprint.
- **FR-015**: The primitive MUST live in `core/` and MUST NOT depend on any platform, host-
  framework, or embedded-vendor headers (Constitution IV).
- **FR-016**: The primitive MUST be total over finite input — it MUST NOT produce NaN/Inf for any
  finite input and finite, total caller nonlinearity.

#### First client — saturation oversampled tier (closes saturation FR-015)

- **FR-017**: The saturation effect's `oversampled` quality tier MUST become a real oversampled
  signal path: pre-emphasis at the base rate → the nonlinearity run through this primitive at the
  oversampled rate → post-de-emphasis at the base rate, replacing the interim ADAA mapping.
- **FR-018**: The saturation nonlinear stage used inside the oversampler MUST be prepared at the
  oversampled rate (its rate-dependent state, e.g. the DC-blocker, MUST use the oversampled rate).
- **FR-019**: `oversampled` MUST become a user-selectable option in the saturation effect's
  discrete quality parameter surface, alongside `naive` and `adaa`.
- **FR-020**: With `oversampled` selected, the saturation aliasing test MUST assert measurably
  lower inharmonic energy than the `naive` tier (replacing the prior `oversampled == adaa`
  assertion).
- **FR-021**: Switching among `naive` / `adaa` / `oversampled` at runtime MUST be real-time-safe
  and MUST NOT introduce stale-state corruption or NaN/Inf beyond a bounded transient.

#### Validation

- **FR-022**: Validation MUST reuse the shipped measurement infrastructure — the aliasing
  measure, the Goertzel/THD analyzer, the `svf-reference` analytic-tolerance pattern, and the
  allocation sentinel — rather than introducing parallel measurement tooling.
- **FR-023**: All named tolerances and margins (passband ripple, stopband rejection, aliasing
  reduction margin, latency equality) MUST be explicit, documented constants, not magic numbers.
- **FR-024**: The halfband filter coefficients MUST be reproducible and reviewable, not
  hand-waved: checked-in `constexpr` tables accompanied by EITHER a committed generator that
  emits them OR a derivation note, in both cases recording the exact design parameters
  (transition band, stopband target, passband-ripple target) used to produce them.

#### Deferred scope — captured, not cut (capture-over-YAGNI)

- **FR-025**: The following MUST be recorded as documented, deliberately-unwired future scope
  (built by neither this feature nor silently dropped; the operator's later scoping pass decides
  each): a block-processing API variant; multiple filter-quality tap-count tiers; an IIR allpass
  "fast" tier; a runtime-selectable factor bounded by a compile-time maximum; arbitrary /
  non-power-of-two resampling ratios; additional clients (diode clipper, tube/console stages);
  and plugin PDC host-latency integration (surfacing the reported latency to a DAW).

### Key Entities *(include if feature involves data)*

- **Oversampler (parameterized by factor)**: the public primitive. Owns the cascade of 2× stages,
  the effective oversampled rate, the reported latency, and the per-sample wrap-and-process
  contract. Depends on: the halfband stages and their coefficient tables; nothing platform-specific.
- **Halfband upsampler / downsampler stage**: a single 2× interpolation (1→2) or decimation (2→1)
  unit, implemented as a linear-phase polyphase halfband FIR. Depends on: a coefficient table and
  a compile-time-sized delay buffer.
- **Halfband coefficient table**: the static, design-time-computed linear-phase halfband FIR
  coefficients (with their provenance documented). Depends on: nothing at runtime (baked).
- **Saturation oversampled path (client)**: the saturation core's realization of the `oversampled`
  tier — an oversampler instance plus a nonlinear stage prepared at the oversampled rate, framed
  by the base-rate emphasis filters. Depends on: the Oversampler primitive and the existing
  waveshaper primitive.

## Success Criteria *(mandatory)*

### Measurable Outcomes

- **SC-001**: With an identity nonlinearity, round-trip output matches the input (aligned by the
  reported latency) within the named passband ripple tolerance across the audible passband, for
  every supported factor.
- **SC-002**: For a driven nonlinearity producing supra-Nyquist harmonics, the oversampled path
  reduces inharmonic (aliased) energy by at least the named margin versus the non-oversampled
  path.
- **SC-003**: The band-limiting filters meet the named stopband rejection with passband ripple
  within the named bound (verified against analytic FIR truth).
- **SC-004**: Reported latency equals measured group delay exactly (integer samples) for every
  supported factor.
- **SC-005**: `process()` performs zero heap allocations and takes no locks, verified by the
  allocation sentinel, for every supported factor.
- **SC-006**: The saturation `oversampled` tier produces measurably lower inharmonic energy than
  its `naive` tier on the existing aliasing test and is selectable from the effect's quality
  surface.
- **SC-007**: All supported factors (2×/4×/8×) pass the transparency, anti-aliasing, and latency
  checks; a higher factor never worsens residual aliasing.
- **SC-008**: The core primitive compiles and runs with no platform/host/vendor headers, keeping
  `core/` platform-independent (buildable host-side under the `test` preset).

## Assumptions

- **Saturation wiring is in scope for this feature** (US4 / FR-017…FR-021). The approved design
  recommends shipping the primitive proven end-to-end via its first client; the operator's later
  scoping pass may split it into a thin follow-up, but the default captured here includes it.
- **Default saturation factor is a planning decision, not a spec assertion**: the factor a client
  uses is a client/planning choice; the saturation client's chosen default lives in the plan
  (`research.md` Decision 7 selects **4×**) and may be revisited by a later tuning pass. The spec
  deliberately does not fix a default (FR-003 requires only that 2×/4×/8× are supported).
- **Single filter-quality tier now**: one linear-phase halfband coefficient table (one stopband/
  ripple spec) is built now; multiple tap-count quality tiers are deferred (FR-025).
- **Coefficient provenance is resolved in the plan**: FR-024 requires reproducible, reviewable
  coefficients; the plan (`research.md` Decision 5) chooses the mechanism — a committed offline
  generator emitting checked-in `constexpr` tables with the design parameters recorded inline.
- **Per-target tuning is a later pass**: exact tap counts / stopband targets per platform
  (desktop vs Daisy vs Teensy) are a tuning concern; this feature fixes named tolerances that the
  tuning pass may tighten, not a per-target matrix.
- **Measurement reuse**: the existing `tests/support/measurement/` harness and
  `tests/core/saturation-aliasing-test.cpp` conventions are sufficient; no new measurement
  framework is introduced.
- **DC handling stays with the caller**: the primitive does not add its own DC blocker; DC
  ownership remains the wrapped nonlinearity's concern (matching the waveshaper convention).

## Open Questions

- **Tuning may revisit the plan's default factor** — the plan selects 4× for the saturation
  client (`research.md` Decision 7); a later tuning pass may prefer 2× (embedded-friendly) vs 4×
  (desktop-transparent). Not a spec blocker. *(tuning pass)*
- **FIR tap counts / stopband spec per target** — one table for all targets, or desktop-vs-Daisy-
  vs-Teensy variants under the deferred quality-tier scope? *(tuning / per-target budget)*
- **Saturation-wiring scope boundary** — confirm US4/FR-017…FR-021 ship in this feature (design
  recommendation, current assumption) vs a thin follow-up item. *(operator scoping pass)*

*(Resolved during planning — was open at design time: halfband coefficient provenance is now
FR-024 + `research.md` Decision 5, a committed offline generator emitting checked-in `constexpr`
tables.)*
