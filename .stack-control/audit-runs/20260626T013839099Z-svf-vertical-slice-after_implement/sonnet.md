### `prepareToPlay` continues into audio callback with broken source state

Finding-ID: AUDIT-BARRAGE-claude-01
Status:     open
Severity:   medium
Surface:    adapters/workbench/workbench-app.cpp:63-79

The `catch (const AudioSourceError& e)` block in `prepareToPlay` schedules a dialog and then returns normally, leaving `source_` in an indeterminate or un-prepared state. After this, JUCE will still invoke `getNextAudioBlock`, which calls `source_.fillBlock(region)` unconditionally. If `source_.useLiveInput` or `source_.prepare` threw before completing, `fillBlock` is called on an object that was not successfully initialized — undefined behavior at best, crash at worst.

The comment says "no silent fallback to silence (Constitution V)" but the actual behavior is exactly that: the exception is consumed, the dialog is async, and audio processing continues silently. A correct implementation would either set a flag to skip `fillBlock` (and emit silence deliberately, with logging) or re-throw so JUCE knows the device failed to initialize. The current code violates both the stated goal and the project guideline against swallowing errors.

Blast-radius: every audio callback after a failed `prepare` runs on a broken source. On systems where live-input fails (e.g. CI, headless, or a fresh install without a connected interface), this is triggered on every launch.

---

### Division by zero when `discreteCount == 0` in combo `onChange` lambda

Finding-ID: AUDIT-BARRAGE-claude-02
Status:     open
Severity:   medium
Surface:    adapters/workbench/parameter-view.cpp:25-29

`count` is captured as `const std::uint8_t count = d.discreteCount` (line 26). The lambda at line 29 computes:

```cpp
const float norm = (static_cast<float>(index) + 0.5f) / static_cast<float>(count);
```

If a `ParameterDescriptor` with `kind == discrete` but `discreteCount == 0` is ever constructed, `count` is zero. A ComboBox with no items still has its internal state, and JUCE can dispatch `onChange` (e.g. programmatic `setSelectedItemIndex` on an empty box may fire). `getSelectedItemIndex()` returns −1 in that case, yielding `(−1 + 0.5) / 0.0 == −Infinity`, which is then forwarded to the effect's `setParameter`. IEEE 754 guarantees no hardware trap, but the effect receives a NaN-family value with no validation upstream.

The guard in `setNormalized` (line 62, `discreteCount < 2 ? 2 : discreteCount`) implicitly acknowledges that zero and one are degenerate values, but the corresponding guard is absent in the constructor's lambda. A consistent fix would either assert/throw in the constructor when `discreteCount == 0` (preferring the project-guideline approach of surfacing the error) or add the same clamp there.

Blast-radius: the bug is latent unless a bad descriptor reaches the workbench. Given that descriptors come from the effect's own table, the current `SvfEffect` is unlikely to emit `discreteCount == 0`. The risk is at the abstraction boundary — any new effect wired to this `ParameterView` with a malformed discrete descriptor will silently send infinity to `setParameter`.

---

### `setNormalized` silently corrects invalid `discreteCount` instead of raising an error

Finding-ID: AUDIT-BARRAGE-claude-03
Status:     open
Severity:   low
Surface:    adapters/workbench/parameter-view.cpp:62

```cpp
const int count = row.descriptor.discreteCount < 2 ? 2 : row.descriptor.discreteCount;
```

This clamps a degenerate count of 0 or 1 to 2 rather than surfacing the invariant violation. Per project guidelines ("never implement fallbacks; throw descriptive errors instead"), the correct behaviour when an invariant is violated at a module boundary is to signal the error, not to silently repair it. A value of `discreteCount == 0` for a `ParamKind::discrete` descriptor indicates a broken descriptor table; masking it with a default count produces a wrong index-to-normalized mapping that corrupts parameter state without any trace.

The fix is a `JUCE_ASSERT` or `throw` on entry when `discreteCount < 2` rather than a silent substitution.

Blast-radius: low in isolation — wrong normalized value written to effect parameters when a bad descriptor slips through — but the silent nature of the correction is the code-quality concern: it makes future descriptor errors hard to detect.

---

### Dead `params_` member field — set but never read

Finding-ID: AUDIT-BARRAGE-claude-04
Status:     open
Severity:   low
Surface:    adapters/workbench/workbench-app.cpp:33, 160

```cpp
params_ = node_->parameters();
```

`params_` (declared as `span<const ParameterDescriptor> params_` at line 160) is assigned in the constructor body but is never referenced anywhere else in the class. The `paramView_` is initialized directly with `node_->parameters()` in the member-initializer list (line 38), and `midi_` bindings use hard-coded `SvfEffect::k*` constants. The field exists purely as unused state, adding noise to the class layout and potentially confusing a future reader who assumes it is consumed somewhere.

The field should be removed.

Blast-radius: no runtime consequence, but dead fields in adapter classes are a maintenance trap — a future refactor might add code that reads it under the assumption it was kept current, when the value is stale after any `node_` swap.

---

### `cmake/dependencies.cmake` comment contradicts unconditional DaisySP fetch

Finding-ID: AUDIT-BARRAGE-claude-05
Status:     open
Severity:   low
Surface:    cmake/dependencies.cmake:20-23

The file's header comment states:

> Dependencies are fetched lazily: a dependency is only declared when a target that needs it is enabled, so the `test` preset pulls only DaisySP + doctest.

The word "lazily" implies DaisySP is conditionally fetched like the other dependencies. But DaisySP is declared unconditionally — no `if(ACFX_BUILD_*)` guard — and the comment then immediately reveals this by saying "the `test` preset pulls only DaisySP + doctest." The two sentences contradict each other: the first implies lazy/conditional; the second reveals the exception that proves unconditional fetching.

This is documentation drift. The comment should say DaisySP is always fetched (it is the one unconditional dependency) and explain why — it is a core DSP primitive used by all targets — rather than claiming a "lazy" model that doesn't apply to it.

Blast-radius: low for current consumers who can read the CMake directly. Higher for automated agents using this file as input to understand the dependency model: "lazy" will cause an agent to incorrectly conclude DaisySP can be excluded from a `ACFX_BUILD_TESTS`-only configuration.

---

### `cmake/CPM.cmake`: `file(DOWNLOAD)` always runs — no pre-existence check

Finding-ID: AUDIT-BARRAGE-claude-06
Status:     open
Severity:   informational
Surface:    cmake/CPM.cmake:23-28

CMake's `file(DOWNLOAD ...)` fetches the remote URL every time it is evaluated, even if the destination file already exists and the hash is valid. There is no preceding `if(NOT EXISTS ...)` guard or `STATUS` check. On a warm developer machine with `CPM_SOURCE_CACHE` set, the cache path prevents repeated fetches. But without `CPM_SOURCE_CACHE`, the file lands in `${CMAKE_BINARY_DIR}/cmake/` and is re-fetched on every fresh configure (cmake clean, new build directory, or CI). The `EXPECTED_HASH SHA256=...` check only verifies after download; it does not skip the network request.

In an air-gapped or offline build environment (embedded-hardware CI for the Daisy/Teensy targets), this will fail the configure step even if CPM was previously cached.

A standard guard:
```cmake
if(NOT (EXISTS "${CPM_DOWNLOAD_LOCATION}" AND ...))
  file(DOWNLOAD ...)
endif()
```
avoids repeated fetches and preserves offline usability. The exact idiom appears in CPM's own bootstrap templates.