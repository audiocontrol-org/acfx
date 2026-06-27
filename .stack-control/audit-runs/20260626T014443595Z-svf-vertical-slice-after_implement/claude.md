I walked chunk `6a56babffbf5b038` (CI, CMake top-level, presets, README, .clang-format/.editorconfig, the daisy adapter, the plugin CMakeLists, and the committed govern convergence record). Findings below.

### `acfx_core` links DaisySP behind a silent `if(TARGET DaisySP)` guard — a missing dependency degrades to cryptic link errors instead of a loud configure-time failure

Finding-ID: AUDIT-BARRAGE-claude-01
Status:     open
Severity:   medium
Surface:    CMakeLists.txt:55-62

The core links its only hard dependency conditionally:

```cmake
add_library(acfx_core INTERFACE)
...
if(TARGET DaisySP)
  target_link_libraries(acfx_core INTERFACE DaisySP)
endif()
```

`core/primitives/svf-primitive.h` wraps DaisySP's `Svf`, whose `Init`/`Process` are compiled in DaisySP's `.cpp` files, so every consumer of `acfx_core` (tests, workbench, plugin, daisy) *must* link `DaisySP`. Because `acfx_core` is an `INTERFACE` target, it configures and "builds" fine even when `DaisySP` is not a target — the failure only surfaces as undefined-reference linker errors deep in a *downstream* target, with no hint that the root cause is an unfetched dependency. This is exactly the silent-fallback shape the project bans ("raise descriptive errors for missing functionality instead"). Contrast `adapters/daisy/CMakeLists.txt:9-13`, which does it correctly with `message(FATAL_ERROR ...)` when `libDaisy_SOURCE_DIR` is undefined. Blast radius: an adopter on a misconfigured preset (or after editing `cmake/dependencies.cmake`) wastes time chasing linker errors that a one-line `if(NOT TARGET DaisySP) message(FATAL_ERROR ...)` would have named at configure time. The fix is to make the guard fail loud rather than skip silently, matching the daisy adapter's precedent.

### Govern convergence record pins `governedShaBase` to a resolved SHA but stores `headSha: "HEAD"` — the "what was governed" record is not reproducible

Finding-ID: AUDIT-BARRAGE-claude-02
Status:     open
Severity:   medium
Surface:    .stack-control/govern/convergence/impl__design-feature-svf-vertical-slice.json:6-7

```json
"governedShaBase": "ff3426a",
"headSha": "HEAD",
```

The base is a concrete commit, but the head end of the governed range is the unresolved symbolic ref `"HEAD"`. A symbolic ref is only meaningful at the instant it was written; once any further commit lands (and the commit log shows three rounds of post-govern fix commits — `bd79479`, `2fef393`, `2406235`), `"HEAD"` no longer denotes the code that was actually governed. A downstream consumer — the re-audit / verification step that reads these convergence records to reconstruct "which tree passed governance" — cannot pin or diff the governed range. Relatedly, `"rounds": 1` sits in the same record while the commit subjects describe round-2 and round-3 govern-finding fixes, so the record's round count also reads as stale relative to history. Blast radius: an unattended verification tool comparing the recorded range against the current tree gets a meaningless (or always-moving) head endpoint and either errors or silently validates the wrong tree. The fix is to resolve `HEAD` to a commit SHA at write time (as is already done for the base) so the record is reproducible.

### CI "Install JUCE Linux/macOS build prerequisites" step is a placeholder no-op that echoes instead of installing anything

Finding-ID: AUDIT-BARRAGE-claude-03
Status:     open
Severity:   low
Surface:    .github/workflows/ci.yml:39-41

```yaml
- name: Install JUCE Linux/macOS build prerequisites
  run: echo "macOS runner ships the required frameworks"
```

The step name claims to install prerequisites for both Linux *and* macOS, but the body installs nothing and only narrates an assumption. The job runs exclusively on `macos-latest` (line 36), so the "Linux" half of the step name is dead, and the macOS half relies on Xcode shipping the needed frameworks rather than declaring them. This is a placeholder masquerading as a real setup step — the kind of operator-discipline trap the audit guidance calls out. Blast radius is contained today (the build presumably succeeds on a stock macOS runner), but the moment a Linux runner is added or JUCE needs an explicit system package, this no-op lets configuration proceed to a confusing downstream build failure rather than failing at an honest "install" step. Either delete the step (let JUCE's own requirements fail loudly) or make it actually install the dependencies it names.

### CI header comment uses the deferral phrase "a deferred follow" to describe absent hardware build-checks

Finding-ID: AUDIT-BARRAGE-claude-04
Status:     open
Severity:   low
Surface:    .github/workflows/ci.yml:3-7

```
# ... Hardware presets are build-checked only
# where ARM toolchains are provisioned — a deferred follow noted in quickstart.md,
# so the boundary is visible rather than silently dropped (FR-015).
```

The audit constraints flag deferral phrases in the diff as bug-factory smells. The comment honestly discloses that the daisy/teensy presets are *not* build-checked in this workflow (no ARM job exists, conditional or otherwise), which is good, but it frames the gap as "a deferred follow" — a commitment to deferred work embedded in a checked-in artifact. Unlike the README's "separate checkpoint when hardware is in hand" (which describes a hardware-gated milestone), "deferred follow" reads as a TODO with no tracking surface. Blast radius is low because the boundary itself is genuinely disclosed and tied to FR-015; the concern is purely that the deferral is recorded as a floating intention rather than as a tracked roadmap/backlog item. Reword to point at the concrete tracking item (e.g. the FR/issue that owns provisioning ARM toolchains) instead of the open-ended "deferred follow."

---

I specifically checked the daisy audio callback for RT-safety and correctness (`AudioCallback` does in-place copy then `process`, no heap/locks; `maybeSet` dead-bands knob reads off a non-blocking ADC cache; `prepare()` is called after `SetAudioBlockSize(48)` so the prepared block size matches) and found it clean. I checked preset/option wiring (each preset flips the matching `ACFX_BUILD_*` option; C++ standard 17-vs-20 reconciles since 20 ≥ the core's `cxx_std_17`), the README's CI claim against `ci.yml` (host tests + portability + desktop build — accurate), and `.gitignore` vs `CPM_SOURCE_CACHE` (consistent). Those came back clean; the findings above are the ones worth triaging.