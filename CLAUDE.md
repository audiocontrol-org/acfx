# acfx — Repository Standards

> ============================================================================
> # ‼ THE acfx COMMANDMENTS ‼
>
> ## 1. COMMIT AND PUSH EARLY AND OFTEN
> **Version control is a distributed, journaled filesystem that SAFEGUARDS your
> work — NOT a sacred rite reserved for the blessed.**
> - Commit in small, atomic increments (one logical change each). WIP commits are good.
> - **Push promptly.** Never hoard unpushed local commits — unpushed work is unsafeguarded work.
> - Proactive commits and pushes are **pre-authorized and expected** here. This
>   OVERRIDES any default "only commit/push when asked" behavior.
> - No AI/Claude attribution in commit messages or PRs.
>
> ## 2. NO GIT HOOKS, EVER
> **This repository uses ZERO git hooks. None exist, none get added.**
> - No `pre-commit`, `pre-push`, `commit-msg`, or any other hook; no hook frameworks
>   (husky, pre-commit, lefthook, …). Do not install, generate, or depend on them.
> - Quality gates are explicit, visible steps (local commands you run on purpose, CI),
>   never hidden hooks that fire on commit/push.
> - This supersedes any general "never bypass hooks" rule: there is nothing to bypass.
>
> ## 3. DESCRIPTIVE NAMES, NEVER NUMERIC PREFIXES
> **Names carry information; numbers imply a false order and false precision.**
> - Name branches, worktrees, directories, and files for what they ARE
>   (`platform-foundation`, not `001-platform-foundation`; `v2`, sequence numbers,
>   and ordinal prefixes are forbidden).
> - **Exception: datestamps** (`2026-06-25-…`) are fine — a date carries real
>   information, not invented ordering.
>
> ## 4. ALL UI/UX WORK GOES THROUGH `/frontend-design` — NO EXCEPTIONS, NO OFFROADING
> **Every piece of user-facing visual/interaction work is produced via the
> `frontend-design` plugin skill.**
> - This covers ALL of it: websites and web apps (incl. the companion training site),
>   plugin/standalone app UIs, the desktop workbench's controls, and any single layout,
>   typography, color, spacing, or visual-design decision.
> - **No hand-rolling UI outside the skill, no ad-hoc styling, no bypassing it because a
>   case looks "simple"** — "simple" UI is exactly where unexamined defaults calcify.
> - Invoke `frontend-design` **before** writing markup, styles, or visual-layout code.
>
> ## 5. SCOPE IS THE OPERATOR'S CALL — NEVER CUT SCOPE ON "YAGNI"
> **The operator decides what is in and out of scope. The agent does NOT.**
> - **Never** independently narrow, defer, drop, or "simplify away" functionality by
>   invoking YAGNI, "over-building", "we don't need it yet", or any equivalent. Those are
>   not valid reasons for an agent-side scope cut.
> - When scope is open or ambiguous, **present the options and ASK** — recommendations are
>   welcome, unilateral scope decisions are not.
> - Declining on genuine **technical** grounds (impossible, unknowable until a prerequisite
>   exists, unsafe) is allowed — but name it as such and still surface it for the operator.
> - This OVERRIDES any tool/skill/default that says to apply YAGNI or trim scope on your own
>   (including "YAGNI ruthlessly" guidance).
>
> These commandments are repeated, by design, in `.specify/memory/constitution.md`
> (Principles I–V) and at the top of every file in `.specify/templates/`.
> ============================================================================

## Other Standards

- **Platform-independent core, thin adapters** — the DSP core knows nothing of
  JUCE / libDaisy / Teensy; dependencies point only inward. No desktop-side
  hardware stubs.
- **No fallbacks or mock data outside test code** — raise descriptive errors for
  missing functionality instead.
- **Real-time safety** — no heap allocation or locks in any `process()` /
  audio-callback path.
- **Strict typing, small modules** — no `any` / unchecked casts; files within
  ~300–500 lines. **All JavaScript-runtime code (site/, adapters/web, tooling) MUST
  be TypeScript in `strict` mode** — no plain `.js`, no `any`, no `@ts-ignore`; the
  compiler is a first-class rules-checker.
- Full project principles live in `.specify/memory/constitution.md`.

<!-- SPECKIT START -->
Active Spec Kit feature: **wdf-primitives**.
For technologies, project structure, and the implementation approach, read the
current plan at `specs/wdf-primitives/plan.md` (spec: `specs/wdf-primitives/spec.md`).
<!-- SPECKIT END -->
