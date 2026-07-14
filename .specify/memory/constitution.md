<!--
================================================================================
  ‼  THE acfx COMMANDMENTS — NON-NEGOTIABLE  ‼
  I.  COMMIT AND PUSH EARLY AND OFTEN. Version control is a distributed, journaled
      filesystem that SAFEGUARDS your work. It is NOT a sacred rite reserved for
      the blessed. Small atomic commits, pushed promptly. Never hoard unpushed work.
  II. NO GIT HOOKS, EVER. This repository uses ZERO git hooks. None exist, none get
      added. Quality gates are explicit and visible, never hidden hooks.
  III.DESCRIPTIVE NAMES, NEVER NUMERIC PREFIXES. Names carry information; numbers
      imply a false order and false precision. Datestamps are the one exception.
  IV. ALL UI/UX WORK GOES THROUGH /frontend-design — NO EXCEPTIONS, NO OFFROADING.
      Every user-facing visual/interaction decision is produced via the frontend-design
      skill. No hand-rolled UI, no ad-hoc styling, no "it's simple" bypass.
================================================================================
-->

<!--
================================================================================
  SYNC IMPACT REPORT — amendment 2026-07-14
  Version: 1.3.0 → 1.4.0  (MINOR: new NON-NEGOTIABLE principle added)
  Added principle:
    - IV. All UI/UX Work Goes Through /frontend-design (NON-NEGOTIABLE) — new 4th
      commandment; every user-facing visual/interaction decision routes through the
      frontend-design plugin skill.
  Renumbered (labels only, meaning unchanged), old → new:
    - IV  Platform-Independent Core, Thin Adapters      →  V
    - V   No Fallbacks, No Mock Data Outside Tests       →  VI
    - VI  Real-Time Safety in the Audio Path             →  VII
    - VII Strict Typing & Small Modules                  →  VIII
    - VIII Test the Core Host-Side                       →  IX
    - IX  Progressive Layered Architecture               →  X
    - X   Measurable Engineering                         →  XI
    - XI  One Concept at a Time                          →  XII
  Cross-reference fix: "extends Principle VIII" (Measurable Engineering) → "IX".
  Templates propagated (commandments header now lists 1–4; citation I–III → I–IV):
    ✅ .specify/templates/checklist-template.md
    ✅ .specify/templates/plan-template.md
    ✅ .specify/templates/spec-template.md
    ✅ .specify/templates/tasks-template.md
  Also propagated to CLAUDE.md commandments block (4th commandment added).
  Deferred TODOs: none.
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
- No AI/Claude attribution in commit messages or PR descriptions.

Rationale: a wrong commit stays small and cheap to revert when commits are atomic;
small, frequent, pushed commits minimize lost work and keep the branch reviewable.
Treating commits as ceremony forfeits every one of these benefits.

### II. No Git Hooks, Ever (NON-NEGOTIABLE)

**This repository uses ZERO git hooks.** No `pre-commit`, `pre-push`,
`commit-msg`, or any other hook — and no hook-managing frameworks (husky,
pre-commit, lefthook, etc.). Do not install, generate, commit, or depend on them.

- Quality gates run as **explicit, visible steps** — local commands you run on
  purpose, and CI on the remote — never as hidden hooks that fire on commit/push.
- This supersedes any general "never bypass hooks" guidance: there are no hooks to
  bypass, and none are to be created.

Rationale: hooks are invisible, machine-local, and diverge silently between clones;
they block or rewrite work behind your back and create "works on my machine"
asymmetry. Explicit, portable gates beat hidden ones — and hidden gates directly
conflict with Principle I's "commit and push without ceremony."

### III. Descriptive Names, Never Numeric Prefixes (NON-NEGOTIABLE)

**Name things for what they ARE.** Numbers in names imply a false order and false
precision that the things themselves do not have.

- Branches, worktrees, directories, files, and identifiers use descriptive names
  (`platform-foundation`, `svf-filter`), never ordinal/sequence prefixes
  (`001-`, `02_`, `v2`, `step3`).
- **Exception: datestamps** (`2026-06-25-…`) are permitted — a date carries real,
  factual information (when something was authored), not invented ordering.

Rationale: a name like `001-platform-foundation` lies twice — it implies there was
a `000`, and that order matters. `platform-foundation` ferries the information that
actually matters: what the thing is.

### IV. All UI/UX Work Goes Through /frontend-design (NON-NEGOTIABLE)

**ALL UI/UX WORK GOES THROUGH `/frontend-design` — NO EXCEPTIONS, NO OFFROADING.**
Every piece of user-facing visual or interaction work MUST be produced via the
`frontend-design` plugin skill.

