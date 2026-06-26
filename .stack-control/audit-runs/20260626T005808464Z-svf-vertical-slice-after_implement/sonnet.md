I'll walk through the four files in this chunk carefully, looking for correctness bugs, design issues, missed edge cases, and operator-discipline traps.

---

### Silent false-pass in portability gate: missing MCU adapter directories

Finding-ID: AUDIT-BARRAGE-claude-01
Status:     open
Severity:   medium
Surface:    scripts/check-portability.sh:37-42

Check 3 runs `grep -rEn 'juce|processor-node' adapters/daisy adapters/teensy`. GNU `grep` exits 0 on a match (triggers the `if`-branch → `fail=1`), exits 1 on no match, and exits **2** on error (e.g., a named directory does not exist). Because the `if` only tests for exit code 0, a missing `adapters/daisy` or `adapters/teensy` directory causes `grep` to exit 2, which is **not** 0, so the branch is never taken and the script prints `OK: daisy + teensy reference neither JUCE nor ProcessorNode`. The portability gate passes on a surface that was never inspected.

This matters in CI if the MCU adapter directories are absent from a partial checkout, a shallow clone, or an initial scaffold run — the very scenario where the guard is needed most. The blast radius: CI reports green on a JUCE-contaminated MCU surface that was never scanned.

Minimal fix: add an explicit existence guard before the grep, or add `|| { note "  FAIL: MCU adapter dirs missing"; fail=1; }` after the grep call.

---

### Silent false-pass in portability gate: missing core/effects/ directory for #ifdef fork check

Finding-ID: AUDIT-BARRAGE-claude-02
Status:     open
Severity:   medium
Surface:    scripts/check-portability.sh:44-48

The same exit-code logic applies to check 4. `grep -rEn '#if(def)?.*(JUCE|DAISY|TEENSY|__arm__|DESKTOP)' core/effects/` exits 2 when `core/effects/` is absent. Because the `if` only triggers on exit 0, a missing directory silently reports `OK: no per-target #ifdef forks in core/effects/`. The one-source-many-targets invariant (SC-001/SC-005) is therefore unverifiable when the directory is absent, yet the gate passes.

Check 2 (`grep -rEn '...' core/`) has the same shape but for all of `core/`, not a subdirectory, making the silent-pass scenario less likely in practice. For check 4 the subdirectory is more fragile.

Fix: `[ -d core/effects/ ] || { note "  FAIL: core/effects/ missing"; fail=1; }` before the grep.

---

### Portability gate missing set -o pipefail; pipe error in size check silently returns empty

Finding-ID: AUDIT-BARRAGE-claude-03
Status:     open
Severity:   low
Surface:    scripts/check-portability.sh:12,21

The script uses `set -u` but not `set -o pipefail`. Line 21 is:

```bash
lines=$(wc -l < "$f" | tr -d ' ')
```

If `wc` fails (e.g., the file becomes unreadable between `find` and the read — unlikely but possible in a race, or on a networked FS), `tr` still succeeds and the assignment captures empty output. The subsequent `[ "$lines" -gt 500 ]` then errors (`integer expression expected`) and emits a warning to stderr, but without `set -e` the loop continues and `fail` is not set. The file is silently skipped, not flagged.

Adding `set -o pipefail` immediately after `set -u` would cause the compound command to exit non-zero on any pipe-stage failure, and the `while`-loop body's return code would propagate to `fail` handling. The blast radius here is low (it requires a genuine I/O error mid-scan), but `set -o pipefail` is a one-line fix with no downside.

---

### no-allocation test covers only kCutoff in setParameter; kMode and kResonance unverified

Finding-ID: AUDIT-BARRAGE-claude-04
Status:     open
Severity:   low
Surface:    tests/core/no-allocation-test.cpp:36

The comment at line 35-36 states "parameter changes on the audio thread must also be allocation-free," but the loop only calls `fx.setParameter(ParamId{SvfEffect::kCutoff}, ...)`. `kResonance` (a linear float) and `kMode` (a discrete integer switch) are never tested.

The mode switch is the interesting case: if the `SvfEffect::setParameter` implementation for `kMode` selects a sub-processor or reinitialises a filter branch, there is a non-zero risk of a transient allocation (e.g., a `std::function` capture, a `std::any`, or a strategy-pattern allocation). The test as written would not catch this. Adding a second loop that cycles through all three parameter IDs would close this gap without significant test complexity.

---

### tests/CMakeLists.txt forces C++20 for all core tests, leaving C++17 fallback path untested for RT-safety

Finding-ID: AUDIT-BARRAGE-claude-05
Status:     open
Severity:   low
Surface:    tests/CMakeLists.txt:17

`target_compile_features(acfx_core_tests PRIVATE cxx_std_20)` pins the entire test binary — including `no-allocation-test.cpp` — to C++20. T011 and T017 describe a C++17 duck-typed fallback path (guarded by `__cpp_concepts`). The no-allocation invariant (FR-014) is claimed for the effect regardless of standard dialect, but the test suite only exercises the C++20 build.

If the C++20 path compiles `SvfEffect` differently from the C++17 path (e.g., `constexpr` elaborations that avoid a heap call the C++17 path makes), the RT-safety guarantee is only demonstrated for one dialect. A second `ctest` preset or a `cxx_std_17` variant of the test binary would close this; alternatively, the tasks documentation should note that the FR-014 guarantee is validated only under C++20.

---

### tasks.md T035 marked [X] complete while body documents an explicit link-time block

Finding-ID: AUDIT-BARRAGE-claude-06
Status:     open
Severity:   informational
Surface:    specs/svf-vertical-slice/tasks.md:131-134

T035 is marked `[X]` (complete) but the body reads: "**Blocked / on-board checkpoint:** full firmware ELF link — the installed `arm-none-eabi-gcc` is C-only (ships no libstdc++), so linking + flashing is the proper-toolchain/hardware checkpoint." The checkpoint note below it explicitly says "T035, blocked here by a C-only `arm-none-eabi-gcc`."

The body text is honest and appropriately flags the unverified surface. The blast-radius concern is limited because a reader will see the caveat. However, an automated tool scanning for `[X]` as "verified done" would count T035 as closed when it is at best "compile-verified, link blocked." Consistency with T027 and T031 (which use the same pattern with manual-checkpoint caveats) means this is an accepted project convention rather than a defect — surfacing it here for operator awareness.