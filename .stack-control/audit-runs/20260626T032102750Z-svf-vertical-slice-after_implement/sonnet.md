### Discrete combo items labeled with opaque integer indices, not descriptor option names

Finding-ID: AUDIT-BARRAGE-claude-01
Status:     open
Severity:   medium
Surface:    adapters/workbench/parameter-view.cpp:18–20

The combo box items are populated with `juce::String(i)` — the loop index cast to a string — as their display text:

```cpp
for (int i = 0; i < d.discreteCount; ++i)
    row.combo->addItem(juce::String(i), i + 1); // item ids are 1-based
```

For an SVF filter's mode parameter the user sees "0", "1", "2" instead of "LP", "HP", "BP". The header comment (`parameter-view.h:10`) explicitly claims "labelled from the descriptor," but the descriptor is only consulted for `d.name` (the row label) and `d.discreteCount`; it carries no option-name table and none is constructed here. The implementation and the stated contract diverge.

Blast-radius: every discrete parameter in the workbench is permanently labeled with opaque numbers. A developer A/B-testing filter modes must either memorize index-to-mode mappings or cross-reference the source — defeating the stated goal of a self-describing auto-generated UI. If `ParameterDescriptor` is intentionally option-name-free, the comment must be corrected; if option names are a planned field, the combo construction must consume them. Either way the current state is a documentation/implementation mismatch.

---

### `params_` member assigned but never read — dead state

Finding-ID: AUDIT-BARRAGE-claude-02
Status:     open
Severity:   low
Surface:    adapters/workbench/workbench-app.cpp (WorkbenchComponent body)

`span<const ParameterDescriptor> params_` is declared as a member and assigned immediately after the initializer list:

```cpp
params_ = node_->parameters();
```

It is never referenced anywhere else in the class. The only consumers of `node_->parameters()` are the `paramView_` initializer (which calls it directly) and this assignment. The field is dead state.

Blast-radius: no functional impact, but a span whose lifetime depends on `node_` being alive could mislead a future reader into thinking it is actively used — particularly because `node_->parameters()` returns a span into `node_`-owned storage, making the field a latent use-after-free if `node_` were ever reset before the span is consumed. Remove the field entirely.

---

### `prepareToPlay` unconfigured-source path relies on an invisible `WorkbenchAudioSource` contract

Finding-ID: AUDIT-BARRAGE-claude-03
Status:     open
Severity:   medium
Surface:    adapters/workbench/workbench-app.cpp:prepareToPlay (lines ~72–92)

When neither `ACFX_WORKBENCH_FILE` is set nor any hardware audio inputs are active, neither `source_.useFilePlayer()` nor `source_.useLiveInput()` is called before `source_.prepare()`:

```cpp
if (const char* path = std::getenv("ACFX_WORKBENCH_FILE")) {
    source_.useFilePlayer(...);
} else if (inputs > 0) {
    source_.useLiveInput(inputs);
}
// ← no `else throw` or `else source_.useNull()`
source_.prepare(sampleRate, blockSize);   // relied on to throw in unconfigured case
sourceReady_ = true;
```

The comment says "else a surfaced error," but whether `prepare()` throws `AudioSourceError` when the source is not configured is a contract of `WorkbenchAudioSource` that is invisible in this chunk. If `prepare()` returns normally when the source is unconfigured, `sourceReady_` is set to `true` and `getNextAudioBlock` will call `source_.fillBlock()` on an unconfigured source, likely outputting undefined audio data through the SVF — a silent failure that contradicts the project constitution's prohibition on silent fallbacks.

Blast-radius: on a machine with no audio inputs and no `ACFX_WORKBENCH_FILE` (common on a developer laptop that has only output channels), the workbench might output garbage audio rather than a clear error dialog. The fix is to add an explicit `else { throw AudioSourceError("..."); }` branch in `prepareToPlay` itself so the guarantee is visible at the call site, regardless of what `prepare()` does.

---

### CPM bootstrap download has no timeout — CI hangs possible

Finding-ID: AUDIT-BARRAGE-claude-04
Status:     open
Severity:   low
Surface:    cmake/CPM.cmake:23–28

The `file(DOWNLOAD ...)` call that bootstraps CPM specifies no `TIMEOUT` or `INACTIVITY_TIMEOUT`:

```cmake
file(DOWNLOAD
  "https://github.com/cpm-cmake/CPM.cmake/releases/download/v${CPM_DOWNLOAD_VERSION}/CPM.cmake"
  "${CPM_DOWNLOAD_LOCATION}"
  EXPECTED_HASH SHA256=${CPM_HASH_SUM}
)
```

If GitHub is unreachable or throttling (common in air-gapped CI, corporate proxies, or transient outages), CMake will block the configure step indefinitely. The SHA256 pin provides integrity protection but does nothing to bound how long a hung TCP connection stalls the build.

Blast-radius: a CI job hangs until the runner's overall job timeout kills it, with no actionable error message. Adding `TIMEOUT 60` (or `INACTIVITY_TIMEOUT 30`) would surface a clean CMake fatal error within a predictable window and let the operator retry or populate `CPM_SOURCE_CACHE` from a mirror.