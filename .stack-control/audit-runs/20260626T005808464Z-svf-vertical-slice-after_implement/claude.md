I'll audit chunk `1d366441c57c4606` — the portability gate script, the tasks.md status flip, and the test wiring. Reading each surface against the feature's headline portability claim.

### SC-007 "no JUCE in MCU dependency graph" is enforced as a shallow textual grep, not a dependency-graph check

Finding-ID: AUDIT-BARRAGE-claude-01
Status:     open
Severity:   medium
Surface:    scripts/check-portability.sh:36-43 (section 3); cross-ref specs/svf-vertical-slice/tasks.md T037, SC-007

Section 3 implements the SC-007 guarantee as `grep -rEn 'juce|processor-node' adapters/daisy adapters/teensy`. T037 and SC-007 are phrased as a property of the **dependency graph** ("no JUCE in MCU dependency graph"), but a recursive substring grep over two adapter directories cannot observe transitive linkage. If `acfx_core` (or any library a daisy/teensy `CMakeLists.txt` links) transitively pulled in JUCE, the literal string `juce` need never appear under `adapters/daisy` or `adapters/teensy`, and the gate would still print "OK: daisy + teensy reference neither JUCE nor ProcessorNode."

The blast radius is that the headline feature claim ("one-source-many-targets; no JUCE on MCU") rests on this script as its named automated guard, and CI does not build the MCU presets (T036 builds only `test`+`desktop`), so this grep is the *only* enforcement. An adopter or unattended agent reading "portability gate green" will conclude the MCU dependency graph is JUCE-free when it was never inspected. A faithful check would resolve the actual link/dependency graph (e.g. `cmake --graphviz=` on the daisy/teensy targets, or asserting on the resolved `LINK_LIBRARIES` transitive closure) rather than grepping source text — or the script's wording and the task description should be downgraded to "no JUCE *referenced in MCU adapter source*," which is all it actually verifies.

### Section 4 reports "every adapter links the same acfx_core" from a text-presence grep, while T035 admits the MCU link is impossible

Finding-ID: AUDIT-BARRAGE-claude-02
Status:     open
Severity:   medium
Surface:    scripts/check-portability.sh:51-57; cross-ref specs/svf-vertical-slice/tasks.md T035

Section 4's loop does `grep -rq 'acfx_core' "adapters/$adapter/CMakeLists.txt"` and, on success, prints "OK: every adapter links the same acfx_core." This is a substring-presence check on a CMake file, not evidence that the adapter *links* anything — a comment, a commented-out line, or a `# links acfx_core` note all satisfy it. More pointedly, the same diff's tasks.md T035 states the daisy/teensy firmware ELF **link cannot complete** ("the installed `arm-none-eabi-gcc` is C-only … so linking + flashing is the … checkpoint"). So for two of the four adapters the gate emits a green "links acfx_core" line describing something the task itself documents as unachievable in this environment.

The consequence is a false-green: a reader scanning gate output sees four adapters confirmed to "link the same acfx_core," contradicting the honest caveat buried in T035. Rename the section to what it verifies ("every adapter *declares* a dependency on acfx_core") or make it assert against configured CMake targets (`cmake --build` for desktop, and explicitly mark MCU as declaration-only) so the output does not overstate.

### tasks.md T038 marked [X] "built all four targets" with no caveat contradicts T035's MCU-link-blocked admission

Finding-ID: AUDIT-BARRAGE-claude-03
Status:     open
Severity:   medium
Surface:    specs/svf-vertical-slice/tasks.md T038 (Phase 6) vs T035 (Phase 5)

T027, T031, and T035 were each flipped to [X] with an explicit honest split ("build/automated portion verified … manual/blocked checkpoint …"). T038 — "Confirm the one-source-many-targets invariant … the identical `core/effects/svf` **built all four targets** with no per-target `#ifdef` forks (SC-001, SC-005)" — was flipped to [X] with **no** caveat. But T035 in the same diff states the two MCU targets only *compile*; their ELF link is blocked by a C-only toolchain. "Built all four targets" therefore overstates: two of the four were compile-verified, not built-and-linked.

The blast radius is spec-internal inconsistency feeding an unattended build/acceptance reader: T038 read in isolation asserts all four targets fully build, while T035 corrects it elsewhere. An agent reconciling acceptance status could record the four-target build as fully proven. T038 should carry the same compile-vs-link honesty split as T035 (e.g. "desktop targets built+linked; MCU targets compile-verified, link is the on-hardware checkpoint per T035"), so the four acceptance tasks are mutually consistent.