- This covers all of it: websites and web apps (including the companion training
  site), plugin/standalone app UIs, the desktop workbench's controls, and any single
  layout, typography, color, spacing, or visual-design decision.
- No hand-rolling UI outside the skill, no ad-hoc styling, and no bypassing it because
  a case looks "simple" — "simple" UI is exactly where unexamined defaults calcify.
- Invoke `frontend-design` **before** writing markup, styles, or visual-layout code,
  and let it drive the aesthetic and structural choices.

Rationale: coherent, intentional design requires a single disciplined front door.
Off-roading — "I'll just style this one thing myself" — is precisely how templated,
incoherent, default-looking UI accumulates. One channel keeps the visual language
consistent and every design decision examined, exactly as Principle II keeps quality
gates explicit and Principle III keeps names honest.

### V. Platform-Independent Core, Thin Adapters

The DSP core compiles with no knowledge of JUCE, libDaisy, or Teensy. Dependencies
point only inward (targets → core; core → nothing platform-specific). Each target
is a thin shell that feeds the core audio and parameters. No desktop-side hardware
stubs.

### VI. No Fallbacks, No Mock Data Outside Tests

Outside test code, the system MUST NOT implement fallbacks or use mock data.
Missing functionality or data MUST raise a descriptive error naming what is absent.
Fallbacks and mock data hide unimplemented paths and become permanent bug
factories; an error surfaces the gap immediately.

### VII. Real-Time Safety in the Audio Path

No heap allocation, locks, or unbounded work in any `process()` / audio-callback
path. The hot path stays templated/inlined; polymorphism is confined to the
host-side block boundary (at most one virtual call per block). This is what keeps
the core safe on a microcontroller and on a real-time audio thread.

### VIII. Strict Typing & Small Modules

Composition over inheritance; interface-first design across boundaries. No `any`,
no unchecked casts, no suppressed type errors. Source files stay within 300–500
lines; larger files are refactored for modularity.

### IX. Test the Core Host-Side

The platform-independent core is unit-tested on the host with no hardware:
parameter scaling, DSP correctness (impulse/frequency response against known-good
values), stability guards (no NaN/denormal), and the no-allocation invariant.

### X. Progressive Layered Architecture

The DSP core is organized into three layers — `labs/ → primitives/ → effects/` — as its
**target architecture**, established by the `three-layer-structure` roadmap work (the
physical `labs/` layer and primitive taxonomy do not yet exist on disk and are not assumed
present). Once that structure lands, every concept graduates through four stages: **Theory →
Laboratory → Reusable Primitive → Production Effect**. Laboratories introduce a concept
through a small, focused experiment; once understood, the concept becomes a reusable, tested
primitive; effects are built by composing primitives and document which primitives they use.
Educational code is not disposable: laboratory implementations evolve into production
primitives rather than being thrown away. (Program vision:
`docs/superpowers/specs/2026-06-29-acfx-progressive-dsp-prospectus.md`.)

### XI. Measurable Engineering

Every effect is validated by objective measurements, not opinion: frequency response,
impulse response, phase response, harmonic distortion (THD), latency, CPU usage, memory
allocation, and numerical stability. Measurements are the primary acceptance evidence;
listening tests complement them but never replace them. This extends Principle IX from
correctness invariants to a standard, reported metric suite for effects.

### XII. One Concept at a Time

Each phase — and each laboratory — introduces a single major new idea and applies it to
a complete effect before moving on. Advanced techniques (numerical solvers, wave digital
filters, physical models) are built up incrementally, never presented as an unexplained
black box; every step is explainable and reproducible.

## Governance

This constitution supersedes ad-hoc practice. Amendments require a written
rationale, a version bump per the policy below, and propagation to the dependent
templates in `.specify/templates/` in the same change. Compliance is checked at
planning and review — as explicit steps, never as a git hook (Principle II).

Versioning policy (semantic):
- MAJOR: backward-incompatible principle removal or redefinition.
- MINOR: a new principle/section added or materially expanded guidance.
- PATCH: clarifications, wording, non-semantic refinements.

**Program north star**: the multi-phase direction these principles serve is the
Progressive Audio DSP & Analog Modeling Platform prospectus
(`docs/superpowers/specs/2026-06-29-acfx-progressive-dsp-prospectus.md`), tracked on the
stack-control roadmap (the `progressive-dsp-platform` program node).

**Version**: 1.4.0 | **Ratified**: 2026-06-25 | **Last Amended**: 2026-07-14
