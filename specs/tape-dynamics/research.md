# Phase 0 Research: tape-dynamics

Consolidated decisions resolving the Technical Context. No `NEEDS CLARIFICATION`
markers remained after the 2026-07-03 clarify session (OQ1/OQ2/OQ4/OQ5 resolved;
OQ3 is an implementation-tuning task, not a research unknown).

## R1 — Hysteresis model: Jiles-Atherton `dM/dH`

**Decision**: Implement the Jiles-Atherton (JA) magnetic hysteresis model. State variable is the
magnetization `M`; input is the applied field `H` (the drive-scaled audio sample). The model is an ODE
in `dM/dH`:

- **Anhysteretic magnetization** `M_an(H_e) = Ms · L(H_e / a)`, where `H_e = H + α·M` is the effective
  field and `L(x) = coth(x) − 1/x` is the **Langevin function** (with the small-`x` series
  `L(x) ≈ x/3 − x³/45 …` used near 0 to avoid the `coth` singularity).
- **Irreversible + reversible split**: `dM/dH = dM_irr/dH · (…) + c · (dM_an/dH − dM_irr/dH)` with the
  standard JA form `dM_irr/dH = (M_an − M_irr) / (δ·k − α·(M_an − M_irr))`, `δ = sign(dH)`.

**Rationale**: JA is the industry-standard audio tape model (J. Chowdhury, "Real-Time Physical
Modelling for Analog Tape Machines," DAFx 2019 / CHOW Tape). It carries genuine physical parameters
and produces a true history-dependent loop — the phase's "nonlinearity with memory" lesson.

**Alternatives considered**: a simplified biased-memory waveshaper (cheaper, not physically grounded,
small teaching surface — rejected as the primary model); the Preisach relay-superposition model
(elegant, heavier state, uncommon in audio tape — rejected). Both are recorded in the design record.

## R2 — Five physical parameters + macro mapping

**Decision**: Expose `Ms` (saturation magnetization / ceiling), `a` (anhysteretic shape),
`α` (inter-domain coupling), `k` (coercivity → loop width / memory), `c` (reversibility → loop
openness) as `JAParams` on the primitive. The effect maps host macros: `drive` → input gain applied to
`H`; `saturation`/`ceiling` → `Ms`; `width` → `k`. `a`, `α`, `c` take physically sensible fixed
defaults exposed on the primitive for advanced consumers but not surfaced as first-class effect macros.

**Rationale**: keeps the effect's control surface musical while leaving the full physical parameter set
available to primitive consumers (the graduation audience). Concrete numeric ranges (OQ3) are tuned
against the harness during implementation, exactly as the `saturation` voicings were.

## R3 — Selectable solver surface (RK2 / RK4 / Newton), all three in first cut

**Decision**: A `Solver` enum with `rk2`, `rk4` (explicit Runge-Kutta), and `newtonRaphson` (implicit)
members, selectable at runtime; all three implemented in the first cut (clarified OQ1). Each integrates
one high-rate step of `dM/dH`:

- **RK2 / RK4**: explicit; 2 / 4 derivative evaluations per step; simple, cheap, conditionally stable —
  stability restored by oversampling (R4).
- **Newton-Raphson**: implicit; solves the backward-step residual with a small **bounded fixed
  iteration count** (no unbounded `while` — RT-safety, Constitution VI) plus a divergence guard (R5).
  Better stability for stiff settings at low oversampling.

**Rationale**: the explicit-vs-implicit / order-of-accuracy / stiffness tradeoff is the feature's
numerical-methods lesson (US3). A shared `dMdH(H, M, dH)` derivative function is reused by all three
solvers, so the marginal cost of shipping all three is modest.

**Alternatives considered**: RK4-only or RK4+Newton first cuts (leaner, defer the lesson) — rejected in
clarify.

## R4 — Antialiasing via the shipped `Oversampler<Factor>`; menu {2,4,8,16}, default 8

**Decision**: Run the JA integration inside `Oversampler<Factor>::process(x, evalAtHighRate)`, passing
the per-sample JA step as the `evalAtHighRate` callable — the shipped oversampler is reused **verbatim**
(no modification). The `oversampling` parameter selects among compiled `Factor` instantiations
**{2, 4, 8}**, default **8** (clarified OQ4; the shipped `Oversampler` `static_assert`s `Factor ∈ {2,4,8}`). Runtime selection dispatches to the matching
compile-time `Oversampler<Factor>` instance held by the core.

**Rationale**: a saturating nonlinearity aliases; oversampling is the antialiasing route. Crucially,
**ADAA (antiderivative antialiasing) does NOT apply here** because the nonlinearity is *stateful* — its
antiderivative is not a static function of the current sample — so the `adaa-waveshaper` technique
cannot substitute. This contrast is a key lab teaching point. Reusing the shipped oversampler avoids
new antialiasing machinery.

**Alternatives considered**: a single fixed factor (less flexible, no CPU/quality control); dropping
2× or capping at 8× (rejected in clarify — full range chosen).

## R5 — Stiff-solver stability guard (no NaN/Inf)

**Decision**: After each high-rate step, guard the state: if `M` (or an intermediate) is non-finite
(`!std::isfinite`) or exceeds a defined magnetization bound (a small multiple of `Ms`), clamp it to the
bound / reset to the last finite value — a defined, stable fallback (FR-006). Newton's iteration
additionally caps its step count and bails to the explicit estimate on non-convergence.

**Rationale**: JA is stiff; explicit solvers can diverge on a hot transient at low oversampling. The
guard makes "no NaN/Inf on any finite input" (SC-005) a hard invariant without an unbounded loop
(Constitution VI). This is an explicit defined rule, not a hidden fallback (Constitution V).

**Alternatives considered**: unconditional trust in the solver (fails SC-005 under transients);
unbounded Newton iteration (violates RT-safety) — both rejected.

## R6 — Emergent vs explicit compression (layered)

**Decision**: Emergent compression is a **measured property** of the saturating magnetics, not a
parameter (FR-012) — the level curve flattens as `drive` rises. Layered on top, an **optional** explicit
trim composes the shipped `EnvelopeFollower` (detection) + `GainComputer` (static curve) as an
envelope-driven gain stage, gated by `trim.enabled`; when disabled the signal path is **bit-exact** the
magnetics-only core (FR-011). Included in the first cut (clarified OQ2).

**Rationale**: matches the design's "both, layered" decision. Reusing the two shipped dynamics
primitives means the explicit layer is composition, not new DSP. The bit-exact-when-disabled contract
makes US6 independently testable and keeps the core path honest.

**Alternatives considered**: emergent-only (defers the layered story) or explicit-only (teaches nothing
new) — both rejected in design.

## R7 — Effect wrapper idiom + RT-safe parameter handoff

**Decision**: `TapeDynamicsEffect` mirrors the shipped `SaturationEffect`/`CompressorEffect`/`SvfEffect`
idiom: `prepare(ProcessContext)` sizes all state (including per-channel `Hysteresis` and the selected
oversampler); `process(AudioBlock)` consumes lock-free parameter edits at the top and runs the core.
Split into `tape-dynamics-core.h` / `tape-dynamics-effect.h` / `tape-dynamics-parameters.h` /
`tape-dynamics-presets.h` to keep each file within the 300–500-line budget (Constitution VII).

**Rationale**: consistency with the shipped effects; the split keeps the parameter table and the DSP
kernel readable and modular.

## R8 — Channel handling

**Decision**: Per-channel independent magnetics — one `Hysteresis` state per channel (tape is a
per-track medium). The optional explicit trim follows the sibling effects' convention; stereo-linking of
the trim detector is out of scope for this feature (the `compressor` owns the linking lesson).

**Rationale**: keeps the feature focused on the hysteresis concept; avoids duplicating the compressor's
stereo-linking surface.

## R9 — Validation reuses the shipped `host/analysis/` infrastructure

**Decision**: Reuse `host/analysis/thdn.h` (THD/aliasing), `alias-sweep.h`, and the
stimulus/spectrum/response utilities for measurement; add a loop-area computation (closed `M`-vs-`H`
polygon area) and a dynamic-range-reduction metric in the harness/tests. A dedicated
`tests/core/hysteresis-test.cpp` covers the primitive (clarified OQ5) alongside effect-level tests.

**Rationale**: measurable engineering (Constitution X) with no new measurement stack; the loop-area and
DRR metrics are the two feature-specific additions.
