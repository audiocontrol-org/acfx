### CI core-tests job runs only on macOS — platform-independence claim unvalidated by CI

Finding-ID: AUDIT-BARRAGE-claude-01
Status:     open
Severity:   medium
Surface:    .github/workflows/ci.yml:14-24

The project's central architectural claim is a platform-independent core that compiles equally on desktop and ARM embedded targets. The `core-tests` job (lines 14–24) runs only on `macos-latest`. Linux CI would exercise a different toolchain (GCC vs Clang), different `char` signedness defaults, stricter aliasing enforcement, and different alignment padding — all of which are real divergence surfaces for a codebase targeting ARM. The `portability-gate` job (lines 26–32) runs on `ubuntu-latest` but only executes the shell script `check-portability.sh`, not the C++ test suite. The full doctest suite never runs under a Linux compiler in CI.

If a core header introduces a macOS-only implicit dependency (a framework type, a `__attribute__` extension, an endianness assumption), CI passes silently and the failure only surfaces when cross-compiling for Daisy or Teensy. A second matrix entry for `ubuntu-latest` on `core-tests` would close this gap with no code changes required.

---

### No-op CI step named "Install JUCE Linux/macOS build prerequisites"

Finding-ID: AUDIT-BARRAGE-claude-02
Status:     open
Severity:   low
Surface:    .github/workflows/ci.yml:36-37

```yaml
- name: Install JUCE Linux/macOS build prerequisites
  run: echo "macOS runner ships the required frameworks"
```

This step does nothing: it echoes a reassurance string and exits. A reader or operator who needs to reproduce the build locally, port CI to a new runner, or audit what is being installed sees a step name that implies action ("Install … prerequisites") pointing to a body that skips it. This is the same operator-discipline trap as a comment claiming a TODO is handled when it isn't — the name and the body contradict each other. The step should either be removed (if truly nothing is needed), or replaced with actual prerequisite installation commands and a comment explaining why the macOS hosted runner already satisfies them.

---

### Feature branch name hardcoded in CI push trigger

Finding-ID: AUDIT-BARRAGE-claude-03
Status:     open
Severity:   low
Surface:    .github/workflows/ci.yml:11-13

```yaml
on:
  push:
    branches: [platform-foundation, main]
```

`platform-foundation` is the current feature branch. Once this branch merges to `main`, the trigger entry becomes dead configuration that will never fire. Future contributors seeing this may interpret it as an intentional permanent branch, or may inadvertently assume any new branch they create also needs to be listed. The `pull_request:` trigger (line 13, no filter) already catches pre-merge builds. After merge the push trigger on `main` alone is sufficient. The hardcoded feature branch name should be removed before or immediately after merge.

---

### Conditional `if(TARGET DaisySP)` guard silently non-links rather than failing

Finding-ID: AUDIT-BARRAGE-claude-04
Status:     open
Severity:   medium
Surface:    CMakeLists.txt:54-57

```cmake
if(TARGET DaisySP)
  target_link_libraries(acfx_core INTERFACE DaisySP)
endif()
```

`acfx_core` is an INTERFACE library whose headers (per the README) include `core/primitives/svf-primitive.h`, which wraps DaisySP's `Svf`. The `if(TARGET DaisySP)` guard makes linking conditional on whether DaisySP was fetched by `cmake/dependencies.cmake`. If DaisySP is not fetched for a given preset, `acfx_core` silently skips the `target_link_libraries` call — but the headers still try to `#include` DaisySP headers, producing an opaque "file not found" compile error rather than a clear CMake-time diagnostic.

The blast radius: a contributor who adds a new CMake preset that doesn't trigger DaisySP fetch, or who edits `cmake/dependencies.cmake` to conditionally skip DaisySP, gets a confusing header-not-found error at compile time with no pointer to the root cause. The correct pattern is `find_package(DaisySP REQUIRED)` or an unconditional fetch — fail loudly at configure time rather than silently at compile time. As written, the guard creates a false appearance of optionality around a mandatory dependency.

---

### Governance convergence JSON frozen at `rounds: 1` with four unresolved HIGH findings despite three subsequent fix commits

Finding-ID: AUDIT-BARRAGE-claude-05
Status:     open
Severity:   medium
Surface:    .stack-control/govern/convergence/impl__design-feature-svf-vertical-slice.json:9,19-48

The governance JSON records `"rounds": 1` and carries four entries in `liftedFindings` — all rated HIGH — none of which appear in `closedInLoopFindings`. The commit log shows three subsequent rounds of fixes:

- `bd79479 Address govern findings: RT-safety, thread ownership, doc drift`
- `2fef393 Address round-2 govern findings: RT-safety, error surfacing, adapter races`
- `2406235 Address round-3 govern findings: precise contracts, lock-free atomics, enforced precondition`

The governance artifact was not re-run or updated after any of these rounds. Its `outcome: "override-eligible"` and `liftedFindings` block therefore describe the state of the codebase as of round 1, not the state after three further fix cycles. If the governance tooling or a future operator reads this file to assess convergence status, they will see four open HIGH findings and an override-eligible outcome — not a clean pass — even though the code claims to have resolved them. The file should be regenerated (or its `rounds`, `closedInLoopFindings`, and `outcome` fields updated) to reflect the actual post-fix state, so the governance artifact and the code agree.