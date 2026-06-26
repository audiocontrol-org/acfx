### MIDI-thread SafePointer construction is fragile cross-thread coupling

Finding-ID: AUDIT-BARRAGE-claude-01
Status:     open
Severity:   medium
Surface:    adapters/workbench/workbench-app.cpp:159-165

`handleIncomingMidiMessage` runs on the JUCE MIDI thread, which is not the message thread. Inside the lambda passed to `midi_.handle`, the code constructs a `juce::Component::SafePointer<ParameterView>` directly on the MIDI thread:

```cpp
juce::Component::SafePointer<ParameterView> safeView(&paramView_);
```

`Component::SafePointer<T>` is `juce::WeakReference<T>`. In JUCE 8, the underlying shared-pointer reference counting uses atomics, so the raw data race is avoided. However, the safety of this cross-thread construction is not a documented JUCE guarantee — it is a consequence of the destructor's specific ordering (`removeMidiInputDeviceCallback` before member destruction), which couples the runtime-safety of every MIDI callback to the invariant that MIDI deregistration happens first. If a future refactor reorders the destructor, removes the `remove` call, or adds a second callback path, the same pattern becomes racy without any compile-time or documentation signal.

A self-contained fix: capture only the primitive values on the MIDI thread and create the SafePointer (or just access `paramView_` directly) inside the `callAsync` lambda, which already runs on the message thread where Component access is documented-safe:

```cpp
juce::MessageManager::callAsync([this, id, norm] {
    paramView_.setNormalized(id, norm);
});
```

The destructor's `removeMidiInputDeviceCallback` + `shutdownAudio` already ensures no `callAsync` item referencing `this` can fire after `paramView_` is destroyed, making the SafePointer unnecessary.

---

### "No silent fallback" claim is unverified when no source is available

Finding-ID: AUDIT-BARRAGE-claude-02
Status:     open
Severity:   medium
Surface:    adapters/workbench/workbench-app.cpp:67-84

The comment at line 68 explicitly promises: "else the live device input, else a surfaced error." The code implements the first two branches but the third is not present in this diff:

```cpp
if (const char* path = std::getenv("ACFX_WORKBENCH_FILE")) {
    source_.useFilePlayer(...);
} else if (inputs > 0) {
    source_.useLiveInput(inputs);
}
source_.prepare(sampleRate, blockSize);
sourceReady_ = true;
```

When `ACFX_WORKBENCH_FILE` is unset and `inputs == 0` (e.g., a headless CI machine, an audio device with output-only channels, or the common case of no microphone connected), neither branch runs. `prepare()` is called on a source in its default-constructed state, and `sourceReady_` is set to `true`. The error surface is entirely inside `WorkbenchAudioSource::prepare()`, which is not in this diff. If `prepare()` does not throw when called on an unconfigured source, the workbench silently produces undefined audio output while `sourceReady_` is `true` — exactly the silent fallback the comment and the project constitution prohibit.

Either `prepare()` must throw `AudioSourceError` when unconfigured (and that guarantee should be documented/tested), or this code should add an explicit `else` branch that throws before reaching `source_.prepare()`.

---

### CPM.cmake bootstrapper hits the network on every configure

Finding-ID: AUDIT-BARRAGE-claude-03
Status:     open
Severity:   medium
Surface:    cmake/CPM.cmake:23-29

The `file(DOWNLOAD ...)` call has no `if(NOT EXISTS "${CPM_DOWNLOAD_LOCATION}")` guard:

```cmake
file(DOWNLOAD
  "https://github.com/cpm-cmake/CPM.cmake/releases/download/v${CPM_DOWNLOAD_VERSION}/CPM.cmake"
  "${CPM_DOWNLOAD_LOCATION}"
  EXPECTED_HASH SHA256=${CPM_HASH_SUM}
)
```

In CMake, `file(DOWNLOAD)` unconditionally issues a network request on every configure invocation. The hash check validates the result but does not suppress the request when the file already exists and is valid. Consequences: (1) every `cmake --preset test` in CI adds a round-trip to GitHub releases, making configures fragile to GitHub availability; (2) offline or air-gapped builds fail on every re-configure even though the file is already on disk at `CPM_DOWNLOAD_LOCATION`. The canonical CPM bootstrap used by the upstream project wraps the download in `if(NOT EXISTS "${CPM_DOWNLOAD_LOCATION}")`. Adding that guard here makes the file a one-time fetch that survives offline reconfigures while keeping the hash verification path for first-fetch integrity.

---

### `setNormalized` min-count guard is inconsistent with combo population

Finding-ID: AUDIT-BARRAGE-claude-04
Status:     open
Severity:   low
Surface:    adapters/workbench/parameter-view.cpp:62-63

In `setNormalized`, the count used to compute the discrete index is clamped to a minimum of 2:

```cpp
const int count = row.descriptor.discreteCount < 2 ? 2 : row.descriptor.discreteCount;
int index = static_cast<int>(normalized * static_cast<float>(count));
```

But the combo box is populated with exactly `d.discreteCount` items (line 22 in the constructor):

```cpp
for (int i = 0; i < d.discreteCount; ++i)
    row.combo->addItem(juce::String(i), i + 1);
```

If `discreteCount == 1`, the combo has a single item (index 0), but `setNormalized` uses `count = 2`, so a normalized value above 0.5 produces `index = 1`, which `setSelectedItemIndex(1)` cannot satisfy (JUCE silently deselects). The `ParameterDescriptor` validation should reject `discreteCount < 2`, or `setNormalized` should use `row.descriptor.discreteCount` directly and clamp to `[0, discreteCount - 1]`, matching the constructor's population.

---

### `params_` member is stored but never read

Finding-ID: AUDIT-BARRAGE-claude-05
Status:     open
Severity:   low
Surface:    adapters/workbench/workbench-app.cpp:38, 196

`WorkbenchComponent` declares `span<const ParameterDescriptor> params_` as a member field (line 196) and assigns it in the constructor body:

```cpp
params_ = node_->parameters();
```

It is never read again. `paramView_` receives `node_->parameters()` directly in the member-initializer list, and the MIDI bindings are hardcoded. The member occupies space in the class layout and creates a confusing surface: readers wonder what `params_` is used for, and any future maintainer may feel obligated to keep it in sync with `node_`. If it is not needed, remove it and the assignment.