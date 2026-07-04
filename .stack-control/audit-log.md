# Audit Log

Durable record of audit findings + their dispositions. Status values: `open` → `fixed-<sha>` → `verified-<date>`, or `acknowledged-<date>` with a substantive reason.

---

## component-abstractions — govern --mode implement (2026-07-04)

The whole-feature cross-model barrage (claude + codex + sonnet lanes) was killed by the
sandbox runtime ceiling before final reconciliation, but four chunks completed and their
per-model outputs live under `.stack-control/audit-runs/*-after_implement/`. Dispositions:

- **AUDIT-BARRAGE-01 (HIGH — corroborated by claude AND codex independently): `Netlist::prepare()`
  did not range-check component terminal node-ids.** A terminal in `[nodeCount_, MaxNodes)`
  read a value-initialized union-find slot (== 0) and silently united with ground, so a
  malformed/floating netlist passed validation; a terminal `>= MaxNodes` was out-of-bounds UB.
  `node.h::isValidNode` existed for exactly this and was unused. → **fixed**: added Check 0 as the
  first step of `prepare()` (validates every terminal via `isValidNode` before any `parent[]`
  indexing), plus two regression tests (`tests/core/circuit-netlist-test.cpp`) covering both the
  in-`[nodeCount,MaxNodes)` silent-grounding channel and the `>= MaxNodes` OOB channel. Verified:
  the 18 circuit test cases pass; the two new cases throw a distinct descriptive
  `std::invalid_argument`.

- **OQ5 host-proxy vs on-device (MEDIUM):** already addressed — `research.md`'s OQ5 note states
  plainly that the Teensy on-device measurement is OUTSTANDING (toolchain unavailable) and only a
  host `-Os` proxy was run. `acknowledged` — no overclaim remains.

- **Teensy `-fno-exceptions` vs control-path `throw` in netlist (MEDIUM):** `acknowledged` — the
  `prepare()`/`add()` throws are control-thread (build-time) validation, matching the already-
  accepted precedent in `core/primitives/nonlinear/adaa-waveshaper.h`; consistent with repo policy,
  not a new regression. On-device embedded build strategy is a program-wide concern, not a
  component-abstractions defect.

- **C++17-gate / platform-header grep heuristics in `check-portability.sh` (LOW):** `acknowledged`
  — these are the pre-existing sibling-gate patterns (substring grep), consistent across the file,
  and the primitive headers under audit are in fact clean. Not a regression introduced here.

- **Pre-existing flaky failure surfaced (NOT a component-abstractions finding):** 3
  `saturation-voicings-test` cases fail only inside the full `acfx_core_tests` run, pass in
  isolation; confirmed present with the component-abstractions netlist change stashed. Captured as
  backlog `TASK-10` (found-mid-work bug in a different feature), not scope-crept here.

Terminal note: the barrage cannot fully reconcile in this sandbox (runtime ceiling; `sonnet` lane
times out → degraded fleet on some chunks). Substantive finding fixed above; remaining items are
acknowledged with reasons. Operator-approved `--override` is the sanctioned terminal for the
convergence record (per the govern-convergence-tail discipline).

### /code-review stop-gap (2026-07-04, operator-selected)

Ran the lighter multi-agent `/code-review` over the whole-feature diff (`77b256f..HEAD`) as the
operator-chosen stop-gap for the un-reconcilable barrage. Three finder angles (primitive
correctness, solver correctness, cleanup/altitude/conventions), each independent:

- **Primitive-correctness finder: 0 findings.** Verified all physics matches references exactly
  (admittance, C/L companions incl. the relative sign, Shockley current/conductance, vCrit, pnjlim);
  independently CONFIRMED the AUDIT-BARRAGE-01 Check-0 fix closes the silent-grounding gap with no
  new gap (negative ids, `== nodeCount`, `>= MaxNodes`, ground-only all handled); union-find + path
  halving correct.
- **Solver-correctness finder: 1 LOW.** `NewtonClipper` validated `maxIterations >= 1` but not
  `voltageTol/currentTol > 0`; a non-positive tolerance silently made a well-posed clipper report
  permanent non-convergence. → **fixed** (throw at construction + regression test). It otherwise
  cleared every subtle hazard: Geq→0 handled (stamps 0 conductance, prepare() guarantees a non-diode
  path to ground so the matrix stays non-singular), Geq→∞ unreachable (pnjlim bounds the bias),
  companion RHS signs, fixed-node reduction (both grounded cases), Gaussian elim/pivoting/back-sub,
  reactive-history order, refusal logic, and array bounds — all verified correct.
- **Cleanup finder: 0 correctness, 0 CLAUDE.md violations.** Three trivial simplifications
  applied (redundant convergence branch, unused `stampComponents` param, discarded return); two
  efficiency items (per-iteration augmented-netlist rebuild; one redundant `dt/L` divide) accepted
  as documented non-normative-lab tradeoffs. Confirmed correct reuse of `acfx::span`; files within
  size budget; no fallbacks; no AI attribution.

Net: the stop-gap surfaced one LOW (fixed) and independently corroborated the barrage's HIGH fix.
No confirmed correctness defect remains in the feature.
