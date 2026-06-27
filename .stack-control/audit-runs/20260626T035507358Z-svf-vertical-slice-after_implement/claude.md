I walked chunk `66d76089d898899c` (tasks.md, tests/CMakeLists.txt, tests/core/no-allocation-test.cpp), cross-referenced the other chunks' file lists for what's *absent*, and applied the round-0 self-red-team driver to the fix diff itself. Findings below.

### Behavioral test suite is C++20-only — the C++17 concept-degradation path that T034/T035 claim works is never run

Finding-ID: AUDIT-BARRAGE-claude-01
Status:     open
Severity:   medium
Surface:    tests/CMakeLists.txt:18

`target_compile_features(acfx_core_tests PRIVATE cxx_std_20)` pins the entire doctest suite — `parameter-test.cpp`, `svf-test.cpp`, and the new `no-allocation-test.cpp` — to C++20. Tasks T034/T035/T038 (now all `[X]`) assert that the *identical* `core/effects/svf` compiles and behaves correctly at **both** C++17 (concept degraded) and C++20 (named concept), and that this is what proves the Teensy/MCU portability claim. But the only thing ever validated at C++17 is *compilation* (via `scripts/check-portability.sh`, another chunk). The runtime assertions — parameter scaling math, SVF frequency response, NaN/denormal stability, and the no-allocation invariant — execute **only** under C++20.

The blast radius: the Teensy target ships on a C++17 toolchain, so the standard an unattended consumer actually runs in production is the one whose *behavior* was never asserted. If the C++17 duck-typed fallback path (or any `#if __cpp_concepts` branch in `effect.h` / `svf-effect.h`) diverges behaviorally from the C++20 path — a different overload selected, a constexpr table evaluated differently — the green test suite cannot catch it, yet T034/T035 read as "the degradation path is verified." A reasonable fix is a second test target (or a CTest matrix entry) that builds and runs the same `tests/core/*` at `cxx_std_17`, so the behavioral contract is asserted on both standards, not just compiled.

### Acceptance scope was narrowed in tasks.md but spec.md Success Criteria and quickstart.md were not reconciled

Finding-ID: AUDIT-BARRAGE-claude-02
Status:     open
Severity:   medium
Surface:    specs/svf-vertical-slice/tasks.md:79-167 (and the absent specs/svf-vertical-slice/spec.md / quickstart.md)

This diff rewrites T027/T031/T035/T038 from "Run quickstart Scenario X and confirm all acceptance scenarios" to "Build + automated-verify …", relocating the interactive/hardware halves into a new **Manual acceptance** section, and the rewordings repeatedly cite the spec's success criteria as satisfied — "SC-006 by construction" (T031), "portability gate green … SC-007" (T035), "(SC-001, SC-005)" (T038). Yet `specs/svf-vertical-slice/spec.md` and `quickstart.md` appear in **none** of the audited chunks' file lists — they were not touched. So the narrowing of what "done" means lives entirely in tasks.md while the upstream contract (the SC definitions, e.g. SC-001/SC-005 "the identical core built **all four** targets", SC-007 "**build & link** on both MCUs") still reads in its original, stronger form.

The honest verified/manual split itself was dispositioned in f88525b across eight govern rounds — I am not re-litigating that. The *new* edge this fix opened is the drift: an unattended agent that treats spec.md's Success Criteria as the authoritative contract (the natural choice — SCs are the acceptance contract, tasks are the worklog) will read SC-001/SC-005/SC-007 as fully met when tasks.md now admits two of four targets were only host-compile-verified and the MCU link/flash never happened. The reconciling fix is to annotate the affected SCs in spec.md (and the corresponding quickstart scenarios) with the same automated-vs-manual boundary tasks.md now carries, so the two artifacts cannot be read to claim different things.

### New "Manual acceptance" items use a non-GFM glyph (☐), so the three unverified acceptance scenarios are invisible to task-completion tooling

Finding-ID: AUDIT-BARRAGE-claude-03
Status:     open
Severity:   medium
Surface:    specs/svf-vertical-slice/tasks.md:135-153

The fix marks all 39 real tasks `[X]` and parks the three actual user-story acceptance runs (US1 live sweep/MIDI/A-B, US2 in-DAW parity, US3 on-hardware build+flash) under a new **Manual acceptance** section as `☐` (U+2610 BALLOT BOX) bullets rather than GFM `- [ ]` checkboxes. The prose explains they are deliberately "**not** as automated task checkboxes" — but the consequence is that any progress reconciler that counts `- [ ]` vs `- [X]` (including the spec-progress reconciliation this governance loop itself performs) sees 39/39 complete = 100%, and never surfaces that the only checks that prove a single user story *actually works end-to-end* are still open.

The blast radius is a false "feature complete & verified" reading by an unattended consumer: every user story's stated **Independent test** *is* its quickstart scenario, and not one has been run — no audio has passed through the workbench, no plugin loaded in a host, no firmware on a board — yet a checkbox-counting reader concludes the feature is shipped and validated. Because these items are genuinely incomplete acceptance work (not won't-fix), they should be tracked as first-class open `- [ ]` checkboxes (clearly labelled operator/manual) so completion tooling reports the feature as partially verified, rather than as an appendix that disappears from any automated tally.

### no-allocation test covers EffectNode thinly — single block size, and `setParameter` allocation-freedom is asserted only for the bare SvfEffect

Finding-ID: AUDIT-BARRAGE-claude-04
Status:     open
Severity:   low
Surface:    tests/core/no-allocation-test.cpp:38-58

The first test case sweeps block sizes `{16, 64, 256, 512}` over `SvfEffect` *and* exercises `fx.setParameter(...)` inside the measured region (line 31) — good coverage of the audio-thread parameter path. The second case, for the `EffectNode<SvfEffect>` host boundary, drops both: it tests `processBlock` at a single hardcoded `blockSize = 256` (line 41) and never calls `setParameter` through the node. The host-boundary wrapper is precisely the layer most likely to introduce an indirection that allocates (a queue, a `std::function`, a virtual dispatch capturing state), so the asymmetry leaves the more-suspect surface less tested than the leaf it wraps.

Blast radius is low — the bare-effect path is well covered and a node-level allocation would likely also show up there — but the test's own comment claims it validates "the EffectNode<SvfEffect> host boundary," which oversells single-size, no-parameter-change coverage. A fix is to run the EffectNode case over the same block-size set and route a `setParameter` call through the node inside the measured loop, matching the rigor of the first case. (Minor adjacent note: the `100`-iteration count in both loops is an unexplained magic number — a one-line comment on why 100 suffices would help the next reader.)