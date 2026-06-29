# Measurement-Correctness Requirements Checklist: Measurement Infrastructure

**Purpose**: Unit-test the *requirements* (spec.md) for the measurement harness — complete,
clear, consistent, measurable? Interrogates the requirements, not the implementation.
**Created**: 2026-06-29
**Feature**: [spec.md](../spec.md)

## Architecture & effect-agnosticism

- [ ] CHK001 Is the Stimulus→Effect→Analyzer→Metric separation specified as three single-purpose concepts, not one blob? [Clarity, Spec §FR-003]
- [ ] CHK002 Is "effect-agnostic" defined concretely — works against the Effect contract AND a per-sample callable, with no effect-specific code? [Clarity, Spec §FR-004, SC-001]
- [ ] CHK003 Is the first-cut vs forward-looking split (generators/analyzers) stated so scope is unambiguous? [Completeness, Spec §FR-001/002, Assumptions]
- [ ] CHK004 Is the minimal-first / anti-over-abstraction constraint captured as a requirement, not just design prose? [Clarity, Spec §FR-003]

## Metric coverage (Principle X)

- [ ] CHK005 Is every one of the eight metrics named with a requirement (freq/impulse/phase response, THD, latency, relative-exec-time, allocation, stability)? [Completeness, Spec §FR-005–012, SC-007]
- [ ] CHK006 Is THD's method (Goertzel harmonic bins from a pure tone) specified, and its expected behavior (≈0 linear, elevated nonlinear) measurable? [Measurability, Spec §FR-008, SC-003]
- [ ] CHK007 Is latency defined as accounting for the effect's own delay (not just raw output offset)? [Clarity, Spec §FR-009, Edge Cases]
- [ ] CHK008 Is "relative execution time" disambiguated as a desktop-relative proxy (NOT absolute hardware/MCU cycles) with block size recorded? [Ambiguity, Spec §FR-010]
- [ ] CHK009 Is the allocation metric specified as reusing the existing sentinel rather than a new mechanism? [Consistency, Spec §FR-011, §FR-017]
- [ ] CHK010 Are the stability sub-cases enumerated (silence-in→silence-out, DC-offset, denormal, idle-noise-floor) plus the NaN/Inf/denormal+bounds scan? [Completeness, Spec §FR-012]
- [ ] CHK011 Is "idle noise-floor" given a measurable threshold notion rather than left qualitative? [Measurability, Spec §FR-012, Edge Cases]

## No false precision / output / gating

- [ ] CHK012 Is the no-false-precision discipline (analytic truths + named tolerances, not fabricated exact numbers) stated as a requirement? [Clarity, Spec §FR-013]
- [ ] CHK013 Is CI gating specified as assertions-only, with the CSV report explicitly opt-in / off-by-default? [Consistency, Spec §FR-014, SC-005]
- [ ] CHK014 Is the CSV report's "well-formed" expectation defined enough to verify (header + one row per run)? [Measurability, Spec §FR-014, contracts/metrics]

## Scope boundaries

- [ ] CHK015 Is host-side-only / no-runtime / no-audio-path-code stated unambiguously? [Clarity, Spec §FR-015]
- [ ] CHK016 Is platform-independence (no platform headers in core/) stated, given the harness lives in tests/support? [Consistency, Spec §FR-016]
- [ ] CHK017 Are the explicit exclusions stated as checkable negatives — no new effect, no new dependency, no general FFT (deferred to Phase 8)? [Clarity, Spec §FR-019, SC-006]
- [ ] CHK018 Is the Phase-IX educational-reuse expectation captured as a (forward-looking) reusability constraint, not lab code in this feature? [Coverage, Spec §FR-020]

## Edge cases & assumptions

- [ ] CHK019 Are settling-prefix and steady-state requirements specified for the response metrics? [Completeness, Spec Edge Cases]
- [ ] CHK020 Is sample-rate-relative frequency/bin specification stated so measurements are rate-consistent? [Coverage, Spec Edge Cases]
- [ ] CHK021 Are the non-blocking residuals (first-cut sequencing; per-metric reference bounds) recorded as assumptions, not left as silent gaps? [Assumption, Spec Assumptions]

## Acceptance & verifiability

- [ ] CHK022 Can each Success Criterion (SC-001..007) be verified from a doctest assertion / diff / portability gate? [Measurability, Spec §SC]
- [ ] CHK023 Do the user-story Independent Tests each map to a concrete, asserted measurement? [Coverage, Spec US1–US4]

## Notes
- A failing item means the *requirement text* needs work, not the code.
- Companion to `requirements.md` (general spec quality); this file is measurement-specific.
- ≥80% of items carry a `[Spec §…]` / marker reference.
