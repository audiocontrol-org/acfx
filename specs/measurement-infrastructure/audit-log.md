---
slug: measurement-infrastructure
targetVersion: ""
---

# Audit log — measurement-infrastructure

## 2026-06-29 — audit-barrage lift (end-govern-after_implement)

### AUDIT-20260629-01 — CsvReport throws `std::runtime_error` without including `<stdexcept>`

Finding-ID: AUDIT-20260629-01
Status:     resolved (fa-fixup) — added `#include <stdexcept>` to report.h; no longer relies on a transitive include
Severity:   high
Per-lane:   codex=high
Decision:   adjudicated (gate-counted high) — blast-radius=unstated, reachability=unstated, fix-debt=no; no down-calibration signal — high retained.
Surface:    tests/support/measurement/report.h:31-33,82-87

`CsvReport::write()` throws `std::runtime_error`, but the header only includes `<fstream>`, `<string>`, and `<vector>`. `std::runtime_error` is declared by `<stdexcept>`; relying on transitive standard-library includes is non-portable and can break any translation unit that includes this header under a different STL/version/include order.

The blast radius is high because this is a header-only support API: a downstream test or lab harness can fail at compile time simply by including `tests/support/measurement/report.h`. The fix is to add `#include <stdexcept>` beside the other standard headers.

### AUDIT-20260629-02 — `reviewClean:true` is an unfalsifiable assertion — no verification provenance recorded

Finding-ID: AUDIT-20260629-02 (claude-01 + claude-02 + claude-02; cross-model)
Status:     resolved (fa-fixup) — ledger rows now carry reviewedTreeSha + reviewMethod + reviewRef (the latter points to this cross-model audit), anchoring reviewClean to retrievable evidence
Severity:   high
Per-lane:   claude=high, sonnet=low
Decision:   adjudicated (gate-counted high) — blast-radius=high, reachability=unstated, fix-debt=no; no down-calibration signal — high retained.
Surface:    .stack-control/execute/measurement-infrastructure.ledger.jsonl:1-19

Every one of the 19 ledger rows asserts `"reviewClean":true`, but each record carries only `{id, tierLabel, model, commitRange, reviewClean}` (plus a freeform `note` on four rows). There is no reviewer identity, no timestamp, no tree SHA that was reviewed, and no pointer to the review output (audit-run id, finding-log path, or report). The `commitRange` field names the *implementation* commit, not the artifact that proves a review happened. This means the load-bearing claim of the entire ledger — "this task was reviewed and came back clean" — is unverifiable by any downstream consumer; it can only be trusted, not checked.

Blast radius: `stack-control:ship` gates on a "govern-converged precondition" and the roadmap/close machinery reconciles against this kind of execute artifact. An unattended agent (or `ship`) reading `reviewClean:true` will treat the feature as audited-clean and proceed to PR/merge, with nothing in the artifact to corroborate the review ever ran or what it examined. A reasonable fix: add a `reviewedTreeSha` (the commit the review actually ran against) and a `reviewRef` (audit-run id or report path) per row, so `reviewClean:true` is anchored to retrievable evidence rather than self-attestation.

### AUDIT-20260629-03 — NoiseGenerator "same seed" test reuses one instance across two fill() calls, locking in per-call re-seeding as the contract

Finding-ID: AUDIT-20260629-03 (claude-03 + claude-01; cross-model)
Status:     resolved (fa-fixup) — the "same seed" test now uses two separate generator instances, asserting determinism-from-seed (mirrors the sibling test)
Severity:   high
Per-lane:   claude=low, sonnet=high
Decision:   adjudicated (gate-counted high) — blast-radius=high, reachability=unstated, fix-debt=no; no down-calibration signal — high retained.
Surface:    tests/core/measurement-stimulus-test.cpp:70-91 (NoiseGenerator: same seed produces identical buffers)

