I'll audit this chunk (the portability gate, tasks.md status updates, the test CMake wiring, and the no-allocation test). Findings below.

### check-portability.sh fails open when a scanned directory is absent or renamed

Finding-ID: AUDIT-BARRAGE-claude-01
Status:     open
Severity:   medium
Surface:    scripts/check-portability.sh:16-26, 29-35, 37-43

Three of the four gates treat "directory not present / nothing scanned" as a pass. Check 2 is `if grep -rEn 'juce|...' core/ ; then FAIL ; else OK`. `grep -r` returns 0 on a match (violation), 1 on no match (clean), and **2 on a fatal error such as the directory not existing**. Exit 2 makes the `if` false, so control falls to the `else` and the script prints `OK: core/ is platform-independent` — having scanned nothing. Check 3 (`adapters/daisy adapters/teensy`) has the identical shape, and Check 1's file-size loop is the same class: a `find` over a missing root yields zero files, so `[ "$fail" -eq 0 ] && note "OK: all source files within budget"` prints OK without inspecting anything.

This is a CI gate whose entire job is to enforce Constitution IV / SC-007. The blast radius is a refactor that renames `core/` → `src/core/`, or a partial/sparse checkout in CI, silently turning the portability gate green while it verifies nothing — exactly the failure mode a quality gate exists to prevent. A safety gate should fail *closed*: assert each scanned root exists (e.g. `[ -d core ] || { note "FAIL: core/ missing"; fail=1; }`) before grepping, and treat `grep` exit ≥2 as a failure rather than a clean result.

### T035 marked `[X]` while its own acceptance criterion (link + linked artifacts) is explicitly blocked

Finding-ID: AUDIT-BARRAGE-claude-02
Status:     open
Severity:   medium
Surface:    specs/svf-vertical-slice/tasks.md (T035 line + US3 checkpoint)

T035's text is "Run quickstart Scenario D: build `daisy` + `teensy` presets, **confirm linked artifacts** and that neither dependency graph includes JUCE (SC-007)." The task is now checked `[X]`, but its appended body states the firmware ELF **link** is blocked: "the installed `arm-none-eabi-gcc` is C-only (ships no libstdc++), so linking + flashing is the proper-toolchain/hardware checkpoint." The checkbox therefore asserts done for a task whose load-bearing acceptance clause ("confirm linked artifacts") is admittedly not met.

The rewritten prose checkpoint is honest ("US3 compile-verified … the full firmware ELF **link** … is the on-hardware checkpoint"), so a careful human reader is corrected. The risk is the machine-readable signal: roadmap/spec-reconciliation tooling (which reconciles on-disk checkbox state against progress) reads `[X]` as complete and concludes SC-007's "build **and link** on both MCUs" is satisfied, when only compile-to-object is proven. Distinguish the two — either leave T035 unchecked with a "compile-verified; link blocked" note, or split it into a verified compile sub-task and an open link/flash sub-task — so the checkbox state matches the artifact's own prose.

### no-allocation test exercises only `kCutoff`; the discrete mode + resonance params are never set in the measured region

Finding-ID: AUDIT-BARRAGE-claude-03
Status:     open
Severity:   medium
Surface:    tests/core/no-allocation-test.cpp:23-32

The test's stated contract is broad: the header comment claims it proves "the no-heap-allocation-in-`process()` invariant (FR-014)" and that "parameter changes on the audio thread must also be allocation-free." But the measured loop only ever calls `fx.setParameter(ParamId{SvfEffect::kCutoff}, …)` (line 30), alternating two continuous values. The discrete **mode** parameter (cutoff[log]/resonance[linear]/mode[discrete×3] per T017) and resonance are never driven inside the sentinel window.

Mode is the highest-risk path for the invariant it claims to guard: a discrete parameter switch is precisely the kind of branch that could re-init filter state or touch a container and allocate, and it is the one path left untested. An adopter reading "no-allocation invariant: green" reasonably believes the entire audio-thread parameter surface is allocation-free, when in fact only one of three descriptors is covered. Extend the loop to also `setParameter` resonance and to cycle the mode through all three discrete values within the measured region, so the test verifies the contract its comment claims.

### "OK" status lines for later checks are suppressed once any earlier check fails

Finding-ID: AUDIT-BARRAGE-claude-04
Status:     open
Severity:   low
Surface:    scripts/check-portability.sh:27, 59

The per-check "OK" confirmations are gated on the *global* accumulator: `[ "$fail" -eq 0 ] && note "  OK: …"`. For Check 4 (line 59, `OK: every adapter links the same acfx_core`), `fail` may already be 1 from Check 1/2/3, so a fully-passing Check 4 prints no OK line at all — the output goes straight from the adapter loop to the final `Portability gate FAILED.` banner. A reader scanning the log to see *which* gate failed gets a misleading picture: a green check looks silent/indeterminate rather than passed.

This is cosmetic — the exit code is correct — but it degrades the diagnostic value of a gate whose output an operator reads precisely to localize a failure. Track a per-check local flag (e.g. `c4=0` reset at the top of each section) and gate each "OK" on its own flag rather than on the shared `fail`.

### Checks 2 and 3 use case-sensitive lowercase patterns, missing uppercase `JUCE_`/`DAISY` macros that Check 4 catches

Finding-ID: AUDIT-BARRAGE-claude-05
Status:     open
Severity:   low
Surface:    scripts/check-portability.sh:30, 38

Check 2 (`grep -rEn 'juce|libDaisy|daisy_seed|…' core/`) and Check 3 (`grep -rEn 'juce|processor-node' adapters/daisy adapters/teensy`) are case-sensitive and only match lowercase `juce`. JUCE's preprocessor surface is uppercase (`JUCE_STANDALONE_APPLICATION`, `JUCE_API`, `JUCE_AUDIO_PROCESSORS_H_INCLUDED`), so a `core/` or MCU-adapter file that referenced a `JUCE_*` macro (without a lowercase include path) would slip through. Notably, Check 4 on line 49 already anticipates uppercase tokens — `'#if(def)?.*(JUCE|DAISY|TEENSY|…)'` — so the script is internally inconsistent about case within the same file.

The practical blast radius is small (a stray macro is unlikely to be the *only* JUCE reference, and direct lowercase includes are the common case), hence low. But for a gate asserting "core/ is platform-independent," the cheap fix is to add `-i` (or fold uppercase alternates into the pattern) so checks 2/3 are as thorough as check 4.

---

I also checked `tests/CMakeLists.txt` (the `doctest_SOURCE_DIR`-empty fallback degrades gracefully to a single `add_test`; the `cxx_std_20` force is intentional for host tests) and the `CHECK_MESSAGE` variadic message usage in the no-allocation test (valid doctest form) — both clean. The strongest signals are the fail-open gate (claude-01) and the checkbox/acceptance mismatch on T035 (claude-02).