### CI `Install JUCE prerequisites` step is a no-op that hides actual build requirements

Finding-ID: AUDIT-BARRAGE-claude-01
Status:     open
Severity:   medium
Surface:    .github/workflows/ci.yml:38-41

The `desktop-build` job's `Install JUCE Linux/macOS build prerequisites` step executes `echo "macOS runner ships the required frameworks"` — a string that asserts correctness rather than enforcing it. This is exactly the "fallback that hides failure modes" anti-pattern called out by the project guidelines. If a future macOS runner image removes Xcode command-line tools, drops a required SDK version, or the project adds a JUCE feature that requires an explicit apt/brew install, this step silently passes and the failure surfaces only at `cmake --preset desktop` with a confusing configure error — not at a clearly labelled prerequisites step. The blast radius is operational: a CI maintainer chasing a macOS configure failure will waste time before realising the "install" step did nothing. The correct fix is either to remove the step entirely (its comment belongs in a `CONTRIBUTING.md` or the `README`), or to replace it with the explicit `xcode-select --install` / brew prerequisite commands the build actually requires (even if currently empty, a commented list of required tools is more honest than an `echo`).

---

### GitHub Actions `actions/checkout` uses a floating major-version tag, not a SHA-pinned digest

Finding-ID: AUDIT-BARRAGE-claude-02
Status:     open
Severity:   medium
Surface:    .github/workflows/ci.yml:19, 31, 38

All three jobs pin `actions/checkout@v4`, a mutable tag. If the `v4` tag on the upstream `actions/checkout` repository is moved (by a supply-chain compromise or accidental force-push), arbitrary code runs with the full permissions of the CI runner on every subsequent push. For this repository the blast radius is bounded — CI does not appear to hold deployment secrets or publish artifacts — but the runner could still be used to exfiltrate CPM-fetched source archives or abuse GitHub API rate limits. The standard mitigation is to pin to an immutable SHA digest: `actions/checkout@11bd71901bbe5b1630ceea73d27597364c9af683` (v4.2.2 at time of writing) and keep it updated via Dependabot or Renovate. The finding applies equally to any other actions that are added to this file in future; the pattern should be established now.

---

### `maybeSet` couples ADC hardware channel index to `lastKnob[]` slot implicitly, with no structural enforcement

Finding-ID: AUDIT-BARRAGE-claude-03
Status:     open
Severity:   low
Surface:    adapters/daisy/daisy-main.cpp:31-36, 49-52

`maybeSet(acfx::SvfEffect::Param param, int adc)` uses `adc` for two independent purposes: as the hardware ADC channel selector (`hw.adc.GetFloat(adc)`) and as the index into the `lastKnob[]` debounce array. The three call sites pass `kAdcCutoff=0`, `kAdcResonance=1`, `kAdcMode=2` — values that coincide with the `AdcChannel` enum but are structurally unrelated to it. If the `SvfEffect::Param` enum is extended and the call site is updated without maintaining the (param, adc) pairing, the wrong `lastKnob[]` slot gets checked and updated, causing debounce state to bleed between parameters — a silent RT misbehaviour with no assertion to catch it. A low-cost fix: change the call site so the `AdcChannel` enum value (not a raw int) drives both ADC access and `lastKnob[]`, and let `maybeSet` map `AdcChannel→Param` internally via a small lookup table, making the pairing a single place of truth.

---

### No CPM dependency cache in CI — every run re-fetches JUCE (~130 MB) cold

Finding-ID: AUDIT-BARRAGE-claude-04
Status:     open
Severity:   informational
Surface:    .github/workflows/ci.yml (all three jobs)

Neither the `core-tests` nor `desktop-build` job caches the CPM source cache directory (`external/.cpm-cache`). JUCE is the largest dependency (≈130 MB of source) and is fetched on every CI invocation. Beyond wall-clock cost, this makes CI fragile against transient GitHub CDN or upstream-repo availability, and consumes runner bandwidth quota. A standard GitHub Actions `actions/cache` step keyed on `cmake/dependencies.cmake` hash and the CPM cache path would eliminate the download for the common case. This does not affect correctness — it is an operational and reliability concern.