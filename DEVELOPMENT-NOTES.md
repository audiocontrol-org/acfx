# Development Notes

---

## 2026-07-04: <!-- session title -->

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
- Commits: 42
  - spec(tape-dynamics): distinguish mix=0 bypass (bit-exact unity) from drive=0dB (unity input gain into always-on magnetics) per PR review; backlog OQ3 tuning + latency-PDC
  - fix(tape-dynamics): floor JA feedback denominator positive (well-posedness); correct stale primitive comments
  - fix(tape-dynamics): wire presets (FR-013), delay-compensate dry/wet, reset core on oversampling switch
  - refactor(tape-dynamics): trim hysteresis-kernel comments under audit byte-ceiling (theory in README)
  - tasks(tape-dynamics): mark all 31 tasks complete (implemented + validated)
  - feat(tape-dynamics): T028 finalize named presets
  - fix(tape-dynamics): align kMaxChannels to the effect convention (32 -> 8) — repair no-allocation/block-size crash introduced by T026 trim
  - test(tape-dynamics): T027 optional trim acceptance (E7 bit-exact off, envelope-driven GR on)
  - feat(tape-dynamics): T026 optional explicit envelope-driven trim (EnvelopeFollower+GainComputer)
  - test(tape-dynamics): T025 aliasing decreases with oversampling (SC-004)
  - test(tape-dynamics): T023 no-allocation / RT-safety acceptance (SC-007)
  - test(tape-dynamics): T021 emergent compression acceptance (SC-003, FR-012)
  - feat(tape-dynamics): T020 lab harness measurements (loop area, DRR, THD/alias)
  - test(tape-dynamics): T019 solver agreement + oversampling convergence (SC-002)
  - test(tape-dynamics): T017 US1 effect acceptance suite (loop, unity passthrough, transient finiteness)
  - feat(tape-dynamics): T016 TapeDynamicsEffect wrapper (Effect concept, param handoff, factor dispatch)
  - feat(tape-dynamics): T015 TapeDynamicsCore — Oversampler composed with JA hysteresis
  - feat(tape-dynamics): T014 TapeDynamicsParameters descriptor table
  - docs(tape-dynamics): T010 lab README — JA hysteresis theory + solver tradeoff + ADAA contrast
  - refactor(tape-dynamics): split hysteresis-test into support header + solver suite (Constitution VII file budget)
  - fix(tape-dynamics): reword hysteresis.h comment tripping portability grep
  - feat(tape-dynamics): T011 lab hysteresis kernel (graduation source)
  - docs(tape-dynamics): T013 list hysteresis primitive (first stateful nonlinear member)
  - test(tape-dynamics): T012 hysteresis loop-area + reproducibility + param-response suite
  - feat(tape-dynamics): T009 stiff-solver stability guard (FR-006)
  - feat(tape-dynamics): T008 Newton-Raphson implicit solver
  - feat(tape-dynamics): T007 RK2/RK4 explicit solvers
  - feat(tape-dynamics): T006 Jiles-Atherton dMdH derivative
  - feat(tape-dynamics): T005 Hysteresis types + class shell
  - chore(tape-dynamics): T004 extend portability gate to new paths
  - test(tape-dynamics): T003 register test skeletons
  - feat(tape-dynamics): T002 scaffold lab + harness target
  - fix(tape-dynamics): OQ4 oversampling menu 16x -> {2,4,8} (shipped Oversampler caps at Factor 8); ledger T001
  - feat(tape-dynamics): T001 scaffold effect module skeleton headers
  - tasks(tape-dynamics): declare per-task model tiers for model-sized dispatch
  - workflow(tape-dynamics): record analyze-clean (spec/plan/tasks consistent); specifying complete
  - tasks(tape-dynamics): 31 tasks across 10 phases, organized by user story (runnable)
  - plan(tape-dynamics): plan + research + data-model + contracts + quickstart; point speckit marker
  - clarify(tape-dynamics): resolve OQ1-OQ5 (all 3 solvers + explicit trim in cut 1; 2/4/8/16 default 8x; dedicated unit test)
  - spec(tape-dynamics): author feature spec from approved design record (specify)
  - workflow(tape-dynamics): design approved, advance planned -> in-flight (designing)
  - design(tape-dynamics): capstone hysteresis + emergent compression design record
- Files changed: 38
- Backlog touched: (none)

## 2026-07-03: Compressors — design → ship (PR #13) through the stack-control front door

**Goal:** Drive `design:feature/compressors` end-to-end through the front door —
from the ready roadmap frontier to a merged compressor: design record, runnable
Spec Kit spec, implementation via model-sized subagent dispatch, governance, PR,
and ship.