The "different seeds produce different sequences" test (lines 112-134) correctly uses two separate generator instances. But the "same seed produces identical buffers" test (lines 70-91) uses a *single* `gen` instance and calls `gen.fill(buf1)` then `gen.fill(buf2)`, asserting `buf1[i] == buf2[i]`. That asserts more than the test name states: it requires that `fill()` re-seeds from `seed` on every call rather than advancing internal RNG state. A perfectly reasonable noise-generator design produces *continuous* (non-repeating) noise across successive `fill()` calls so adjacent capture blocks don't repeat — and that design would fail this test even though it is fully deterministic from the seed.

Blast radius is contained (it's a test, and per-call re-seed is a defensible choice for single-buffer stimulus generation), so this is low. But the inconsistency with the sibling test is a smell, and the test silently pins a behavioral decision the spec may not intend. If per-call re-seed *is* the contract, state it; if determinism-from-seed is the actual contract, mirror the sibling test and use two instances with the same seed so the test asserts exactly its name.

### AUDIT-20260629-04 — Correlation latency misses polarity-inverted delays

Finding-ID: AUDIT-20260629-04
Status:     resolved (fa-fixup) — lagSamples now selects by correlation MAGNITUDE (detects polarity-inverted delays); added a polarity-inverted latency fixture
Severity:   high
Per-lane:   codex=high
Decision:   adjudicated (gate-counted high) — blast-radius=unstated, reachability=unstated, fix-debt=no; no down-calibration signal — high retained.
Surface:    tests/support/measurement/analyzers.h:203-218

`CorrelationAnalyzer::lagSamples()` selects the lag with the largest signed correlation. For a delayed but polarity-inverted output, such as `out[n] = -in[n - D]`, the true delay lag has a negative peak, while lag 0 can have correlation `0` and therefore wins. With an impulse input, `corr(D) == -1` and `corr(0) == 0`, so the analyzer reports `0` instead of `D`.

This matters because `latencySamples()` is the reusable FR-009 latency metric, not just a narrow test helper. A downstream consumer measuring an inverting filter, all-pass stage, or any processor with a 180-degree polarity flip would get a quietly plausible but wrong latency. The fix should choose the strongest correlation magnitude for latency, while preserving sign separately only if a caller needs polarity information.

## 2026-06-29 — audit-barrage lift (end-govern-after_implement)

### AUDIT-20260629-05 — `reviewRef` points at a non-existent audit-log anchor

Finding-ID: AUDIT-20260629-05
Status:     resolved (fa-fixup2) — reviewRef now points at the real convergence record `.stack-control/govern/convergence/impl__design-feature-measurement-infrastructure.json` (a concrete, existing artifact) instead of a non-existent heading anchor
Severity:   high
Per-lane:   codex=high
Decision:   adjudicated (gate-counted high) — blast-radius=high, reachability=unstated, fix-debt=no; no down-calibration signal — high retained.
Surface:    .stack-control/execute/measurement-infrastructure.ledger.jsonl:1-19

All 19 ledger rows now add `reviewRef:"specs/measurement-infrastructure/audit-log.md#end-govern-2026-06-29"` as the retrievable evidence for `reviewClean:true`, but that anchor does not exist in the target file. The audit log’s relevant heading is `## 2026-06-29 — audit-barrage lift (end-govern-after_implement)` at `specs/measurement-infrastructure/audit-log.md:8`, and `rg` finds `end-govern-2026-06-29` only in the ledger rows, not as a heading or explicit anchor in the audit log.

This is a fresh defect in the fix for the prior provenance finding: the ledger no longer merely lacks evidence, it records a broken evidence pointer. Blast radius is high because this field is the mechanism that makes `reviewClean:true` verifiable for downstream governance or an unattended ship/reconciliation consumer; acting on the artifact as written either fails to resolve the review evidence or silently accepts an unverifiable clean claim. A reasonable fix is to point `reviewRef` at an actual stable target, such as a real heading anchor, a concrete audit-run report path, or an explicit named anchor added to the audit log.

### AUDIT-20260629-06 — `thd()` silently returns 0.0 in unmeasurable cases — a dead or out-of-band effect reads as "linear"

Finding-ID: AUDIT-20260629-06
Status:     resolved (fa-fixup2) — thd() now returns NaN (not 0.0) when the fundamental is unmeasurable (v1<eps) or when no harmonic falls below Nyquist (measured==0); added two fixtures (dead effect, out-of-band fundamental)
Severity:   high
Per-lane:   claude=high
Decision:   single-model (gate-counted high)
Surface:    tests/support/measurement/metrics.h — `thd()`, the `if (v1 < kEpsilon) return 0.0;` guard and the `if (harmHz >= nyquist) break;` loop exit (≈ lines 185–225)

`thd()` returns `0.0` in two distinct "couldn't measure" situations, and `0.0` is indistinguishable from "the effect is perfectly linear":

1. **Fundamental absent** — `if (v1 < kEpsilon) return 0.0;`. If the effect under test outputs silence (broken, not wired, crashed reset, wrong channel), `v1≈0` and `thd` reports `0.0`. A test written as `EXPECT_LT(thd(out, f, sr), 0.05)` then *passes for a completely dead effect*. The most common real failure mode (no output) is scored as the best possible distortion result.

2. **No harmonics in band** — for a high fundamental (e.g. 15 kHz at 44.1 kHz), the 2nd harmonic (30 kHz) already exceeds Nyquist, the loop `break`s on the first iteration, `sumSq` stays `0`, and the function returns `0.0/v1 = 0.0`. A grossly nonlinear effect reads as zero THD purely because its harmonics fell above Nyquist.

This is the worst place for a silent fallback — the project commandments forbid fallbacks that hide failure modes precisely because they are bug-factories, and here the fallback lives in the *measurement* harness, so it makes broken effects look correct. The blast radius: every downstream THD assertion silently loses its ability to catch a dead/no-output effect or an unmeasurable-band configuration. A reasonable fix is to return a sentinel that asserts loudly (NaN, mirroring `phaseRad`'s floor guard) or to expose the harmonic count actually summed so callers can distinguish "linear" from "nothing to measure," rather than collapsing both onto `0.0`.

### AUDIT-20260629-07 — Stability tests substitute synthetic stubs for the real SVF, leaving FR-012 untested against any real effect and the known SVF denormal failure unguarded

Finding-ID: AUDIT-20260629-07
Status:     resolved (fa-fixup2) — added an executable guard asserting stability(svf).ok==false with failedCase=="denormal" (the known DaisySP SVF limitation, backlog TASK-1), so it is enforced executably rather than living only in a comment
Severity:   high
Per-lane:   claude=high
Decision:   single-model (gate-counted high)
Surface:    tests/core/measurement-stability-test.cpp:6-15, 39-86, 88-104

The header NOTE (lines 6-15) concedes that the real `SvfEffect` *fails* the harness's denormal stability case, then resolves this by swapping in two in-test stubs — `CleanFx` (a denormal-flushing passthrough, lines 39-58) and `BrokenFx` (writes NaN, lines 60-72) — for the two stability cases (tests 1 and 2, lines 88-104, 106-120). The result is that the entire US3 stability surface (FR-012) is validated **only** against synthetic stubs purpose-built to pass/fail. No real effect is ever exercised by `stability()`. The clean-stub test proves the verdict returns `{true,nullptr}` for a passthrough that flushes subnormals; the broken-stub test proves NaN is caught. Neither proves any *shipped* effect is numerically stable.

The blast radius is that a downstream consumer (or an unattended agent) reading this suite concludes FR-012 stability is validated for the effect library, when in fact the one real effect available (`SvfEffect`) is known to fail the denormal case and that failure is captured **only in a source comment** — there is no `xfail`/expected-failure assertion, no executable guard. If the SVF were later "fixed" (or broken in a new way), nothing in the suite would detect the regression, and the comment's claim ("correctly caught by the harness") is itself never executed. A reasonable fix: add an explicit test that asserts `stability(svf, ctx).ok == false` with `failedCase` naming the denormal case, so the known limitation is enforced executably and a future flush-to-zero fix forces the test to be updated (turning the silent comment into a real guard).

---
