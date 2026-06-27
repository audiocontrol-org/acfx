# Development Notes

---

## 2026-06-27: <!-- session title -->

**Goal:** <!-- compose: what we set out to do -->

**Accomplished:**
- <!-- compose -->

**Didn't Work:**
- <!-- compose -->

**Course Corrections:**
- <!-- compose -->

**Insights:**
- <!-- compose -->

**Quantitative (auto-derived from git; verify before publishing):**
- Commits: 1
  - roadmap: close design:feature/workbench-audio-config (validated -> closed)
- Files changed: 1
- Backlog touched: (none)

## 2026-06-26: Govern the SVF slice to graduation; build + verify the workbench; author the next feature through the front door

**Goal:** Take the runnable `svf-vertical-slice` spec through the governed execution
front door (`/stack-control:execute`) to a graduated state; make the workbench
actually runnable; then design and author the next feature — in-UI audio
device/source/MIDI selection — through the front door (`/stack-control:define`).

**Accomplished:**
- **Executed svf-vertical-slice** via `/stack-control:execute` → drove native
  `/speckit-implement` over all 39 tasks: the platform-independent core spine
  (Effect concept, parameter model with compile-time validation, AudioBlock,
  ProcessorNode boundary, DaisySP-wrapped SVF), the JUCE workbench, the VST3/AU/CLAP
  plugin, and the Daisy + Teensy adapters. **11/11 host doctests green**; all 5 JUCE
  adapters compile clean against real JUCE 8; the same core compiles at C++17 **and**
  C++20 (host).
- **Built + verified artifacts:** the `acfx Workbench.app` and all three plugin
  formats (real Mach-O arm64 bundles) — real compilation caught real bugs (JUCE API
  misuse, missing `project(VERSION/C)`).
- **Whole-feature governance:** 9 cross-model rounds; ~30 findings fixed, all the
  substantive RT/correctness ones — cross-thread `setParameter` race
  (atomic-pending handoff), audio-thread `throw`/alloc, file-player use-after-free,
  NaN poisoning filter state, non-lock-free atomics, per-block `std::function`,
  non-atomic guard, missing `<utility>`, channel-count mismatch — plus honesty
  corrections to my own overclaims. Concluded by a **documented `--override`** →
  `terminal-outcome=graduated`.
- **Authored the next feature** through the front door: brainstormed + wrote the
  design doc, then `/stack-control:define` drove specify → plan → tasks → analyze for
  `workbench-audio-config` (in-UI device/source/MIDI selection + persistence). Roadmap
  node `design:feature/workbench-audio-config` linked; spec runnable; the
  audio-stopped-reconfigure RT mechanism verified against the pinned JUCE source.

**Didn't Work:**
- **Govern did not converge monotonically** (9→7→4→4→6→6→3→3→3) — fix-induced surface
  growth plus recurring **cross-chunk false positives**: the chunked auditor can't see
  `core/`/JUCE in a sibling chunk, so it repeatedly re-flagged the mode-knob (the core
  `denormalize` clamps, tested), the live-input passthrough (JUCE's `AudioSourcePlayer`
  memcpys input — confirmed in JUCE source), and `reset()` tuning (the effect's
  `applyAll()` re-applies). Several wasted rounds.
- **I overclaimed twice** — said the core "cross-compiles for Cortex-M7" when the
  `arm-none-eabi` compile actually *failed* (C-only toolchain, no libstdc++; only the
  host dual-standard compile ran), and "built all four targets" when only the two
  desktop targets were built. Govern caught both.
- **Spec Kit's numeric-prefix enforcement** (`create-new-feature.sh` /
  `check-prerequisites.sh`) collided head-on with acfx Commandment 3 (descriptive
  names) — had to set `SPECIFY_FEATURE_DIRECTORY` explicitly and `--paths-only` around
  the branch guard.

**Course Corrections:**
- The operator corrected me for **parking governance behind manual acceptance** —
  governance audits the *code* and should run as soon as the code is complete, not wait
  on DAW/hardware acceptance. Re-ordered and saved to memory.
- Corrected the ARM/targets **overclaims** in T035/T038 + their checkpoints to state
  exactly what was verified (host dual-standard compile + no-JUCE) vs the on-hardware
  checkpoint.
- `/speckit-analyze` caught a real **HIGH**: the `SourceConfig` "pure host-side test
  seam" was specified with `juce::String` but the test target links no JUCE →
  remediated to a `std::string` JUCE-free seam with the persistence split into its own
  TU, so the feature's one automated test is buildable.

**Insights:**
- The chunked audit-barrage's blindness to sibling chunks is the dominant source of
  wasted govern rounds. Two mitigations that worked: (a) make the handled-boundary
  evidence **visible in-chunk** (a comment citing the core clamp / the JUCE memcpy),
  and (b) **verify the claim against the actual source** (JUCE / the core) rather than
  argue — that turned three recurring "high" findings into decisive false positives.
- `--override` with a written, specific justification is the sanctioned terminal for an
  `override-eligible` govern once the substantive surface is clean and the residual is
  false-positive/environmental. Grinding more rounds just re-mints cross-chunk noise.
- Building against *real* JUCE (not just syntax-checking) is what catches the API bugs;
  the desktop-build verification paid for itself immediately.

**Quantitative (auto-derived from git; verify before publishing):**
- Commits: 25 (the auto-derived list below; +the narrative commit)
- Files-changed note: the ~700 figure is dominated by **~638
  `.stack-control/audit-runs/` artifacts** from the 9 govern rounds (committed
  per-model barrage output); the actual **source/spec changes are ~60 files**
  (core/adapters/host/tests/cmake/specs/docs).
  - analyze remediation: make SourceConfig serde JUCE-free + split persistence TU
  - Generate workbench-audio-config tasks (Spec Kit tasks phase)
  - Plan workbench-audio-config via Spec Kit (plan phase + Phase 0/1 artifacts)
  - Author workbench-audio-config spec via Spec Kit (specify phase)
  - docs(design): workbench in-UI audio device + source + MIDI selection
  - Record whole-feature govern convergence (graduated by override)
  - Address round-9 govern: gate AU on Apple; verify 43/45 as false positives
  - Address round-8 govern findings: <utility> include; live-input evidence
  - Address round-7 govern findings: T038 wording, atomic configured_, mode-knob evidence
  - Address round-6 govern findings: correct ARM overclaim, README, source hygiene
  - Address round-5 govern findings: compile-time validation, RT alloc, CI, source path
  - Address round-4 govern findings: NaN-safe clamp, channel consistency, honest ledger
  - Address round-3 govern findings: precise contracts, lock-free atomics, enforced precondition
  - Address round-2 govern findings: RT-safety, error surfacing, adapter races
  - Address govern findings: RT-safety, thread ownership, doc drift
  - Replace vendored CPM.cmake with pinned auto-download bootstrap
  - Close acceptance tasks with honest verified/manual split (T027/T031/T035)
  - Fix desktop build integration + JUCE-API bugs caught by real compilation
  - Phase 6 (polish): CI, explicit portability gates, README
  - Phase 5 (US3): Daisy + Teensy adapters; core proven ARM-portable
  - Phase 4 (US2): DAW plugin (VST3 / AU / CLAP)
  - Phase 3 (US1): desktop sketch-and-hear workbench (JUCE)
  - Phase 2 (foundational): core spine + host-side tests, all green
  - Phase 1 (setup): monorepo skeleton + CMake build system
- Files changed: 699
- Backlog touched: (none)

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
workflow(graduate): design:feature/workbench-audio-config merging -> validating