**Accomplished:**
- Full lifecycle: design → define → clarify → plan → tasks → analyze → execute →
  govern(override) → ship (PR #13). Roadmap node `planned → merging`.
- Full three-layer vertical: `GainComputer` graduated into
  `core/primitives/dynamics/` (second inhabitant) — compress/limit/expand/gate with
  a unified C¹ knee; `CompressorCore` composing the shipped EnvelopeFollower / SVF
  (sidechain HPF) / DelayLine (lookahead); `CompressorEffect` host wrapper
  (17-param lock-free handoff, external key, stereo linking).
- All 41 `tasks.md` tasks dispatched to fresh per-task subagents at their resolved
  model tier (2 haiku / 27 sonnet / 12 opus), ledgered.
- 351 host doctest cases green, portability gate green, no-alloc + NaN safety across
  the config sweep, lab harness emitting measurement evidence.

**Didn't Work:**
- The whole-feature `govern` audit-barrage FATAL'd on the environmental per-file
  envelope (`spec.md` 39,965 B > the 24,576 B fleet limit) — the recurring sandbox
  ceiling; could not run the cross-model barrage.

**Course Corrections:**
- Used the recorded terminal for the govern ceiling: `/code-review high` stop-gap
  (3 finder angles) → fixed 7 substantive findings (a real gate soft-knee bug that
  violated FR-007/SC-003, a sidechain OOB read, latency-report divergence, overflow
  guards, dense-id static_assert) → operator-approved `--override` recorded the
  convergence terminal.
- Reconciled an auto-makeup spec/impl mismatch: the operator's chosen closed-form
  `−computeGainDb(0 dBFS)` is a constant makeup that lifts below-threshold signal;
  corrected the contradictory "unity below threshold" spec wording rather than the
  faithful implementation.
- Third-party review response: agreed on hardening non-finite guards
  (setMakeup/setMix/setOutput); pushed back on the auto-makeup "conflict" (already
  reconciled in-spec; the review had read a stale test comment, now cleaned).

**Insights:**
- Test suites can carry blind spots that mirror the implementation: the gain-computer
  test's gate-knee C¹ scan was written around the buggy floor kink, so 351 green did
  NOT catch the gate-knee degeneracy bug — the adversarial `/code-review` finder did.
  A green suite authored against the code is not an independent check of the spec.
- "Documented divergence" between code and spec is a smell a reviewer will (rightly)
  escalate; reconcile the spec or the code, and scrub stale divergence comments —
  they outlive the divergence and mislead the next reader.
- A constant makeup gain inherently lifts below-threshold signal; "auto-makeup →
  unity below threshold" is an incorrect expectation, not a target.

**Quantitative (auto-derived from git; verify before publishing):**
- Commits: 16
  - fix(compressors): harden non-finite param guards; clarify auto-makeup reconciliation (review response)
  - fix(compressors): address /code-review findings (govern stop-gap)
  - chore(compressors): mark all 41 tasks complete (349/349 green, gate clean)
  - test(compressors): Polish — no-alloc/NaN safety + real harness (T039/T040/T041)
  - test(compressors): 7 validation suites for US1-US13 (T012-T037)
  - feat(compressors): external sidechain key + stereo linking (T032/T038)
  - feat(compressors): CompressorCore chain + CompressorEffect wrapper (T010/T011)
  - feat(compressors): graduate GainComputer primitive (T008/T009)
  - feat(compressors): Phase 1 setup — lab, skeletons, build wiring
  - tasks(compressors): dependency-ordered task list (41 tasks, 13 stories)
  - plan(compressors): Phase 0/1 design artifacts
  - clarify(compressors): resolve 4 high-impact deferred decisions
  - spec(compressors): author Spec Kit spec from approved design record
  - design(compressors): operator-approved; advance planned -> in-flight
  - design(compressors): design record for gain-computer primitive + CompressorEffect
  - chore(roadmap): close design:primitive/envelope-followers (shipped, validated)
- Files changed: 31
- Backlog touched: (none)

## 2026-07-02: Envelope followers — design → ship through the stack-control front door

**Goal:** Drive `design:primitive/envelope-followers` end-to-end through the
stack-control front door — from the ready roadmap frontier to a merge-ready
dynamics level-detector primitive: design record, runnable Spec Kit spec,
implementation via model-sized subagent dispatch, governance, and a PR.

**Accomplished:**
- Full lifecycle: design → define → clarify → plan → tasks → analyze → execute →
  govern(override) → ship (PR #12). Roadmap node `planned → merging`.
- `EnvelopeFollower` primitive at `core/primitives/dynamics/envelope-follower.h` —
  the first inhabitant of the new `dynamics/` category, graduated from its lab in
  one atomic commit. Full catalog: peak/RMS/peak-hold × branching/decoupled(+smooth)
  × linear/dB, RT-safe and allocation-free.
- All 34 `tasks.md` tasks dispatched to fresh per-task subagents at their resolved
  model tier (10 haiku / 15 sonnet / 9 opus), each reviewed, committed, ledgered.
- 293 host doctest cases green (`make test`), portability gate green, zero heap
  allocation across all 24 mode × topology × domain configs, lab harness emitting
  measurement evidence.

**Didn't Work:**
- The govern cross-model barrage was killed by the sandbox runtime ceiling (~10 min)
  before reconciling — no convergence record produced. Terminated via
  operator-approved `--override` after a `/code-review` stop-gap.
- The `agent-context` after_specify/after_plan hook could not run (PyYAML not
  importable in the `python3` env) — the CLAUDE.md SPECKIT marker was updated by hand.
- `check-prerequisites.sh` rejects the descriptive branch name (TF-09) — resolution
  goes through `.specify/feature.json` / the CLAUDE.md marker instead.

**Course Corrections:**
- Governance earned its keep: the partial barrage + the `/code-review` stop-gap caught
  **two HIGH-severity bugs** incremental per-slice testing missed — (1) decoupled+dB
  released *up* toward 0 dB (unity) instead of the −120 floor; (2) RMS silently
  degenerated to `|x|` when `setRmsWindow` was unset (zero defaults). Both fixed with
  regression coverage.
- PR review caught a third real defect: `setDomain(decibel)` without a following
  `reset()` produced a loud ~0 dB first sample (env_ in stale linear units). Fixed by
  making `setDomain` re-baseline the smoother state to the domain floor, deleting the
  unenforced ordering contract rather than documenting around it.

**Insights:**
- The bug class that slipped past per-task testing was **compositional** — mode ×
  topology × domain combinations (decoupled+dB, rms silent-degenerate) that each
  slice tested in isolation but never together. Cross-model governance and a broad
  code-review pass are exactly the nets that catch that class; single-slice tests do not.
- Model-sized subagent dispatch (haiku/sonnet/opus by declared tier) held up well:
  the opus lanes on the subtle math (coefficients, decoupled recurrence, dB
  level-independence) produced correct reference-model-pinned code; haiku handled the
  mechanical no-alloc/wiring tasks cleanly.
- Pinning implementations to an in-test reference model (the decoupled 1e-5 equality)
  is what let the domain-floor fix land safely — the linear reference stayed bit-exact
  because floor=0 makes the change a no-op in linear.

**Quantitative (auto-derived from git; verify before publishing):**
- Commits: 38
  - fix(envelope-followers): setDomain re-baselines the envelope; clean stale comments (PR review)
  - fix(envelope-followers): sensible non-zero defaults + composition coverage (code-review)
  - docs(envelope-followers): reconcile test-registration count (govern audit)
  - fix(envelope-followers): decoupled release decays toward the domain floor (govern audit)
  - tasks(envelope-followers): mark all 34 tasks complete (implemented, tested, committed)
  - impl(envelope-followers): T033 combinatorial no-alloc sweep + T034 low-fs coefficient characterization
  - impl(envelope-followers): T029 finalize lab README to match shipped primitive (US6)
  - impl(envelope-followers): T028 lab harness measurement evidence (US6)
  - impl(envelope-followers): T027 decibel-domain no-allocation coverage (SC-007)
  - impl(envelope-followers): T025+T026 decibel detection domain + -120 dBFS floor
  - impl(envelope-followers): T024 peak-hold no-allocation coverage (SC-007)
  - impl(envelope-followers): T022+T023 peak-hold detector (latch + hold + restart)
  - impl(envelope-followers): T021 decoupled+smooth no-allocation coverage (SC-007)
  - impl(envelope-followers): T018+T019+T020 decoupled + smooth-decoupled ballistics
  - impl(envelope-followers): T017 RMS no-allocation coverage (SC-007)
  - impl(envelope-followers): T015+T016 RMS detector (one-pole mean-square + sqrt) + tests
  - impl(envelope-followers): T014 no-allocation coverage for EnvelopeFollower process() (SC-007)
  - impl(envelope-followers): T010+T012 branching attack/release ballistics + timing tests
  - impl(envelope-followers): T009 US1 interface + edge-case test suite
  - chore(execute): gitignore local stack-control execute ledger
  - impl(envelope-followers): T008 graduate envelope-follower kernel into core/primitives/dynamics/ (first inhabitant)
  - impl(envelope-followers): T007 process() detect/domain/smooth dispatch skeleton
  - impl(envelope-followers): T006 coefficient math + init/reset guards (FR-013/016/018)
  - impl(envelope-followers): T004 envelope-follower test-suite section header in acfx_core_tests
  - impl(envelope-followers): T003 lab harness stub + acfx_lab_envelope_follower_harness target
  - impl(envelope-followers): T005 portability gate covers dynamics/ + envelope-follower lab
  - impl(envelope-followers): T001 lab README (ballistics theory + graduation walkthrough)
  - impl(envelope-followers): T002 EnvelopeFollower kernel skeleton (lab)
  - tasks(envelope-followers): add [tier:label] model-sized-dispatch tags
  - analyze(envelope-followers): record analyze-clean marker (specifying->implementing gate)
  - tasks(envelope-followers): dependency-ordered tasks.md (34 tasks)
  - plan(envelope-followers): plan.md + research/data-model/contracts/quickstart
  - clarify(envelope-followers): resolve 5 of 6 open questions into the spec
  - define(envelope-followers): Spec Kit spec.md + quality checklist; set spec pointer
  - design(envelope-followers): record design-approved marker
  - design(envelope-followers): design record + design pointer
  - chore(roadmap): close multi:feature/phase-nonlinear-dsp (all children closed)
  - chore(roadmap): close design:gap/harmonic-analysis (shipped, validated)
- Files changed: 27
- Backlog touched: (none)

## 2026-07-01: Saturation — design → define → execute → govern(override) → ship

**Goal:** Drive `design:feature/saturation` end-to-end through the stack-control
front door — from the ready roadmap frontier to a merge-ready production effect:
design record, runnable Spec Kit spec, 25-task implementation, governance, and PR.

**Accomplished:**
- Delivered the saturation production effect — the **first lab→effect graduation**.
  `SaturationEffect` composes the shipped `Waveshaper` (+ADAA) between two
  `SvfPrimitive` emphasis stages, with 4 voicings, a `quality` control (naive/adaa +
  reserved oversampled→adaa), lock-free atomic parameter handoff, and a dry/wet mix.
  **184/184 host tests, portability gate green**, RT-safety proven by the sentinel.
- Ran all 25 spec tasks via tier-sized subagents (haiku/sonnet/opus per `[tier:]`),
  test-first, reviewed, committed+pushed, and ledgered.
- Design→define→execute front doors clean (compass on-course each step; analyze-clean).
- Governed via operator-approved `govern --override` plus a full 8-angle `/code-review`
  as the compensating control; opened PR #9 (`saturation`→`main`), awaiting operator
  CI-green + merge.

**Didn't Work:**
- The cross-model **govern barrage could not complete in the sandbox** — killed
  ~5-7 min before reconciliation; the sonnet fleet lane times out each chunk (degraded
  2/3, floor of 2 still met). No convergence record auto-written.
- govern's **24576-byte per-file audit envelope** FATALed early on `spec.md` and the
  effect test — required trimming/splitting purely for the envelope, not code quality.

**Course Corrections:**
- Split `saturation-effect-test.cpp` (descriptor + RT files) and tightened `spec.md`
  prose to fit the audit envelope.
- Terminal via operator-approved `--override` + `/code-review` stop-gap (recorded the
  sandbox runtime ceiling to memory for next time).
- Adversarial fixes during the run: T001's empty-`.gitkeep` self-contradiction; the
  softClip/tape distinctness convergence (fixed with real tape HF-loss, not a margin
  nudge); biasedAsym→diodeCurve for ADAA throw-safety.

**Insights:**
- The compensating code review **caught a real correctness defect the incomplete
  barrage missed**: unclamped emphasis cutoffs silently collapsing to `sr/3`
  (sample-rate-dependent voicing drift) — fixed with `SvfEffect`-style clamping.
- Two saturation tests were false-confidence (tautological voicing-label check,
  vacuous cross-thread assertion) — the review made them real.
- No runtime bugs in the implementation itself; findings were doc-staleness, test
  hygiene, and the one config-vs-applied filter clamp — a good signal for the
  test-first, per-task-reviewed dispatch discipline.

**Quantitative (auto-derived from git; verify before publishing):**
- Commits: 67
  - fix(saturation): resolve code-review findings — clamp emphasis cutoffs (sr-safe), true tone=0 passthrough, de-vacuous 2 tests, de-stale RT comments
  - test(saturation): resolve govern findings — de-stale voicing comments, relabel settling tolerance, expand FR-008 to all 12 voicing-switch pairs
  - docs(saturation): resolve govern audit findings — define oversampled->adaa mapping, disambiguate output trim vs auto-makeup, CSV contract resolved, effect-test-split + T025 caveats
  - test(saturation): split effect test into descriptor + RT files under govern envelope (184/184, no assertion change)
  - docs(saturation): tighten spec prose under govern fleet envelope (no requirement change)
  - chore(saturation): mark tasks complete (T001-T024 [x], T025 [~] cross-compile env-blocked) — implementation done 184/184
  - docs(saturation): finalize harness --csv contract + FR-024 boundary + effect-composes-primitive convention (T023,T024)
  - refactor(saturation): graduate composition kernel lab->effects/saturation — first lab->effect graduation, US5 GREEN 184/184 (T022)
  - feat(saturation): lab harness + portability gate coverage (C-SAT/C-SFX) — US5 evidence, gate green (T019,T020)
  - docs(saturation): complete lab README — theory, walkthrough, composition, evidence (T021)
  - feat+test(saturation): quality control naive/adaa + reserved oversampled fallback — US4 GREEN 184/184 (T017,T018)
  - test(saturation): lock ADAA-safe voicing invariant + prove in-process param path alloc-free — US3 done 181/181 (T016)
  - feat(saturation): SaturationEffect wrapper — param table + lock-free atomic handoff, US3 179/179 (T014,T015)
  - feat(saturation): tune tape for genuine distinctness + document voicings — US2 GREEN 172/172 (T013)
  - test+feat(saturation): US2 voicing configs + distinctness suite (RED: softClip/tape 0.0185<0.02, T013 tunes tape) (T011,T012)
  - test(saturation): allocation-sentinel proof for SaturationCore::process — US1 MVP complete 167/167 (T010)
  - feat(saturation): implement SaturationCore signal chain — US1 GREEN 166/166 (T009)
  - test(saturation): US1 core + harmonics suites (RED — awaits T009/T010 impl)
  - feat(saturation): SaturationCore composition-kernel surface (T004)
  - test(saturation): add driveThdSeries + mixBalance helpers, reuse shipped harmonic/aliasing/DC measures (T006)
  - feat(saturation): voicing/quality enums + VoicingConfig surface (T005)
  - build(saturation): CMake placeholder for saturation test suites; baseline green 158/158 (T002)
  - docs(saturation): lab README skeleton, names effects/saturation graduation target (T003)
  - chore(saturation): drop empty .gitkeep dirs — honor no-empty-dir taxonomy rule (T001 review)
  - chore(saturation): scaffold lab + effect dirs; record effects taxonomy (T001)
  - roadmap(saturation): record analyze-clean marker
  - roadmap(saturation): link spec pointer to specs/saturation
  - spec(saturation): author runnable Spec Kit spec via /stack-control:define
  - design(saturation): record operator design-approved marker
  - design(saturation): design record for the composed production effect
  - chore(roadmap): close design:primitive/waveshapers (shipped, validated)
  - Merge pull request #8 from audiocontrol-org/phase-nonlinear-dsp
  - backlog(waveshapers): capture two non-blocking PR#8 review follow-ups
  - fix(waveshapers): ADAAWaveshaper::setShape re-pairs ADAA history (PR#8 review)
  - docs(waveshapers): govern round-2 hygiene — test traceability + stale comment + name disambiguation
  - chore(waveshapers): govern fix D1 — scrub deferral/TODO wording from execution ledger
  - test(waveshapers): govern fixes C1-C4 — tighten diode/softKnee/chebyshev assertions + drive rationale
  - fix(waveshapers): govern fixes B1-B7 -- honest harness output, drop stale CMake EXISTS guard, README accuracy
  - docs(waveshapers): govern fixes A1-A9 — reconcile research/plan/quickstart with implementation
  - tasks(waveshapers): mark T001-T026 complete, T027 operator-acceptance; execution ledger
  - docs(waveshapers): T026 finalize taxonomy cross-references + diode-altitude boundary
  - feat(waveshapers): T025 optional --csv harmonic-spectrum dump in harness
  - feat(waveshapers): T024 graduate kernel headers to core/primitives/nonlinear/ (US5)
  - build(waveshapers): T023 extend portability gate to waveshaping lab + nonlinear primitive locations
  - docs(waveshapers): T022 complete lab README — theory, walkthrough, measured evidence
  - feat(waveshapers): T021 host-only waveshaping harness (harmonics + naive-vs-ADAA aliasing)
  - feat(waveshapers): T020 first-order ADAAWaveshaper (US4 green)
  - feat(waveshapers): T019 antiderivatives for covered shapes + coverage predicate
  - test(waveshapers): T018 ADAA aliasing-reduction + uncovered-error + base-unchanged tests (RED)
  - feat(waveshapers): T017 wire Evaluation::lut into Waveshaper::process (US3 green)
  - feat(waveshapers): T016 WaveshaperLut fixed-size table + linear interp + edge-clamp
  - test(waveshapers): T015 LUT deviation + edge-clamp + no-alloc tests (RED: waveshaper-lut.h pending)
  - feat(waveshapers): T014 full catalog enum dispatch + no-stale-state test + diode boundary doc
  - feat(waveshapers): T013 remaining catalog shapes (arctan/algebraic/softKnee/chebyshev/diode/folds)
  - test(waveshapers): T012 per-shape analytic correctness tests (RED: 7 shapes pending)
  - feat(waveshapers): T011 gain-compensation law + RT-safety sentinel (US1 green)
  - feat(waveshapers): T010 RT-safe Waveshaper wrapper process()+staging
  - feat(waveshapers): T009 first-cut shapes tanh/hardClip/cubicSoftClip
  - test(waveshapers): T008 US1 harmonic-signature tests (RED: impl pending)
  - test(waveshapers): T007 Waveshaper wrapper tests (RED: impl pending)
  - test(waveshapers): T006 harmonic-signature/aliasing/DC measurement helpers
  - feat(waveshapers): T005 wrapper DC-blocker + gain-comp scaffolding
  - feat(waveshapers): T004 memoryless shape contract surface (declarations)
  - build(waveshapers): T002 CMake wiring for waveshaper tests + harness target
  - docs(waveshapers): T003 lab README skeleton
  - docs(waveshapers): T001 record waveshaping lab + nonlinear graduation target in taxonomy
  - tasks(waveshapers): add [tier:] model-sized-dispatch tags to all 27 tasks
- Files changed: 52
- Backlog touched: TASK-3, TASK-4

## 2026-06-30: Design + define waveshapers → runnable spec

**Goal:** Pick up the `design:primitive/waveshapers` roadmap item — the first nonlinear primitive of
`phase-nonlinear-dsp` — and drive it through the stack-control front door from a blank design to a
runnable, analyze-clean Spec Kit spec. First concept intended to walk the Theory→Lab→Primitive
graduation greenfield (SVF only proved the retroactive migration).

**Accomplished:**
- **Designed the primitive** via `/stack-control:design` (brainstorming backend, house-rules
  injected). Six operator forks settled, all toward capture-everything: altitude = **lab +
  graduated primitive**; interface = **pure `acfx::shape::*` fns + enum-selected stateful
  `Waveshaper` wrapper**; anti-aliasing = **memoryless core + opt-in ADAA, oversampling orthogonal
  sibling**; **asymmetric shapes + DC-block** included; **memoryless diode curve** here (distinct
  altitude from the circuit-solved diode-clipper later); evaluation = **closed-form + LUT as peers**.
- **Incorporated a third-party review** before `/define` — its main concern (ADAA too stateful for
  a memoryless core) plus five clarifications, all folded in: memoryless contract pinned as fixed
  with ADAA strictly layered; bias defined as a fixed post-drive offset (`shape(drive·x + bias)`);
  DC-blocker pinned to the wrapper; closed-form named the LUT's ground-truth reference; the
  oversampled comparison demoted to contingent. Operator approved; `design-to-spec` gate 7/7.
- **Defined the spec** through the native chain — specify → plan → tasks → analyze — each backend
  drive bracketed by the front-door capability marker. Produced spec.md (23 FR / 7 SC / 5
  prioritized user stories), plan.md (Constitution Check PASS, 11/11), research.md (8 mechanism
  decisions), data-model.md, contracts/waveshaper-api.md, quickstart.md, and **tasks.md (27 tasks,
  US1–US5 + polish, test-first)**.
- **Analyze clean** (0 CRITICAL / 0 HIGH, 100% requirement→task coverage); set the `spec:` pointer
  and recorded `design-approved` + `analyze-clean`, advancing the node
  `designing → specifying → implementing`. Spec is execute-ready; operator chose to **pause before
  implementation** at the runnable-spec milestone.

**Didn't Work:**
- `check-prerequisites.sh` (analyze prereq) hard-failed again on the descriptive branch name
  `phase-nonlinear-dsp` — the recurring numeric-prefix gate vs Commandment 3 (TF-09 / deskwork#511).
  Proceeded via the `feature.json`-resolved spec dir; ran analyze read-only by hand.
- The `after_specify` agent-context hook skipped: PyYAML is absent in this environment's python3, so
  `update-agent-context.sh` cannot parse its config. Updated the CLAUDE.md SPECKIT marker manually
  instead (pointer now `specs/waveshapers/plan.md`).

**Course Corrections:**
- Used the descriptive spec dir `specs/waveshapers` over the template's `NNN-` default, matching the
  existing `specs/` convention and Commandment 3 — even though `init-options.json` says
  `branch_numbering: sequential`.
- Skipped interactive `/speckit-clarify` per the operator's explicit "drive straight to tasks.md"
  choice: the approved design + incorporated review already resolved every fork, and the residual
  unknowns are deliberately-parked open questions (per-shape tolerances, ADAA order, LUT scheme).

**Insights:**
- The **memoryless/stateful contract split** was the load-bearing design decision — the external
  reviewer zeroed in on exactly it, and pinning "the base `Shape` contract is and stays memoryless;
  ADAA/DC-block/drive/bias live only in the wrapper" is what kept the primitive clean. Worth carrying
  into the oversampling + saturation siblings.
- Capturing the full transfer-function catalog while letting the **first graduated cut** be a
  planning/tasks decision (US1 = tanh/hardClip/cubicSoft, US2 = the rest) honored capture-over-YAGNI
  without bloating the MVP — the spec delivers the whole catalog across US1+US2; only order is fixed.

**Quantitative (auto-derived from git; verify before publishing):**
- Commits: 8
  - roadmap(waveshapers): record analyze-clean; spec runnable, phase=implementing
  - tasks(waveshapers): 27 tasks across US1-US5; analyze-clean (0 critical/high)
  - plan(waveshapers): impl plan + research/data-model/contracts/quickstart; constitution PASS
  - define(waveshapers): author Spec Kit spec from approved design; set spec pointer
  - design(waveshapers): operator-approved; design-to-spec gate 7/7 met
  - design(waveshapers): incorporate external review before /define
  - design(waveshapers): record nonlinear memoryless primitive design
  - chore(roadmap): close phase-digital-fundamentals and all shipped children
- Files changed: 12
- Backlog touched: (none)

## 2026-06-30: Design + define three-layer-structure → execute-ready

**Goal:** Take up the `design:gap/three-layer-structure` roadmap item and drive it through the
stack-control front door from a blank design to a runnable, analyze-clean spec — establishing the
`labs/ → primitives/ → effects/` three-layer DSP core that Constitution Principle IX declares but
that does not yet exist on disk.

**Accomplished:**
- **Designed the structure** via `/stack-control:design` (brainstorming backend, house-rules
  injected). Five operator forks settled: lab shape = **C-hybrid** (portable RT-safe kernel +
  host-only harness); taxonomy = **establish-now + migrate existing**; enforcement = **extend the
  portability gate** (lab-harness isolation + dependency direction); location = `core/labs/`;
  worked example = **SVF migrated end-to-end**. Design record written, operator-approved,
  `design-to-spec` gate 7/7.
- **Defined the spec** through the full native chain — specify → clarify → plan → tasks → analyze —
  each backend drive bracketed by the front-door capability marker. Produced spec.md (21 FR / 7 SC
  / 3 prioritized user stories), plan.md, research.md (6 mechanism decisions), data-model.md, two
  contracts (layering-rules, lab-folder), quickstart.md, and **tasks.md (31 tasks, 6 phases)**.
- **Analyze clean** (0 CRITICAL/HIGH, 100% actionable-requirement coverage); linked the `spec:`
  pointer and recorded `analyze-clean`, advancing the node `designing → specifying → implementing`.
  Node is now execute-ready (`tasks-complete` is the only remaining `implementing` exit criterion).

**Didn't Work:**
- `check-prerequisites.sh` (analyze prereq) hard-failed on the descriptive branch name
  `three-layer-structure` — the recurring numeric-prefix gate that conflicts with Commandment 3
  (already TF-09 / deskwork#511). Proceeded via the `feature.json`-resolved spec dir; no new
  friction filed.

**Course Corrections:**
- Used the descriptive spec dir `specs/three-layer-structure` over the template's `NNN-` default,
  matching the existing `specs/` convention and Commandment 3 (no numeric prefixes).
- Assessed `/speckit-clarify` as a no-op rather than forcing questions: the approved design already
  resolved every fork through structured Q&A, and the two Partial taxonomy areas are *deliberately*
  parked open-questions — clarifying them now would contradict the operator's defer decision.

**Insights:**
- The design→define handoff is clean when capture-over-YAGNI is honored: the five open-questions
  (primitive manifest, harness output contract, taxonomy boundaries, shared viz tooling, graduation
  provenance) rode through as *parked* rather than being cut or smuggled into scope.
- Sequencing the MVP as **SVF end-to-end** (US1) makes the three-layer structure copyable — every
  downstream phase gets one real lab → graduated primitive → effect to imitate, and the
  `#include`/build implications surface on day one rather than at the first new concept.
- Implementation has not started — this session is design + spec only; `/stack-control:execute`
  (model-sized dispatch of the 31 tasks + auto-govern) is the next, separately-authorized step.

**Quantitative (auto-derived from git; verify before publishing):**
- Commits: 5
  - workflow(three-layer-structure): link spec + record analyze-clean
  - tasks(three-layer-structure): 31 tasks across 6 phases, runnable
  - plan(three-layer-structure): plan + design artifacts
  - spec(three-layer-structure): author spec from approved design record
  - design(three-layer-structure): establish labs/->primitives/->effects/ core structure
- Files changed: 13
- Backlog touched: (none)

## 2026-06-30: Execute + govern + ship measurement-infrastructure

**Goal:** Take the runnable `measurement-infrastructure` spec through the rest of the
stack-control front door — analyze → execute (model-sized dispatch) → end-govern → ship —
and land it on `main`.

**Accomplished:**
- **Analyze clean.** Ran `/speckit-analyze` via the `extend` front door; the only blocking-ish
  finding (C1) was a missing FR-007 near-zero-magnitude phase→NaN test, fixed it (+ an I1 branch
  metadata nit) and recorded the `analyze-clean` marker (specifying → implementing).
- **Executed all 19 tasks** via model-sized dispatch (033) — each in a fresh subagent at its
  declared `[tier:]` model (haiku/sonnet, opus for the US1 MVP suite), reviewed, committed at
  story boundaries, durably ledgered. Built the host-side Stimulus→Effect→Analyzer→Metric harness
  (`tests/support/measurement/`) + 5 per-story doctest TUs. Grew the suite 62 → **91 passing**;
  all 8 Principle-X metrics represented.
- **End-of-feature cross-model govern** (claude+codex+sonnet): **15 HIGH findings resolved across
  5 rounds** (see `specs/measurement-infrastructure/audit-log.md`), then an operator-approved
  `--override` once substantive code converged. terminal-outcome=graduated.
- **Shipped** via PR #6 (CI green: host tests + desktop/plugin build + portability) → merged to
  `main`; fired `graduate` on the trunk so `status: shipped` is welded to the merge. Phase now
  **validating**.

**Didn't Work:**
- **Govern never converged to zero in 5 rounds.** Each round resolved the prior findings but
  surfaced ~2-3 more; the tail was *fix-induced* (AUDIT-13 from the AUDIT-11 fix; AUDIT-15 from
  AUDIT-06) and an inherent *meta-ledger self-reference* (02→05→08→14: an append-only fix-ledger
  can't self-verify its latest commit).
- **Worktree ship gap.** Graduating from the `main` worktree failed the `graduate-impl` gate
  because the govern convergence record is gitignored + per-worktree, so it didn't travel with the
  merge — had to copy it into the main worktree manually (captured to tooling-feedback).
- **CPM bootstrap not reconfigure-safe** — `cmake --preset test` over a stale `build/test` fails
  with `Unknown CMake command CPMAddPackage`; workaround `rm -rf build/test` (backlog TASK-2).
- A subagent (T013) committed/pushed despite a "do not commit" brief; tightened the instruction
  for later tasks.

**Course Corrections:**
- Split the 772-line `measurement-test.cpp` into 5 per-story TUs + a shared header to satisfy the
  repo's hard 500-line portability budget (the size gate applies to all files, not just headers).
- **Redesigned the stability denormal check** from passthrough (subnormal input) to *generation*
  (normal step → silence, scan the decay tail) and stopped `isClean` rejecting bounded subnormals,
  so correct passthrough effects are no longer false-flagged (AUDIT-12).
- Adopted NaN sentinels for *unmeasurable* (thd with no fundamental / harmonics above Nyquist;
  relativeExecTime with blockSize ≤ 0) instead of misleading 0.0 / silent clamps (Constitution V).
- Made the denormal test FPU-mode-independent via a stored-subnormal stub rather than asserting the
  real SVF's environment-conditional behavior (AUDIT-09).

**Insights:**
- The govern loop has a **myopic-convergence tail**: past the real bugs, fixes generate fresh
  surfaces and the meta-ledger self-references. Operator-approved `--override` is the sanctioned
  terminal once substantive code is converged — captured as a memory for next time.
- The harness did its job as *measurable engineering*: it surfaced a genuine, in-scope-to-defer
  limitation — the DaisySP SVF doesn't flush denormals (backlog TASK-1) — that a non-measuring
  workflow would have missed.
- Worktrees + gitignored per-worktree convergence records interact badly with the trunk-side
  `graduate` step; worth fixing upstream so the record travels or is re-resolvable.

**Quantitative (auto-derived from git; verify before publishing):**
- Commits: 19
  - workflow(graduate): design:feature/measurement-infrastructure merging -> validating
  - Merge pull request #6 from audiocontrol-org/measurement-infrastructure
  - fix(measurement-infrastructure): guard relativeExecTime against non-positive blockSize
  - fix(measurement-infrastructure): resolve end-govern round-5 findings AUDIT-13..15
  - fix(measurement-infrastructure): resolve end-govern round-4 findings AUDIT-11..12
  - fix(measurement-infrastructure): resolve end-govern round-3 findings AUDIT-08..10
  - fix(measurement-infrastructure): resolve end-govern round-2 findings AUDIT-05..07
  - fix(measurement-infrastructure): resolve end-govern findings AUDIT-01..04
  - chore(measurement-infrastructure): mark T001-T019 complete (implemented, reviewed, committed)
  - chore(measurement-infrastructure): execute ledger (T001-T019) + backlog finds (SVF denormals, CPM reconfigure)
  - refactor(measurement-infrastructure): split measurement tests by user story to satisfy 500-line portability budget (T017/T018)
  - impl(measurement-infrastructure): US4 — opt-in CSV report + emission on/off tests (T014-T015)
  - test(measurement-infrastructure): T013 US3 stability/allocation/exec-time tests
  - impl(measurement-infrastructure): US2 — correlation analyzer, THD + latency metrics, distortion/delay tests (T008-T010)
  - impl(measurement-infrastructure): US1 MVP — impulse/Goertzel analyzers, response metrics, effect-agnostic tests (T005-T007)
  - impl(measurement-infrastructure): Foundational — stimulus generators + capture seam + generator tests (T002-T004)
  - impl(measurement-infrastructure): T001 scaffold harness dir + register measurement test TU
  - chore(measurement-infrastructure): record analyze-clean marker (specifying -> implementing)
  - refine(measurement-infrastructure): close analyze findings C1/I1
- Files changed: 20
- Backlog touched: (none)

## 2026-06-29: Close program-scaffolding; design + define measurement-infrastructure (Phase 1)

**Goal:** Close out the shipped `program-scaffolding` governance feature, clean up the merged
branches/worktrees, and stand up the first concrete Progressive-DSP sub-project —
`measurement-infrastructure` — through the stack-control front door, left runnable for
execution next session.

**Accomplished:**
- **Closed `program-scaffolding`** — recorded the `validated` marker and advanced it to the
  terminal `closed` phase; removed the merged `modulated-delay` worktree and deleted three
  merged branches (`program-scaffolding`, `fix-discrete-param-labels`, `modulated-delay`)
  locally + on the remote.
- **Designed `measurement-infrastructure`** via `/stack-control:design` — wrote the design
  record (Stimulus→Effect→Analyzer→Metric harness, Principle X), circulated it, and folded an
  external "approve with minor revisions" review into it (Goertzel+sine-sweep, assertions+CSV,
  separated stimulus/analyzer/metric, silence/DC/denormal/idle checks, relative-exec-time rename).
- **Defined it** via `/stack-control:define` — full chain (specify→clarify→plan→checklist→
  tasks→analyze), runnable spec linked to the node.
- **Adopted the new model-sized-dispatch execute protocol (033)** — added a `tier_map` to
  `.stack-control/config.yaml` and `[tier:]` tags to all 19 tasks; `resolve-tiers` resolves
  every task to a model.
- **Incorporated a second third-party spec review's clarifications** (phase semantics, canonical
  CSV schema in the contract, educational-reuse rationale, analyzer/metric reinforcement).
- **Set up the feature branch + worktree** (`acfx-work/measurement-infrastructure`), baseline
  `make test` 62/62 green. **Execution deferred to next session.**

**Didn't Work:**
- **`govern --mode implement` could not converge** on the docs-heavy `program-scaffolding`
  diff — killed 3× by the environment's per-command time cap with no resume; closed via a
  documented `--override` after the audit had run and all findings were fixed.
- The **speckit agent-context update script needs PyYAML** (absent here), so the `CLAUDE.md`
  SPECKIT marker was hand-edited each plan.

**Course Corrections:**
- The compass **refused `define` before `design`** (node `planned`, `designing` skipped) — pivoted
  to `/stack-control:design` first, which is the correct rail (and the right home for the
  open decisions).
- **Pushed back on the spec review** rather than rubber-stamping: 3 of 5 items were already
  handled; put the canonical CSV schema in `contracts/metrics.md`, not the spec body.
- The cross-model audit caught that **`measurement-infrastructure` was ungated** despite being
  the stated Phase-2 enabler — added the `phase-nonlinear-dsp depends-on measurement-infrastructure`
  edge so `roadmap next` reflects the real frontier.

**Insights:**
- The new execute is **all-or-nothing on tiers** — every task needs `[tier:]` and the
  installation needs a `tier_map`, or it dispatches nothing (no silent session-default).
- **`govern` doesn't fit a hard per-command time cap** on large docs diffs (no chunk-resume);
  `--override` is the documented escape, but a resumable govern is the real fix (upstream-worthy).
- The audit earned its cost on a *substantive* diff (caught the ungated enabler + a present-tense
  constitution overclaim), even though convergence couldn't be persisted here.

**Quantitative (auto-derived from git; verify before publishing):**
- Commits: 7
  - spec(measurement-infrastructure): incorporate third-party review clarifications
  - chore(measurement-infrastructure): adopt model-sized-dispatch execute (033)
  - define(measurement-infrastructure): author spec/plan/tasks from the approved design
  - design(measurement-infrastructure): record design-approved; rename Decisions heading for the exit gate
  - design(measurement-infrastructure): incorporate external design review
  - design(measurement-infrastructure): host-side Measurable Engineering harness design record
  - chore(roadmap): close design:feature/program-scaffolding (validated; terminal)
- Files changed: 16
- Backlog touched: (none)

## 2026-06-28: Run the SVF as a DAW plugin (AU + VST3); file tooling-feedback issues

**Goal:** Get the shipped SVF effect loading as a DAW plugin (AU + VST3), and file the two
upstream govern/lifecycle defects surfaced during the workbench feature.

**Accomplished:**
- **Filed the two upstream-tool issues** against `audiocontrol-org/deskwork` (operator-approved):
  **#513** — govern audits its own artifacts (the convergence record in `--diff-base` becomes a
  recursive finding); **#514** — need an operator-owned-pending task state so `tasks-complete`
  doesn't force `[X]` on unrun manual-acceptance tasks.
- **Built + installed all three plugin formats** (VST3 / AU / CLAP — the SVF is `acfx_core` +
  `acfx_host` only, untouched by the workbench feature) into the user plugin folders, **signed
  with the Developer ID** (`ES3R29MZ5A`) + hardened runtime.
- **SVF loads and runs in Logic as an AU** (operator-confirmed). **VST3** is signed + installed
  and ready in any VST3 host.

**Didn't Work:**
- **`auval` was a dead end on macOS Sequoia 15.7.** It refused to register/validate the AU
  (`didn't find the component` / version -50) through every standard fix — AU cache clear,
  `AudioComponentRegistrar` bounce, a full `coreaudiod` bounce, ad-hoc → Developer-ID re-sign,
  xattr strip — even though the bundle was valid (arm64, signed, correct `aufx/Asvf/Acfx`
  Info.plist, `com.acfx.acfx_plugin` id). Yet **Logic loaded it fine.** Burned a lot of
  diagnosis on a CLI gate that doesn't reflect what the DAW actually does.
- Initial install was **ad-hoc-signed → Gatekeeper-rejected** (`spctl`), which is what sent me
  down the (ultimately unnecessary) registration rabbit hole.

**Course Corrections:**
- Stopped treating `auval` as authoritative once the operator confirmed Logic loads the AU —
  the **DAW is the real acceptance test** here, not the CLI validator.

**Insights:**
- For **local** DAW use a Developer-ID signature is plenty; **notarization is only needed to
  distribute to other Macs** (Gatekeeper-on-download). Don't conflate the two.
- On this machine `auval` is not a reliable AU gate — verify plugins in the actual host.
- **Dev-ergonomics gap:** the plugin builds ad-hoc with `COPY_PLUGIN_AFTER_BUILD=FALSE` and no
  signing identity, so every rebuild needs a manual install + Developer-ID re-sign. Candidate:
  wire Developer-ID signing + auto-copy into `adapters/plugin/CMakeLists.txt`.

**Quantitative:**
- Repo commits this session: 0 (work was outside the tree — plugin build/install/sign on the
  machine + two external GitHub issues).
- External artifacts: deskwork **#513**, **#514**; signed VST3/AU/CLAP installed under
  `~/Library/Audio/Plug-Ins/`.
- Backlog touched: (none)

## 2026-06-27: Drive workbench-audio-config implement → govern → ship → close; fix macOS live input

**Goal:** Take the runnable `workbench-audio-config` spec all the way through the
stack-control front door — analyze → execute (implement + govern) → ship → close — and
make the workbench actually usable for live input during manual acceptance.

**Accomplished:**
- **Analyze + execute via the front door.** `/stack-control:extend` ran `/speckit-analyze`
  (0 critical/high, 3 mediums); `/stack-control:execute` drove native `/speckit-implement`
  over all 19 tasks (US1–US4): `AudioSettingsWindow` over JUCE's device selector, the
  Live/File source bar with async chooser, persistence via `ApplicationProperties`, explicit
  MIDI selection, and the JUCE-free `SourceConfig` serde seam (written test-first). The
  audio-stopped reconfigure invariant (prepareToPlay = single reconfigure point) holds.
  **17/17 host tests green**; workbench compile-verified against real JUCE. Committed + pushed
  at every task boundary.
- **Whole-feature governance** (cross-model: claude + codex + sonnet). 3 rounds (4→2→3
  findings). Fixed every real code defect: saved-device-preference clobber on fallback,
  unsurfaced missing-input-device, ignored decode-failure, file-chooser/decoder format drift,
  and missing-saved-file → muted (now surfaced live fallback). Dispositioned the residual
  (manual-acceptance representation + a govern-chunking artifact) in the audit-log. Converged
  by **documented `--override`** → `terminal-outcome=graduated`.
- **Shipped.** `/stack-control:ship`: PR #1 `platform-foundation → main`, CI green
  (portability + host tests + desktop/plugin build), merged, `status: shipped` recorded by the
  welded `graduate`.
- **Fixed macOS live input** (found in manual acceptance): root-caused to a missing
  `NSMicrophoneUsageDescription` (TCC silently zeroed input); enabled JUCE
  `MICROPHONE_PERMISSION_ENABLED`. Added a `LevelMeter` (RT-safe atomics + timer) and a
  `FileLogger` config/peak log (`~/Library/Logs/acfx/acfx-workbench.log`) for observability —
  the workbench had none. Operator verified input → filter → output works.
- **Closed.** `/stack-control:close`: recorded the `validated` marker, advanced the roadmap
  node to the terminal `closed` phase. Full lifecycle complete.

**Didn't Work:**
- **Live input was silent on first launch** and the workbench had **no meters or logging**, so
  there was no way to tell whether audio was arriving — flew blind until the mic-permission
  root cause + observability landed.
- **Govern audited its own output.** Committing govern artifacts (`audit-runs/`,
  `govern/convergence/`) into the tree meant the next barrage (with `--diff-base` spanning
  them) flagged govern's own convergence record as showing unresolved highs (AUDIT-05) — a
  recursion that can't converge until the artifacts leave the diff.
- **Manual-acceptance `[X]` gate-gaming recurred every govern round** (AUDIT-03 → -07): the
  `tasks-complete` gate only accepts `[X]`, but the barrage (correctly) flags marking unrun
  interactive scenarios as done. No code fix resolves it — structural.
- **CMake in-place reconfigure** after a `CMakeLists` change repeatedly failed with
  `Unknown CMake command CPMAddPackage`; needed `rm -rf build/<preset>` + a clean configure.

**Course Corrections:**
- Operator authorized marking the interactive Scenarios B–F `[X]` (with a prominent banner)
  so the `tasks-complete` gate could audit the committed code — manual acceptance stays
  operator-owned before graduation.
- At ship, operator accepted the post-govern mic-fix commit (`3b9281b`) **as-is / ungoverned**
  (RT-safe by construction, builds + tests green, live-verified) — documented exception.

**Insights:**
- A macOS standalone app that opens audio input **must** declare `NSMicrophoneUsageDescription`
  or TCC silently denies/zeros the input (output needs no permission — hence "output works,
  input doesn't"). Now baked into the workbench `CMakeLists.txt`.
- Governance must **exclude its own `.stack-control/` artifacts** from the audited diff, else
  the barrage recursively finds its own convergence record.
- The lifecycle needs an **operator-owned-pending** task state distinct from done, so
  manual-acceptance tasks don't have to be forced to `[X]`.

**Quantitative (corrected — session boundary b561b3e..HEAD; the auto-derived merge-base
boundary undercounted to 1 after the mid-session merge to main):**
- Commits: 16 (T001–T019 across US1–US4, 3 govern-fix commits, mic-fix + observability,
  graduate, close, session-end)
- Files changed: ~21 (+1071 / −53) across `adapters/workbench/`, `tests/`, `specs/`,
  `README.md`, `.github/`, `.gitignore`
- New workbench units: `audio-settings`, `source-bar`, `workbench-settings` (serde),
  `workbench-persistence`, `level-meter`
- Backlog touched: (none)
- Lifecycle: `workbench-audio-config` specifying → implementing → governing → merging →
  validating → **closed**

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
workflow(graduate): design:feature/program-scaffolding merging -> validating
workflow(graduate): design:feature/measurement-infrastructure merging -> validating
workflow(graduate): design:gap/three-layer-structure merging -> validating
workflow(graduate): design:gap/harmonic-analysis merging -> validating
workflow(graduate): design:primitive/envelope-followers merging -> validating
workflow(graduate): design:feature/compressors merging -> validating
workflow(graduate): design:feature/program-dependent-saturation merging -> validating
