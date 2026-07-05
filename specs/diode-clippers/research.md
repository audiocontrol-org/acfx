# Phase 0 — Research: diode-clippers

All decisions below are resolved (no open NEEDS CLARIFICATION). They consolidate the approved
design record (`docs/superpowers/specs/2026-07-04-diode-clippers-design.md`) and the 2026-07-05
`/speckit-clarify` session, grounded in the shipped `component-abstractions` and
`passive-tone-stacks` code.

## R1 — Per-topology circuit layout (the three builders)

- **Decision.**
  - **Symmetric shunt** — `Vin` (grounded `VoltageSource`) → series `Resistor R` → node `n1`; a matched antiparallel `Diode` pair between `n1` and ground (`Diode{n1,gnd}` + `Diode{gnd,n1}`); a filter `Capacitor Cf` across the diodes (`n1`→ground); output at `n1`.
  - **Asymmetric shunt** — same series-R → shunt structure, but an **unequal** diode population (v1 canonical: 2-up / 1-down = three diodes: two `Diode{n1,gnd}` in series-or-parallel per the chosen asymmetric form, one `Diode{gnd,n1}`), giving a non-odd transfer with a DC offset. Exact population (2:1 parallel vs a two-in-series leg) fixed in data-model.
  - **Series** — `Vin` → **input coupling `Capacitor Cc`** (series) → node `n1`; inline `Diode`(s) from `n1` to `n2`; `Resistor R` from `n2` to ground; output at `n2`. (OQ5 resolved: coupling cap in series, not across the diodes.)
