<!--
================================================================================
  ‼  THE FIRST COMMANDMENT — COMMIT AND PUSH EARLY AND OFTEN  ‼
  Version control is a distributed, journaled filesystem that SAFEGUARDS your
  work. It is NOT a sacred rite reserved for the blessed. Commit in small
  increments. Push promptly. Never hoard unpushed local commits.
================================================================================
-->

# acfx Constitution

This constitution governs development of **acfx**, a cross-platform audio DSP
platform (effects now; synths, drum machines, and samplers later) targeting DAW
plugins, desktop standalone, Daisy, and Teensy from one shared core. It supersedes
ad-hoc practice. Where it overlaps with the work-level and user-level `CLAUDE.md`
files, they are intended to agree; those files remain authoritative for the wider
environment.

## Core Principles

### I. Commit and Push Early and Often (NON-NEGOTIABLE)

**COMMIT AND PUSH EARLY AND OFTEN.** Version control is a distributed, journaled
filesystem that safeguards changes — **NOT a sacred rite reserved for the
blessed.**

- Prefer many small, atomic commits (one logical change each) over a few large
  ones. Commit work-in-progress freely.
- **Push promptly.** Do not let local commits pile up unpushed — unpushed work is
  unsafeguarded work.
- Proactive commits and pushes are **pre-authorized and expected** in this repo;
  this overrides any default "only commit/push when explicitly asked" behavior.
- Never bypass pre-commit or pre-push hooks. Fix issues; do not skip them.
- No AI/Claude attribution in commit messages or PR descriptions.

Rationale: a wrong commit stays small and cheap to revert when commits are atomic;
small, frequent, pushed commits minimize lost work and keep the branch reviewable.
Treating commits as ceremony forfeits every one of these benefits.

### II. Platform-Independent Core, Thin Adapters

The DSP core compiles with no knowledge of JUCE, libDaisy, or Teensy. Dependencies
point only inward (targets → core; core → nothing platform-specific). Each target
is a thin shell that feeds the core audio and parameters. No desktop-side hardware
stubs.

### III. No Fallbacks, No Mock Data Outside Tests

Outside test code, the system MUST NOT implement fallbacks or use mock data.
Missing functionality or data MUST raise a descriptive error naming what is absent.
Fallbacks and mock data hide unimplemented paths and become permanent bug
factories; an error surfaces the gap immediately.

### IV. Real-Time Safety in the Audio Path

No heap allocation, locks, or unbounded work in any `process()` / audio-callback
path. The hot path stays templated/inlined; polymorphism is confined to the
host-side block boundary (at most one virtual call per block). This is what keeps
the core safe on a microcontroller and on a real-time audio thread.

### V. Strict Typing & Small Modules

Composition over inheritance; interface-first design across boundaries. No `any`,
no unchecked casts, no suppressed type errors. Source files stay within 300–500
lines; larger files are refactored for modularity.

### VI. Test the Core Host-Side

The platform-independent core is unit-tested on the host with no hardware:
parameter scaling, DSP correctness (impulse/frequency response against known-good
values), stability guards (no NaN/denormal), and the no-allocation invariant.

## Governance

This constitution supersedes ad-hoc practice. Amendments require a written
rationale, a version bump per the policy below, and propagation to the dependent
templates in `.specify/templates/` in the same change. Compliance is checked at
planning and review.

Versioning policy (semantic):
- MAJOR: backward-incompatible principle removal or redefinition.
- MINOR: a new principle/section added or materially expanded guidance.
- PATCH: clarifications, wording, non-semantic refinements.

**Version**: 1.0.0 | **Ratified**: 2026-06-25 | **Last Amended**: 2026-06-25
