# Development Notes

---

## 2026-06-25: SVF vertical slice — full front-door specifying phase (design → runnable tasks)

**Goal:** Pick up the approved acfx platform design (which had nothing downstream)
and take it into the governed build pipeline through the stack-control front door —
roadmap → design → define → plan → tasks → analyze.

**Accomplished:**
- Added the Milestone-1 roadmap item `design:feature/svf-vertical-slice` (via `roadmap add`, not hand-edit).
- Installed the base GitHub Spec Kit command layer for Claude; preserved the customized constitution + commandment-banner templates (git as the safety net — restored templates after `specify init` clobbered the banners).
- Drove the full Spec Kit chain for the SVF vertical slice: `spec.md` (3 prioritized user stories, 19 FRs, 8 SCs), `plan.md` + Phase 0/1 artifacts (`research.md` resolving the 4 deferred open-items, `data-model.md`, 3 `contracts/`, `quickstart.md`), `tasks.md` (39 tasks, 100% FR+SC coverage), and a clean `analyze`.
- Recorded `analyze-clean`; the node advanced to phase **implementing** (next move: governing/execute).
- Filed deskwork#507 for the front-door adoption gap; captured 2 tooling-friction notes.

**Didn't Work:**
- `/stack-control:define` dead-ended: base Spec Kit was never installed in this repo (only constitution + templates were committed), so `/speckit-specify` was unresolvable — yet `setup` reported `ready: yes`.
- The design-control plugin's bundled `speckit-*` skills are internal-only; not exported to host projects. Freshly-installed project `speckit-*` skills weren't in this session's Skill-tool registry (registry is fixed at session start).
- `check-prerequisites.sh` hard-failed on the non-numeric branch `platform-foundation` (TF-09), colliding with Commandment III.
- `stackctl session-end` auto-derived **0 commits** (boundary misfire — the branch tracks itself; merge-base gave HEAD). Corrected by hand below.

**Course Corrections:**
- Installed base Spec Kit via `specify init --here --force --integration claude --no-git`, then restored the customized templates from git; committed the install as a clean restore point (944d798).
- When the project `speckit-*` skills weren't yet registered, drove each skill's documented procedure directly (its SKILL.md is the canonical flow) — faithful to "drive native Spec Kit."
- Past the branch-name gate, resolved `FEATURE_DIR` via `.specify/feature.json` (as the `define` skill prescribes) and used a descriptive spec dir `specs/svf-vertical-slice` (no numeric prefix) per Commandment III.

**Insights:**
- **stack-control governs Spec Kit but does not install it** — base Spec Kit is an unstated prerequisite of the front door; `setup`'s "ready: yes" overstates readiness for `define`. This is the core of deskwork#507.
- Vanilla Spec Kit's `NNN-`/branch numbering assumptions collide with Commandment III; the clean workaround is an explicit descriptive spec dir + `feature.json` resolution (works for `setup-plan`/`setup-tasks`; only `check-prerequisites.sh` still enforces the branch name).
- `session-end`'s commit-count auto-derivation is unreliable when the branch's upstream is itself — verify the quantitative block every time (a stackctl defect worth filing).

**Quantitative (corrected by hand; auto-derivation reported 0 — boundary misfire):**
- Session boundary: `6325755` (session-start HEAD) `..HEAD`.
- Commits: 6
  - `92bdce2` Add Milestone-1 SVF vertical-slice item to roadmap
  - `944d798` Install GitHub Spec Kit command layer (Claude integration)
  - `8c8e9a2` Author SVF vertical-slice spec via Spec Kit (specify phase)
  - `8a7115e` Plan SVF vertical slice via Spec Kit (plan phase + Phase 0/1 artifacts)
  - `cd90607` Generate SVF vertical-slice tasks + record analyze-clean (specifying phase complete)
  - `6397c1c` docs(session): session-end record
- Files changed: 46 (+5510)
- Backlog touched: (none)
- Next session: `/stack-control:execute` (add the deskwork-governance Spec Kit extension first; MVP scope = US1, Phases 1+2+3).
