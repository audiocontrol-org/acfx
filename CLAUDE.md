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
> These commandments are repeated, by design, in `.specify/memory/constitution.md`
> (Principles I–III) and at the top of every file in `.specify/templates/`.
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
  ~300–500 lines.
- Full project principles live in `.specify/memory/constitution.md`.
