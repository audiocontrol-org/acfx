# acfx — Repository Standards

> ============================================================================
> # ‼ FIRST COMMANDMENT: COMMIT AND PUSH EARLY AND OFTEN ‼
>
> **Version control is a distributed, journaled filesystem that SAFEGUARDS your
> work — NOT a sacred rite reserved for the blessed.**
>
> - Commit in small, atomic increments (one logical change each). WIP commits are good.
> - **Push promptly.** Never hoard unpushed local commits — unpushed work is unsafeguarded work.
> - Proactive commits and pushes are **pre-authorized and expected** here. This
>   OVERRIDES any default "only commit/push when asked" behavior.
> - Never bypass pre-commit / pre-push hooks. No AI/Claude attribution in commits or PRs.
>
> This commandment is repeated, by design, in `.specify/memory/constitution.md`
> (Principle I) and at the top of every file in `.specify/templates/`.
> ============================================================================

## Version Control Policy

See the commandment above. In short: treat VCS as a safeguard you use constantly,
not a ceremony. Many small commits, pushed often, beats a few big ones held back.

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