- **Rationale.** These are the canonical passive clipper forms; the shunt pair isolates the *symmetry* axis (and forces the diode count past the static solver's `kMaxDiodes=2`), the series form isolates the *topology / reactance-placement* axis. All compose only frozen vocabulary.
- **Alternatives considered.** Op-amp-feedback (Tube Screamer) clipper — rejected/deferred (needs the deferred op-amp/nullor element). Bridge/full-wave clipper — captured in the design taxonomy, not a v1 exemplar.

## R2 — Diode-population cap (`MaxDiodes`) and augmented capacity

- **Decision.** The transient solver is `TransientClipper<MaxNodes, MaxComponents, MaxDiodes>` with **default `MaxDiodes=4`**. Its internal per-iteration augmented netlist is `Netlist<MaxNodes, MaxComponents + 2·MaxDiodes>` (each diode linearizes to a `Resistor` + `CurrentSource`).
- **Rationale.** `MaxDiodes=4` admits the 2-up/1-down asymmetric string with headroom; templated so a future topology instantiates larger without a global ceiling; heap-free by construction (mirrors `component-abstractions` OQ3 templated-capacity resolution).
- **Alternatives considered.** A fixed `kMaxDiodes` constant (as the static `NewtonClipper`) — rejected: too rigid for the asymmetric population; the design review chose a template parameter.

## R3 — Timestep / Newton loop separation (the load-bearing mechanism)

- **Decision.** The solver's `step(netlist, dt)` runs:
  1. **Once per timestep (outer):** compute each reactive element's backward-Euler companion `{Geq, Ieq}` from *held* history (via `capacitor.h`/`inductor.h` `companion(dt, ·)`); warm-start the port voltage from the previous converged sample.
  2. **Newton loop (inner), companions held fixed:** companion-linearize the diode string into a Norton pair (`Geq = g(vAK)`, `Ieq = I(vAK) − Geq·vAK`), append as ordinary linear stamps to a stack copy `Netlist<MaxNodes, MaxComponents + 2·MaxDiodes>`, solve the purely-linear system with a nested `LinearSolver`, damp the new junction voltage through `Diode::limitJunctionVoltage` (pnjlim), test `|Δv| < voltageTol`.
  3. **Once, after Newton converges (outer):** advance reactive history exactly once.
- **Rationale.** The existing `LinearSolver::solve()` advances reactive history on **every** call, so reusing it inside Newton would advance capacitor history N times per sample and corrupt the transient — precisely why `component-abstractions`' static `NewtonClipper` **refuses** reactive+nonlinear. Separating the loops (companions fixed across Newton, history advanced once) is the fix, and keeps the reactive companion contribution correct.
- **Alternatives considered.** Reuse `LinearSolver::solve()` per Newton iteration with caps in the netlist — rejected (corrupts history, per above). A full MNA branch-current formulation — rejected (out of the lab's charter; Phase 5).

## R4 — Newton contract and non-convergence handling

- **Decision.** Defaults `maxIterations = 50`, `voltageTol = 1e-9`, `currentTol = 1e-12` (held from the static solver initially, not pre-tuned for reactive stiffness). `step()` returns a `NewtonStatus { converged, iterations, voltageResidual, currentResidual }`. Convergence gates on `|Δv| < voltageTol`; the current residual is reported, not gating. **Non-convergence returns `converged=false` with the last iterate + residual — no fallback, no fabricated output** — and is surfaced in both the Tier-2 test and the harness (an explicit starved-budget case).
- **Rationale.** Measure-first: do not raise `N` pre-emptively (that could mask a conditioning problem); make failures visible instead. Matches the no-fallback constitution rule.
- **Alternatives considered.** Silent clamp / hold-last-good on non-convergence — rejected (a bug-hiding fallback). Pre-raising `N` for reactive stiffness — deferred to a measured tuning need (OQ2).

## R5 — Solver validation: prove-exact-first + independent oracle

- **Decision.** (a) A **linear-only** run (no diode) of the transient solver over an RC network must match the closed-form backward-Euler recurrence `v[n] = α·Vin + (1−α)v[n−1]`, `α = dt/(dt+RC)`, to ~1e-9 — proving the reactive discretization and history handling before any nonlinearity is trusted. (b) Each clipper's **DC steady-state** port voltage (reached by settling the transient under a DC input, caps fully charged) must match an **independent bisection root-find** of that topology's diode equation to ~1e-6, and agree with the existing static `NewtonClipper` curve. Symmetric oracle: `0 = v + R·2·Is·sinh(v/nVt) − Vin`; asymmetric and series carry their own steady-state equations (fixed in data-model).
- **Rationale.** A trusted-solver-first strategy (as `passive-tone-stacks` proved `solveAC` on closed forms before trusting it on tone stacks). The bisection oracle is genuinely independent (not solver-vs-itself), guarding against a wrong-reference/wrong-solver false agreement.
- **Alternatives considered.** Transcribing a published clipper transfer table — rejected (unverifiable in-session; same false-confidence risk `passive-tone-stacks` rejected for the Duncan rational).

## R6 — Reactive-signature measurement (OQ3, resolved 2026-07-05)

- **Decision.** Drive a **1 kHz sine** into clipping at a fixed drive; solve at `dt = 1e-5 s` (100 kHz); measure output spectral energy **above a 5 kHz cutoff**; assert it **strictly decreases** at each step of an ascending `Cf` sweep. The numeric monotonic margin (strict `<` vs a min-fractional-drop) is an implementation detail chosen to be robust to double-precision noise.
- **Rationale.** A musical fundamental with clear clipping-generated harmonics to attenuate; reuses the `component-abstractions` harness `dt = 1e-5` convention; a single scalar (HF-band energy) gives a clean monotonic-in-`Cf` assertion.
- **Alternatives considered.** Band-limited edge (rise-time/centroid) — fuzzier scalar; swept-sine chirp — heavier, less pointed (both recorded in the spec Clarifications).

## R7 — Isolation, layering, naming

- **Decision.** Builders in `core/primitives/circuit/diode-clipper/` (portable, C++17, no lab include); solver + harness in `core/labs/diode-clippers/` (host-only, C++20 OK, non-normative, with the load-bearing-boundary README). Deleting the lab leaves the primitive + Tier-1 tests building (FR-019/SC-007). Namespace `acfx::labs::diode_clippers`; harness target `acfx_lab_diode_clippers_harness`; tests `diode-clipper-builder-test.cpp` / `diode-clipper-transient-test.cpp`.
- **Rationale.** Identical to the `component-abstractions` / `passive-tone-stacks` primitive↔lab split and the acfx three-layer structure; descriptive names, no numeric prefixes.
- **Alternatives considered.** Folding the solver into the primitive — rejected (couples the primitive to a solver worldview and breaks the isolation guarantee).
