# Specification Quality Checklist: Synchronous, multi-format USB-audio transport

**Purpose**: Validate specification completeness and quality before proceeding to planning
**Created**: 2026-08-25
**Feature**: [spec.md](../spec.md)

## Content Quality

- [x] No implementation details (languages, frameworks, APIs) — *transport behaviour is described
  in USB-audio-class terms (sync type, per-frame frame count, clock source, latency reporting),
  which are the observable device contract, not a chosen code stack.*
- [x] Focused on user value and business needs — *the P1 story is "play through it at correct pitch,
  no noise"; multi-rate/bit-depth and latency are framed as user capabilities.*
- [x] Written for non-technical stakeholders — *the problem and outcomes read without firmware
  internals; USB terms are the device's external contract.*
- [x] All mandatory sections completed

## Requirement Completeness

- [x] No [NEEDS CLARIFICATION] markers remain — *all decisions were made with the operator during
  brainstorming; the one open technical risk (24-bit fit) is captured as an explicit operator-owned
  feasibility gate (FR-014), not a clarification.*
- [x] Requirements are testable and unambiguous
- [x] Success criteria are measurable — *pitch match, zero short/ZLP packets, no added noise,
  bounded latency, all rate×depth combos, operator acceptance.*
- [x] Success criteria are technology-agnostic — *phrased as observable outcomes (returned pitch,
  packet cadence, latency), not code internals.*
- [x] All acceptance scenarios are defined
- [x] Edge cases are identified — *mid-stream rate/format change, cold-buffer startup, momentary
  shortfall, suspend/resume, 24-bit budget overflow.*
- [x] Scope is clearly bounded — *44.1/48 kHz × 16/24-bit stereo; 88.2/96 kHz explicitly out; 24-bit
  gated on feasibility.*
- [x] Dependencies and assumptions identified — *no-converter timebase, aggregate drift-correction,
  FIFO budget, DSP prepare(sampleRate) seam, verification approach.*

## Feature Readiness

- [x] All functional requirements have clear acceptance criteria
- [x] User scenarios cover primary flows — *pitch-correct monitoring (P1), native multi-rate (P1),
  bit depth (P2), latency (P2), objective verification (P3).*
- [x] Feature meets measurable outcomes defined in Success Criteria
- [x] No implementation details leak into specification

## Notes

- All items pass on the first iteration. The 24-bit feasibility (FR-014) is the one item the plan's
  research must resolve before implementation commits to it; it is scoped as an operator-owned gate,
  so it does not block spec completion.
- This spec supersedes base-feature decisions D20 (no feedback endpoint) and D4 (single 48/16
  format); FR-015 records that so the base spec is not read as still-authoritative.

## Third-party UAC2 review resolutions (2026-08-25)

A USB-Audio-Class review requested changes; all adopted (one tempered):

1. **44.1 kHz cadence** — corrected from a wrong "44/45 alternation" (averages 44.5 kHz) to the
   exact **44 100 frames / 1 000 USB frames** rational-accumulator schedule (US2, FR-002, FR-013,
   SC-002).
2. **Latency mechanism** — adopted the correction that `bLockDelay*` is clock-lock delay, not
   end-to-end latency, and that host delay-compensation must be verified not assumed (FR-009,
   US4). **Tempered:** did NOT assert a standard "UAC2 Latency Control" exists — framed as a
   plan research item ("the correct mechanism, if one hosts consume exists"), since its existence
   is unconfirmed.
3. **24-bit wire format pinned** — packed 3-byte subslots (`bSubslotSize=3`, 24-bit, signed LE),
   288 bytes/ms at 48 kHz; consistent across FR-005, FR-010, FR-014, Key Entities.
4. **Rate vs format selection separated** — rate via Clock Source frequency control, format via
   AudioStreaming alternate setting (FR-004, FR-005, FR-006, edge cases).
5. **Single shared clock domain** — both interfaces reference one SOF-derived Clock Source
   (new FR-016).
6. **FR-001 rationale reframed** — coherent SOF-locked nominal rate, NOT "no host resampling
   ever" (which contradicted the aggregate-drift-correction assumption).
7. **OUT abnormal-packet fault handling carried forward** — new FR-017 with the IN-owns-cadence /
   OUT-owns-contents asymmetry.
8. **Packet-capture guard strengthened** — full metric set + accumulated-rate tracking (FR-013,
   US5, SC-002).
9. **Latency success criterion made measurable** — SC-004 now requires pinned measured values
   (ring capacity/fill/water range, round-trip in frames + ms, device-reported latency).