### core/ and MCU-adapter grep gates false-FAIL on documentation comments that merely *name* the banned platforms

Finding-ID: AUDIT-BARRAGE-claude-04
Status:     open
Severity:   medium
Surface:    scripts/check-portability.sh:28-34 (section 2), 36-43 (section 3)

Section 2 greps `core/` for `juce|libDaisy|daisy_seed|...` and section 3 greps the MCU adapters for `juce|processor-node`, both as recursive content greps with no comment/include-line discrimination. The project's own constitution language ("the DSP core knows nothing of JUCE / libDaisy / Teensy") is exactly the kind of prose a `core/` header is likely to carry as a file-banner explaining the invariant. A `core/` file with a comment `// platform-independent — never include libDaisy or daisy_seed headers` contains the literal `libDaisy` and `daisy_seed` and would trip section 2, failing the gate on a file that is *documenting* compliance. Likewise an `adapters/daisy` comment "// no ProcessorNode here; this is the MCU path" would false-FAIL section 3 on `processor-node`.

This is a bug-factory: the gate punishes the correct behavior (documenting the boundary), which trains contributors to delete the explanatory comments to appease the script. The check should target *include directives* specifically (e.g. `grep -E '^\s*#\s*include\s*[<"][^>"]*(juce|libDaisy|daisy_seed)'`) rather than any occurrence of the substring anywhere in the file.

### The per-target `#ifdef`-fork gate is scoped to `core/effects/` only, leaving `core/primitives/` and `core/dsp/` unchecked

Finding-ID: AUDIT-BARRAGE-claude-05
Status:     open
Severity:   low
Surface:    scripts/check-portability.sh:46-50 (section 4, first grep)

Section 4 checks for per-target forks with `grep -rEn '#if(def)?.*(JUCE|DAISY|TEENSY|__arm__|DESKTOP)' core/effects/`. But the SVF DaisySP wrapper lives in `core/primitives/svf-primitive.h` (per the cross-chunk file list), and the shared spine lives in `core/dsp/`. A `#ifdef DAISY` fork introduced in `core/primitives/svf-primitive.h` — exactly the kind of platform-conditional a hardware-primitive wrapper invites — would not be caught, even though SC-001/SC-005 ("one source, many targets, no per-target forks") apply to the whole shared core, not only the effect directory.

Blast radius is low because the audited code currently has no such fork and the named tasks scope the invariant to `core/effects/svf` specifically. Still, the gate's coverage is narrower than the invariant it claims to protect; widening the grep root to `core/` (it already greps `core/` for headers in section 2) closes the gap at no cost.

### no-allocation test exercises `setParameter` only on bare `SvfEffect`, never through the `EffectNode` host boundary

Finding-ID: AUDIT-BARRAGE-claude-06
Status:     open
Severity:   low
Surface:    tests/core/no-allocation-test.cpp:18-39 vs 41-58

The first TEST_CASE drives `fx.setParameter(...)` inside the measured loop, proving the bare effect's parameter path is allocation-free on the audio thread. The second TEST_CASE (`EffectNode<SvfEffect>::processBlock`) measures only `processBlock` and never calls a parameter setter, so the host boundary's parameter-application path — the one the workbench and plugin actually use from the message thread / per-block — is not asserted allocation-free. Given that commit `2fef393` ("adapter races") and the round-2 govern findings specifically touched parameter handoff at the `ProcessorNode`/`EffectNode` boundary, that path is the more interesting one for the RT-safety invariant (FR-014).

Blast radius is low: the bare-effect setter is the same code the node ultimately calls, so a regression would likely surface in test one. But the FR-014 invariant is claimed at the host boundary, and the test that names that boundary doesn't exercise its parameter path. Adding a `node.setParameter(...)` (or whatever the `ProcessorNode` parameter-ingest API is) inside the second loop would close the coverage gap and directly guard the surface the recent fixes churned.

---

I checked the four files for correctness, gate fidelity, spec/doc drift, and test coverage. The no-allocation test is structurally sound — the count is correctly captured before any `CHECK_MESSAGE` runs, `reset()` is correctly per-block-size, vectors are allocated before `reset()`, and doctest's variadic `CHECK_MESSAGE` message args are valid; my findings there are coverage, not defects. `tests/CMakeLists.txt` degrades safely when `doctest_SOURCE_DIR` is absent. The substantive findings cluster on the portability gate overstating what it verifies (grep-as-dependency-graph, text-presence-as-link, comment false-positives) and one spec-internal inconsistency (T038 vs T035) — these are what an unattended consumer trusting "gate green / all tasks [X]" would be misled by.