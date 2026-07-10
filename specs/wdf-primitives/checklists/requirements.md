# Specification Quality Checklist: WDF-primitives

**Purpose**: Validate specification completeness and quality before proceeding to planning
**Created**: 2026-07-09
**Feature**: [spec.md](../spec.md)

## Content Quality

- [x] No implementation details (languages, frameworks, APIs)
- [x] Focused on user value and business needs
- [x] Written for non-technical stakeholders
- [x] All mandatory sections completed

> Note (library-primitive interpretation, consistent with the sibling
> `implicit-integration` spec): the "user" of this feature is a DSP developer, and the
> API surface (the one-port port interface, port-resistance / reflection contracts,
> placement) IS the user-facing contract. Naming the interface shape, the namespace, and
> the file budget is contract specification, not premature implementation — the spec
> states WHAT each one-port must expose and WHY, not HOW the scattering math is coded.
> This mirrors how the shipped nodal-primitive specs named `CompanionSupply`, `noexcept`,
> and placement in their FRs.

## Requirement Completeness

- [x] No [NEEDS CLARIFICATION] markers remain
- [x] Requirements are testable and unambiguous
- [x] Success criteria are measurable
- [x] Success criteria are technology-agnostic (measured as observable scattering /
      passivity / allocation outcomes, not internal mechanisms)
- [x] All acceptance scenarios are defined
- [x] Edge cases are identified
- [x] Scope is clearly bounded
- [x] Dependencies and assumptions identified

## Feature Readiness

- [x] All functional requirements have clear acceptance criteria
- [x] User scenarios cover primary flows
- [x] Feature meets measurable outcomes defined in Success Criteria
- [x] No implementation details leak into specification (beyond the API-contract surface a
      library primitive necessarily specifies — see note above)

## Notes

- Spec authored faithfully from the operator-approved, third-party-reviewed design record
  `docs/superpowers/specs/2026-07-09-wdf-primitives-design.md`. Scope boundary (leaf
  one-ports + wave convention + port ABI; adaptors/tree/roots deferred to sibling nodes)
  is the design record's explicit split per the roadmap's four WDF nodes — captured
  cross-cuts, not YAGNI scope cuts.
- No `[NEEDS CLARIFICATION]` markers: the design record + third-party review resolved the
  domain calls (voltage waves, bilinear, leaf lifecycle). Remaining Open Questions are
  genuinely downstream (signal I/O, variable dt, root-element affordances, placement) and
  are recorded as such, not blocking clarifications.
- All checklist items pass — spec is ready for `/speckit-clarify` (expected to be a no-op
  given the resolved design) or `/speckit-plan`.
- **Third-party spec review (2026-07-09), Request Changes → resolved.** One blocking issue
  and one non-blocking observation, both accepted after technical verification:
  1. **(Blocking) Passivity criterion was wrong for reactive one-ports.** Same-sample
     `|b| ≤ |a|` is invalid for a reactive leaf (`b[n] = a[n−1]` returns previously-stored
     energy; `a[n−1]=1, a[n]=0 → b[n]=1 > |a[n]|`). Fixed: FR-017/FR-021/SC-003/US7 now
     split passivity into memoryless-instantaneous (`|b| ≤ |a|`, `Rp > 0`) vs. reactive
     **wave-power balance across state transitions** (`Σ(a² − b²) ≥ 0`, telescoping to the
     stored `a[N]²` for the lossless capacitor — energy stored and returned).
  2. **(Non-blocking) General resistor reflection vs. adapted-only API.** The leaf API is
     now explicitly **adapted-only**; the general `b = a(R−Rp)/(R+Rp)` is a test-suite
     oracle, not a public capability (arbitrary reference resistance is the adaptor layer's
     concern). Fixed: FR-002, US1, Edge Cases.
  Reviewer's conclusion: after the passivity correction, the spec is ready for
  implementation planning.
