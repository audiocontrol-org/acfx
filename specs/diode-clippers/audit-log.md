---
slug: diode-clippers
targetVersion: ""
---

# Audit log — diode-clippers

## 2026-07-06 — audit-barrage lift (end-govern-after_implement)

### AUDIT-20260706-01 — `currentTol_` is validated, stored, and exposed as a tunable but never gates anything — a dead convergence criterion

Finding-ID: AUDIT-20260706-01 (claude-02 + claude-01; cross-model)
Status:     open
Severity:   high
Per-lane:   claude=low, sonnet=high
Decision:   adjudicated (gate-counted high) — blast-radius=unstated, reachability=unstated, fix-debt=no; no down-calibration signal — high retained.
Surface:    core/labs/diode-clippers/solver/transient-clipper.h:82-89 (ctor doc + validation), :335 (`currentTolerance()`), :140-165 (Newton loop)

The constructor accepts `currentTol` (default `1e-12`), validates it `> 0`, stores it, and exposes it via `currentTolerance()`; the constructor comment frames iteration bound and "convergence tolerances" (plural) as the harness-tunable knobs. But the Newton loop gates convergence solely on `dv < voltageTol_` (line ~160). `currentResidual` is computed and reported, yet `currentTol_` is never compared to anything. It is dead configuration masquerading as a live tuning parameter.

Blast radius: a harness author (or an unattended agent) tuning `currentTol` down to force a tighter current-based convergence will see no behavioral change and may conclude the solver converged on current when it only ever converged on voltage — a silent no-op knob is a trust hazard exactly of the kind FR-011 warns against ("do not silently retune to hide a non-converging case"). Fix: either add the current-residual gate (`converged = dv < voltageTol_ && di < currentTol_`, or an OR per the intended semantics) or delete the parameter and its accessor and drop "tolerances" to singular in the doc. Keeping an inert knob that reads as active is the defect.

### AUDIT-20260706-02 — symmetricShuntClipper test never verifies the diodes are ANTIPARALLEL — the one property that makes it "symmetric"

Finding-ID: AUDIT-20260706-02
Status:     open
Severity:   high
Per-lane:   claude=high
Decision:   single-model (gate-counted high)
Surface:    tests/core/diode-clipper-builder-test.cpp:100-120 (the `symmetricShuntClipper - prepare()-valid topology` case) + `everyDiodeSpansPort` helper at lines 65-79

The symmetric shunt clipper's defining electrical property is that its two diodes sit **antiparallel** across the shunt node (one anode-up, one anode-down) so the wave is clipped symmetrically on both half-cycles. This test asserts `diodeCount == 2`, `onlyFrozenClipperVocabulary`, and `everyDiodeSpansPort(portP, portN)` — but `everyDiodeSpansPort` (lines 65-79) returns true for a diode in **either** orientation (`fwd || rev`). So two diodes both wired `anode==shunt, cathode==ground` (same orientation, i.e. a parallel half-wave rectifier, not a symmetric clipper) satisfy every assertion in this case and the suite passes green.

Note the asymmetric case immediately below (lines 140-152) *does* count orientations (`up==2`, `down==1`) — so the machinery to check orientation exists and is deliberately applied there, but the symmetric case, whose whole name is "symmetric," omits it. That inconsistency is the tell: the single most safety-relevant invariant of this topology is unguarded.

Blast radius: this file is the stated Tier-1 topology **contract guard** for SC-001 ("the port IS the diode-string node pair"). A builder bug that mis-orients the second diode (a one-character anode/cathode swap in a copy-pasted line is exactly the likely defect) produces asymmetric/half-wave DSP behavior and this guard stays green — a downstream agent building on the "verified" primitive inherits silently-wrong clipping. Fix: assert exactly one diode has `anode==portP` and exactly one has `cathode==portP` (mirror the up/down count already used in the asymmetric case).

### AUDIT-20260706-03 — Multi-diode series test requires the wrong topology

Finding-ID: AUDIT-20260706-03
Status:     open
Severity:   high
Per-lane:   codex=high
Decision:   single-model (gate-counted high)
Surface:    tests/core/diode-clipper-builder-test.cpp:63-75,171-175

The shared helper `everyDiodeSpansPort()` requires every diode to connect directly between `portP` and `portN`. That is correct for antiparallel shunt clipping, but it is not correct for a multi-diode series string. In the `seriesCount=2` test, lines 171-175 assert two diodes exist and then require both to span the same port pair, which describes parallel duplicate diodes across the same nodes, not two inline series diodes with an intermediate node.

The blast radius is high because this test is the Tier-1 topology contract for the primitive. A downstream implementer can satisfy it by building the wrong electrical topology, and the test would reject the natural/correct two-diode series chain if it introduces the required intermediate node. A reasonable fix is to split the helper by topology: shunt tests can require every diode to span the clipper port, while series tests should assert a connected diode chain from `portP` to `portN` with `seriesCount - 1` intermediate nodes and no duplicated parallel spans.

### AUDIT-20260706-04 — Non-convergence and RC checks skip `reset()`, relying on an unstated default-construction contract

Finding-ID: AUDIT-20260706-04 (claude-03 + codex-01; cross-model)
Status:     open
Severity:   high
Per-lane:   claude=low, codex=high
Decision:   adjudicated (gate-counted high) — blast-radius=unstated, reachability=unstated, fix-debt=no; no down-calibration signal — high retained.
Surface:    core/labs/diode-clippers/harness/diode-clippers-harness.cpp:296-306 (`runNonConvergence`); also 103-116 (`runLinearRc`)

Every helper-mediated path zeroes solver state before use — `settlePortDC` (line 96) and `driveSine` (line 119) both call `solver.reset()` first. The two raw-loop paths do not: `runNonConvergence` constructs `TransientClipper<4,8> starved(1)` and immediately calls `starved.step(...)` with no `reset()`, and `runLinearRc` constructs `TransientClipper<8,8> solver` and enters its step loop with no `reset()`. Both therefore depend on default/parameterized construction leaving the reactive-history buffers at a defined zero state — a contract that lives in `transient-clipper.h` (a different chunk) and is nowhere asserted here.

Blast radius: low. `runLinearRc` is self-verifying — non-zero initial capacitor history would blow `maxErr` past `1e-9` and fail loudly, so a broken contract can't produce a silent false PASS there. `runNonConvergence` is the weaker case: it asserts `!status.converged` after a single starved iteration, and a garbage (or unexpectedly non-zero) initial history could in principle flip `converged` either way, so a constructor that doesn't value-initialize would make this test's result non-deterministic rather than loudly wrong. Since the fleet of other paths establishes `reset()` as the harness convention, the cheap and consistency-restoring fix is to call `starved.reset()` (and `solver.reset()` in `runLinearRc`) before the first `step()`, so no path relies on an implicit construction-zeroes-history contract.

---

I checked the remaining surfaces and found them clean: the CMake target mirrors the sibling passive-tone-stacks harness (same linkage/features, guarded by `ACFX_BUILD_TESTS OR ACFX_BUILD_DESKTOP`); the `main()` accumulator uses the `fn() && allPassed` ordering so every check runs regardless of prior failures (no short-circuit skip); the backward-Euler `alpha = dt/(dt+RC)` recurrence and the symmetric/asymmetric KCL oracles are algebraically correct; the 1000-sample / 10-period window aligns DFT bins to avoid leakage; and `bisectRoot` brackets a monotone residual over `[-5,5]`. Those are the reasons I did not surface findings there.
