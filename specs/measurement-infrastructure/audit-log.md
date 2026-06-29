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

## 2026-06-29 — audit-barrage lift (end-govern-after_implement)

### AUDIT-20260629-08 — Ledger asserts 19/19 review-clean but the post-govern audit-fix commits are unledgered

Finding-ID: AUDIT-20260629-08 (claude-02 + claude-03 + codex-01 + claude-01; cross-model)
Status:     resolved (fa-fixup3) — ledger gained GOVERN-FIX-round1/2/3 rows whose reviewedTreeSha matches the post-fix trees c925a75/bcaab5e (round3=self by necessity); each fix round is independently re-audited by the next govern pass
Severity:   high
Per-lane:   claude=high, codex=high, sonnet=medium
Decision:   agreement (gate-counted high)
Surface:    .stack-control/execute/measurement-infrastructure.ledger.jsonl:14-19 (and the ledger as a whole)

The ledger's most-recent rows (T016–T019) pin `reviewedTreeSha`/`commitRange` to `fa3ce69`, and *every* row carries `reviewClean:true`. But the audited commit log shows three commits landed after `fa3ce69`: `31b437c` (mark T001-T019 complete), `c925a75` (resolve AUDIT-01..04), and `bcaab5e` (resolve AUDIT-05..07). The diff under audit is the *complete* new-file content — exactly 19 rows — so the ledger was never extended to record review of those audit-driven fixes. The govern pass found AUDIT-01..07 in this very feature, the fixes shipped, and nothing in the ledger covers them.

This is the round-0 self-red-team failure mode directly: a fix is a fresh surface and must be reviewed in its own right, yet the convergence ledger still reads "fully clean" for code that has since changed twice. Blast radius is high — a consumer (or unattended agent) reading this ledger concludes the feature is review-clean at HEAD, when the most recently modified lines (the AUDIT-01..07 resolutions) have *no* review-of-record here. The ledger should gain rows (or amended rows) whose `reviewedTreeSha` matches the post-fix trees `c925a75`/`bcaab5e`, or it should not be presented as the authoritative convergence record for the shipped feature.

---

### AUDIT-20260629-09 — SVF denormal-guard test asserts a build/hardware-flag-dependent behavior as an invariant

Finding-ID: AUDIT-20260629-09
Status:     resolved (fa-fixup3) — replaced the FPU-mode-dependent real-SVF hard assertion with a deterministic DenormalFx stub (a stored subnormal CONSTANT, unaffected by FTZ/DAZ) that portably guards stability()'s subnormal DETECTION; real-SVF behavior recorded as backlog TASK-1, not a hard invariant
Severity:   high
Per-lane:   claude=high
Decision:   single-model (gate-counted high)
Surface:    tests/core/measurement-stability-test.cpp:96-113

The test `stability: real SVF FAILS the denormal case` hard-asserts `result.ok == false` and `failedCase == "denormal"`. The premise is that `SvfPrimitive` lets its internal state decay into subnormal floats. But whether subnormals are *produced at all* is not a property of the SVF code — it is a property of the FPU rounding mode at run time. With flush-to-zero / denormals-are-zero enabled (FTZ/DAZ on x86 via `_MM_SET_FLUSH_ZERO_MODE`, the `FZ` bit in AArch64 `FPCR`, or any build with `-ffast-math`), the hardware silently flushes the SVF's decaying state to zero, the denormal stimulus never drives state subnormal, `stability(svf)` returns `{true, nullptr}`, and this test **inverts and fails** — with no actual regression in the SVF.

This matters because the target platforms here are embedded DSP (Daisy/Teensy ARM), and audio/release builds commonly enable FTZ for performance; a CI lane or developer building with FTZ on will see a red test that reads as "the SVF broke" when nothing changed. The blast radius is a false-failure that an unattended agent would chase as a real defect, or "fix" by changing the SVF. A robust guard would either (a) explicitly set the FPU to IEEE/denormals-on at the top of this test so the precondition is enforced rather than assumed, or (b) assert the actual contract under test — that `stability()` *detects* subnormal output when subnormals are present — using the `BrokenFx`-style stub that deterministically emits a subnormal, rather than depending on the real SVF + ambient rounding mode. The accompanying comment (lines 51-67) frames this as catching a "genuine limitation," but the limitation it actually probes is environment-conditional.

### AUDIT-20260629-10 — Correlation latency can report the wrong delay for valid non-impulse inputs

Finding-ID: AUDIT-20260629-10
Status:     resolved (fa-fixup3) — narrowed the documented contract (CorrelationAnalyzer + latencySamples): the unnormalized correlator is well-defined only for impulse/white stimulus; periodic/tonal inputs are out of contract for this minimal-first metric (normalized/windowed correlator deferred). Harness latency tests drive impulses (in-contract)
Severity:   high
Per-lane:   codex=high
Decision:   adjudicated (gate-counted high) — blast-radius=unstated, reachability=unstated, fix-debt=no; no down-calibration signal — high retained.
Surface:    tests/support/measurement/analyzers.h:175-223

