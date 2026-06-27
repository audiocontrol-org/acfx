I'll audit chunk `66d76089d898899c` (tasks.md ledger reframing + the new test build wiring + no-allocation test) for the svf-vertical-slice feature. Walking the three files for correctness, internal consistency, and overclaim residue against the reworded ledger.

### T038 still claims "built all four targets" — contradicts T035's admission that the MCU targets are not built

Finding-ID: AUDIT-BARRAGE-claude-01
Status:     open
Severity:   high
Surface:    specs/svf-vertical-slice/tasks.md — T038 (Phase 6) vs. T035 (Phase 5)

T035 was honestly reworded to admit the MCU build is **not** done: *"the installed `arm-none-eabi-gcc` is C-only (no libstdc++), so the on-target compile, link, and flashing are the on-hardware checkpoint… Not verified here: the actual arm-none-eabi Cortex-M7 compile and the firmware ELF link."* But T038 — marked `[X]` and left with its original text — asserts *"the identical `core/effects/svf` **built all four targets** with no per-target `#ifdef` forks of the effect (SC-001, SC-005)."* Only two targets (workbench, plugin) were actually built; daisy and teensy were host-compile-verified, not built for their real target. The two completed tasks cannot both be true as written.

The blast radius is exactly the overclaim that commit d183a18 ("correct ARM overclaim") set out to remove: an unattended agent scanning the ledger for "are all four targets built?" lands on T038's affirmative `[X]` and propagates "cross-platform build proven on all four targets" downstream — skipping the ARM-toolchain setup the feature still needs, or reporting the cross-platform claim as fully realized. T035's nearby correction does not reach a reader who keys on T038. A reasonable fix mirrors the T035/T027/T031 reframing: reword T038 to "built the two desktop targets and host-compile-verified the identical core for the two MCU targets (the on-target MCU build is the Manual-acceptance checkpoint)," so the one-source-many-targets claim is scoped to what was actually proven.

### T034 (unchanged, marked done) asserts the Teensy toolchain supports ≥C++17 — T035 says the installed ARM toolchain is C-only

Finding-ID: AUDIT-BARRAGE-claude-02
Status:     open
Severity:   medium
Surface:    specs/svf-vertical-slice/tasks.md — T034 vs. T035 (Phase 5)

T034 is `[X]` with text unchanged: *"Verify the installed Teensy toolchain's C++ standard… **set Teensy to the highest supported (≥C++17)** in `cmake/toolchains/teensy.cmake` and confirm the concept-degradation path compiles the same `SvfEffect`."* This asserts the installed Teensy toolchain (also `arm-none-eabi-gcc`, Cortex-M7) supports and was set to ≥C++17. T035 directly contradicts that: *"the installed `arm-none-eabi-gcc` is **C-only (no libstdc++)**."* A C-only toolchain cannot compile C++ at any standard, so "verified the Teensy toolchain's C++ standard and set it to ≥C++17, confirming `SvfEffect` compiles" is not what happened — the concept-degradation confirmation was actually done on the **host** (per T035's "verified on the host toolchain"), substituting for the Teensy compile.

The reading an agent reaches from T034 is "the Teensy toolchain compiles C++17 and `SvfEffect` was confirmed compiling under it" — false, and load-bearing for anyone planning the on-hardware step (they'd expect a working C++ toolchain already present). Severity is medium rather than high only because T035's longer explanation, if read, resolves it; but T034 left unchanged is the more natural first hit. Fix: reword T034 to state the toolchain was found C-only, the `teensy.cmake` standard was set as the *target* configuration, and the concept-degradation path was confirmed on the host C++17 compile — not on the Teensy toolchain.

### Manual-acceptance items use `☐` Unicode, invisible to any `[X]`/`[ ]` completion counter — ledger reads 100% complete while three acceptance gates remain

Finding-ID: AUDIT-BARRAGE-claude-03
Status:     open
Severity:   medium
Surface:    specs/svf-vertical-slice/tasks.md — "Manual acceptance" section (new)

Every numbered task T001–T039 is now `- [X]`, and the three deferred acceptance runs were moved to a new "Manual acceptance" section rendered with `☐` Unicode bullets rather than markdown `- [ ]` checkboxes. The prose discloses this ("tracked here as explicit operator checkpoints, **not** as automated task checkboxes"), but the structural consequence is a trap: any mechanical completeness check that computes a ratio of `- [X]` to `- [ ]` (and this project explicitly adds "script/CI checks" per T037, and runs govern/ship gates over feature state) will count 39/39 = 100% and never surface that US1/US2/US3 acceptance is still pending. The interactive sweep, in-DAW parity, and on-hardware flash — the actual proof the feature works — are exactly the parts hidden from the counter.

For an unattended consumer ("all tasks `[X]` → feature done → graduate/ship"), this reports a fully-accepted feature when only the build is verified. The blast radius is a premature ship/close decision. A safer encoding keeps the manual items as real markdown checkboxes (`- [ ]`) under an explicitly-labeled non-automated heading, OR adds a machine-readable marker (e.g. a `status: manual-acceptance-pending` field) the gate scripts key on, so mechanical counters see the gap the prose describes.

### no-allocation test never observes `process()` output — can pass vacuously if the optimizer elides the measured calls

Finding-ID: AUDIT-BARRAGE-claude-04
Status:     open
Severity:   low
Surface:    tests/core/no-allocation-test.cpp:25-37

In both cases the measured loop writes only into local `left`/`right` buffers that are never read after the loop, and `fx.process` / `node.processBlock` come from headers (`svf-effect.h`, `processor-node.h`) so they are inlinable into this TU. Between `AllocationSentinel::reset()` and `AllocationSentinel::allocations()` the compiler is free to dead-code-eliminate process calls it can prove side-effect-free on never-observed buffers. If it does, the sentinel reads 0 and the test passes — but it passes *vacuously*, having measured nothing. For an invariant test whose entire job is to prove `process()` does not allocate, a green that can be produced by eliding `process()` weakens the guarantee (FR-014).

This is low severity because the failure mode is a false *pass*, not a false fail, and only bites under aggressive optimization; but it undercuts the test's contract. A cheap hardening is to consume the output so the calls are observably live — e.g. accumulate `left[0]`/`right[0]` into a `volatile float` sink (or `doctest::DOCTEST_REF_WRAP` / a `CHECK` on a derived value) after the loop, forcing the compiler to keep the processing. That also makes the test exercise the real numeric path rather than a potentially-elided one.

---

The new `tests/CMakeLists.txt` itself is clean: the `doctest_SOURCE_DIR`-guarded discovery degrades gracefully to a single `add_test` when the helper module is absent (including the undefined-variable case where the path resolves to a non-existent absolute path), the `tests/`-rooted include dir correctly serves the `support/...` includes, and capturing `allocations` into a local *before* `CHECK_MESSAGE` runs is the right move to keep the assertion macro's own string formatting out of the measured count. The `cxx_std_20` force on the test binary means ctest only ever exercises the C++20 (named-concept) core path, not the C++17 degraded path — but T035 frames the C++17 verification as a separate compile-only step, so that is a disclosed boundary, not a contradiction.