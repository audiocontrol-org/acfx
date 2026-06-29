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