`CorrelationAnalyzer::lagSamples()` searches every lag using unnormalized `|sum in[n] * out[n+k]|`. The comment claims that for `out = ±in` delayed by `D`, the magnitude peaks at `D`, but that only holds for signals whose autocorrelation is dominated at zero under the same overlap window. With ordinary tonal, periodic, high-tail-energy, or otherwise structured inputs, a shorter or periodic lag can produce a larger unnormalized overlap than the true delay.

The blast radius is high because this analyzer backs the reusable FR-009 latency metric; a downstream consumer can pass a sine, sweep segment, or program-like measurement buffer and receive a plausible integer latency that is simply the strongest autocorrelation side-lobe or overlap artifact. The fix should narrow the contract and fixtures to impulse/known-white stimulus, or normalize/window/bound the correlation and add adversarial tests where non-impulse inputs would otherwise select the wrong lag.

## 2026-06-29 — audit-barrage lift (end-govern-after_implement)

### AUDIT-20260629-11 — Log sweep can emit NaN/Inf without any guard

Finding-ID: AUDIT-20260629-11
Status:     resolved (fa-fixup4) — SweepGenerator::fill now guards degenerate configs: non-positive sample rate -> silence; equal endpoints or non-positive log endpoints -> well-defined constant-frequency tone at f0Hz. No NaN/Inf from the noexcept path; + a degenerate-params finite fixture
Severity:   high
Per-lane:   codex=high
Decision:   single-model (gate-counted high)
Surface:    tests/support/measurement/stimulus.h:76-86

`SweepGenerator`’s logarithmic branch computes `ratio = f1Hz / f0Hz`, `logRatio = std::log(ratio)`, and then divides by `logRatio` without validating `f0Hz`, `f1Hz`, `sampleRate`, or the `f0Hz == f1Hz` case. Inputs like `f0Hz == 0`, negative frequencies, equal endpoints, or `sampleRate == 0` produce NaN/Inf samples while `fill()` is marked `noexcept`, so the failure becomes silent measurement data corruption rather than an explicit rejected configuration.

The blast radius is high because this is a shared test-support primitive: downstream measurement tests may trust generated stimuli as analytic references, and a bad stimulus can make analyzers fail for the wrong reason or pass against invalid data. A reasonable fix is to define the invariant for valid sweep parameters and either assert/guard invalid inputs or fall back to a well-defined constant-frequency/linear path for degenerate cases.

### AUDIT-20260629-12 — `isClean()` rejects subnormals → false-positive "stability" failures for correct, stable effects

Finding-ID: AUDIT-20260629-12 (claude-01 + claude-02; cross-model)
Status:     resolved (fa-fixup4) — isClean() no longer rejects subnormals (finite+bounded does not threaten stability); the denormal case redesigned to a GENERATION probe (normal step -> silence, check the silent decay tail via detail::hasSubnormal), so passthrough of small values is not flagged. + an IdentityFx passthrough fixture asserting ok==true (would have failed under the old check)
Severity:   high
Per-lane:   claude=high, sonnet=low
Decision:   adjudicated (gate-counted high) — blast-radius=unstated, reachability=unstated, fix-debt=no; no down-calibration signal — high retained.
Surface:    tests/support/measurement/metrics.h:303-318 (`detail::isClean`), consumed by `stability()` lines 360-430

`detail::isClean()` fails any buffer containing a subnormal sample (`std::fpclassify(x) == FP_SUBNORMAL → return false`), and `stability()` applies it to **all four** cases. This conflates a *performance* concern (sustained denormals stall the CPU) with the *numerical-stability* contract FR-012 actually scopes (NaN/Inf/runaway growth), and it produces spurious failures for effects that are perfectly stable and correct:

- **"denormal" case (lines 389-405):** the stimulus is a decaying exponential reaching ~1e-40, which is *itself subnormal* in its final ~hundreds of samples (FLT_MIN ≈ 1.175e-38). A faithful linear effect — a bypass, a unity gain, any effect that passes small inputs through without flushing — reproduces those subnormal inputs on its output and is reported as a stability *failure*. The test cannot distinguish "effect manufactures denormals internally (bad)" from "effect faithfully passes a denormal input (fine)."
- **"dc" case (lines 382-387):** a stable high-pass filter fed constant DC decays its output toward 0, passing through the subnormal range on the way down. Stable, correct, bounded — yet flagged.
- **"silence"/"idle" cases:** an effect with a quiet decaying tail whose samples are *below the 1e-6 idle floor* (i.e. inaudible, passing the magnitude bound) still fails purely because those tiny tail values are subnormal — the value is rejected for being too *small*, not too large.

Blast radius: this is test-support code adopters run to validate their effects. A correct effect failing the "stability" battery is worse than a silent pass — it sends the adopter to debug a non-bug, or to bolt on denormal-flushing they didn't need, or to distrust the harness. A reasonable fix: drop the `FP_SUBNORMAL` rejection from the stability/idle/silence criteria entirely (subnormals are bounded and finite, so they don't threaten stability), or split it into a *separate, explicitly-named* "produces-no-sustained-denormals" check that the adopter opts into — and that measures denormals the effect *generates* rather than ones it merely *passes through* from a deliberately-denormal stimulus.
