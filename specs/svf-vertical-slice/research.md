# Phase 0 Research — SVF Vertical Slice

Resolves the approved design's deferred open-items and records best-practice
decisions for the load-bearing techniques. Each entry: **Decision / Rationale /
Alternatives considered**.

## 1. SVF implementation: wrap DaisySP's `Svf`

- **Decision**: For this milestone the SVF effect wraps DaisySP's proven `Svf`
  primitive behind the platform's own `core/primitives` wrapper and parameter
  model. The effect exposes cutoff (skewed/log), resonance (linear), and mode
  (low/high/band) and delegates the per-sample math to DaisySP.
- **Rationale**: Consistent with the design's *locked* decision "DSP primitives —
  reuse DaisySP (wrapped)". Lowest DSP risk for the proving slice; DaisySP's `Svf`
  is MCU-proven and allocation-free, so it satisfies Constitution VI directly. The
  milestone's purpose is to prove the *spine*, not to author novel DSP.
- **Alternatives considered**: An in-house Cytomic/Simper TPT SVF for full
  ownership and to remove the DaisySP dependency from the inner loop. **Deferred**
  to a later cycle — it adds DSP-correctness risk and ownership work orthogonal to
  proving the cross-platform spine. Captured in the spec's out-of-scope list.

## 2. Workbench audio source: both live input and a built-in player

- **Decision**: The workbench supports **both** a built-in loop/file player and a
  live input device, selectable at runtime. The built-in player is the default for
  reproducible A/B and hardware-free testing; live input is the real sketch use.
- **Rationale**: A/B and regression listening need a deterministic source (player);
  the day-to-day sketch loop needs live input. Both reuse the same `ProcessorNode`
  path, so the cost is in the workbench adapter only, not the core.
- **Alternatives considered**: Player-only (simpler, but can't sketch against a
  live instrument) and live-only (no deterministic A/B). Rejected — each drops one
  of the two real workflows. If no source is available, the workbench raises a
  descriptive error (Constitution V), never silent zeros/mock audio.

## 3. C++ standard per target: C++20 where supported, C++17 on Teensy

- **Decision**: `core/` is written to the common C++17 subset and *additionally*
  guards the `Effect`/`Generator` **concept** contract-checks behind a C++20
  feature test. Desktop, tests, and Daisy (arm-none-eabi-gcc) build at C++20 and
  get the named concept checks; Teensy builds at **C++17** and the same effect
  types compile as plain duck-typed templates (the contract-check is lost on that
  one target, never the code).
- **Rationale**: Matches the design exactly. Keeps one source of truth for effects
  while not blocking on Teensy's toolchain.
- **NEEDS-VERIFICATION (implementation)**: the exact C++ standard the installed
  Teensy core/toolchain supports (C++14/17/20). The plan assumes C++17; the
  implementation phase confirms and, if higher is available, raises Teensy to it.
  This is a verification task in `tasks.md`, not an unresolved spec ambiguity.

## 4. Dependency pinning via CPM: known-good tags, recorded when fetched

- **Decision**: All external dependencies are fetched by CPM.cmake and pinned to an
  explicit, known-good release **tag/commit** in the `CPM` declaration: JUCE 8,
  `clap-juce-extensions`, DaisySP, libDaisy, Teensy core + Audio Library, doctest.
  The exact tag is captured in the CMake declaration at the moment each dependency
  is first fetched and verified to build — **not** invented in this plan.
- **Rationale**: Reproducibility (Constitution-aligned, no system-installed
  surprises) without fabricating version numbers now (false precision erodes
  trust). The pinned tag becomes a reviewable line in the build files.
- **Alternatives considered**: System/`find_package` dependencies (rejected — not
  reproducible across the four toolchains); git submodules (rejected — CPM gives
  pinning + caching with less ceremony and matches the design).

## 5. The host boundary: one virtual call per block

- **Decision**: A `ProcessorNode` abstract interface (in desktop-only
  `host/processor-node/`) wraps a concrete templated `Effect` via a small adapter
  template. Desktop hosts hold `std::unique_ptr<ProcessorNode>` and call
  `processBlock()` once per audio block; inside, the call dispatches to the inlined,
  non-virtual `Effect::process`.
- **Rationale**: Polymorphism only where it is free (once per block, desktop-side);
  zero virtual overhead in the per-sample inner loop and on MCU (which never
  includes `ProcessorNode`). Directly satisfies FR-004 / Constitution VI.
- **Alternatives considered**: A virtual `Effect` base class (rejected — vtable in
  the hot path, fails VI on MCU); `std::variant`/visitor over a closed effect set
  (rejected — milestone wants the open `Effect` concept, and variant adds dispatch
  cost without the polymorphism the host actually needs).

## 6. The no-allocation invariant test technique

- **Decision**: Host-side test overrides global `operator new`/`operator delete`
  (or installs a thread-local allocation sentinel) to count allocations, then
  asserts the count is **zero** across a representative `prepare`-then-N-`process`
  sequence at several block sizes. The sentinel is test-only support code.
- **Rationale**: Makes FR-014 a real, automated gate rather than a code-review
  promise. Cheap, deterministic, runs in CI.
- **Alternatives considered**: A custom RT-audio allocator that traps (heavier,
  and we want a test assertion, not a runtime guard); static analysis only
  (insufficient — can't see transitive allocations in dependencies).

## 7. CLAP export with JUCE 8

- **Decision**: Export VST3 + AU natively via JUCE 8 and add CLAP through the
  `clap-juce-extensions` add-on (pinned via CPM), per the design.
- **Rationale**: JUCE 8 does not ship CLAP natively; the add-on is the established
  path and keeps a single JUCE plugin target producing all three formats.
- **Alternatives considered**: Dropping CLAP for the milestone (rejected — CLAP is
  an explicit deliverable); a separate non-JUCE CLAP build (rejected — duplicates
  the plugin adapter, breaks "plugin and workbench share the core").

## Outcome

All four spec-deferred open-items are resolved (one with an explicit
implementation-phase verification task for Teensy's C++ level). No
`[NEEDS CLARIFICATION]` remains that blocks design. Ready for Phase 1.
