I'll walk the diff carefully for this chunk (parameter-view.cpp, parameter-view.h, workbench-app.cpp, cmake/CPM.cmake, cmake/dependencies.cmake, cmake/toolchains/daisy.cmake, cmake/toolchains/teensy.cmake, core/dsp/audio-block.h) and report findings independently.

---

### `prepareToPlay` catches `AudioSourceError` and stores it in a field that is never read

Finding-ID: AUDIT-BARRAGE-claude-01
Status:     open
Severity:   high
Surface:    adapters/workbench/workbench-app.cpp:68–73, 156

`prepareToPlay` wraps the audio-source setup in a try/catch and stores the error message in `lastSourceError_`:

```cpp
} catch (const AudioSourceError& e) {
    lastSourceError_ = juce::String(e.what());
}
```

`lastSourceError_` is declared as a private `juce::String` field and never read anywhere in the file — there is no `paint()`, no label, no alert, no logger call, no assertion. The operator has no indication that audio source setup failed; the workbench just silently produces silence. Per the project rule in CLAUDE.md: *"Never implement fallbacks or mock data outside of test code. Throw errors with a description of the missing functionality. Errors let us know that something isn't implemented. Fallbacks and mock data are bug factories."* A field that stores an error and then discards it is a fallback that hides failures. The fix is to surface the error on the message thread — e.g., via a `juce::AlertWindow::showMessageBoxAsync` or a persistent error label in the UI — rather than swallowing it into an unread string.

---

### `getNextAudioBlock` silently falls back to cleared buffer on `AudioSourceError`

Finding-ID: AUDIT-BARRAGE-claude-02
Status:     open
Severity:   high
Surface:    adapters/workbench/workbench-app.cpp:89–95

Inside the real-time audio callback:

```cpp
try {
    // ...
    source_.fillBlock(region);
} catch (const AudioSourceError&) {
    buffer.clear(startSample, numSamples);
    return;
}
```

The exception message is not stored, not surfaced on the message thread, and the workbench produces silence with zero operator feedback. This is a stricter violation than Finding-01: at least `prepareToPlay` stored the string. Here the error is discarded entirely. The pattern used in `handleIncomingMidiMessage` — `juce::MessageManager::callAsync` with a `SafePointer` — is the correct mechanism to bridge RT-callback failures to the GUI thread. The fix is to post the error to the message thread for display rather than silently clearing the buffer.

---

### Dead field `params_` in `WorkbenchComponent`

Finding-ID: AUDIT-BARRAGE-claude-03
Status:     open
Severity:   medium
Surface:    adapters/workbench/workbench-app.cpp:38, 152

The constructor assigns:

```cpp
params_ = node_->parameters();
```

and the field is declared as `span<const ParameterDescriptor> params_`. `params_` is never read anywhere in the class; `paramView_` already holds the parameter table and is used for all parameter operations. This is dead code. Beyond the minor code-smell, the field holds a `span` (a non-owning view) into `node_->parameters()`. If `node_` is ever destroyed and replaced (e.g., for preset switching), `params_` would hold a dangling view while the reader might not realize it — a latent lifetime hazard if the field is ever picked up and used. Remove the field.

---

### Discrete parameter round-trip encoding asymmetry between combo callback and `setNormalized`

Finding-ID: AUDIT-BARRAGE-claude-04
Status:     open
Severity:   medium
Surface:    adapters/workbench/parameter-view.cpp:27–29, 55–59

The combo's `onChange` encodes with **center-of-bucket**:

```cpp
const float norm = (static_cast<float>(index) + 0.5f) / static_cast<float>(count);
```

But `setNormalized()` decodes with **floor**:

```cpp
int index = static_cast<int>(normalized * static_cast<float>(count));
```

These are not exact inverses. For center-of-bucket values themselves the round-trip is fine (`floor((i+0.5)/count * count) == i`). But any externally-supplied value that is not a center-of-bucket — for example a MIDI CC 0 (norm=0.0), or a value serialized by the DAW plugin — will decode correctly in `setNormalized` (index 0 for norm=0.0) yet if the user then touches the control, the combo re-encodes back to `0.5/count`, shifting the value. A plugin (adapters/plugin/) that persists parameter values in normalized form and restores them via a symmetric setNormalized path would see the workbench re-encode at a different position than the DAW stored. The fix is to make both sides use the same encoding, or to define one canonical form (center-of-bucket throughout, decoding by `floor(norm * count + 0.5)` or similar nearest-bucket mapping).

---

### `AudioBlock::channel()` has no bounds guard in debug builds

Finding-ID: AUDIT-BARRAGE-claude-05
Status:     open
Severity:   low
Surface:    core/dsp/audio-block.h:22

```cpp
float* channel(int ch) const noexcept { return channels_[ch]; }
```

There is no `JASSERT`, `assert`, or any static-analysis annotation. `AudioBlock` is the primary data-path type (FR-002) consumed by every adapter. An out-of-bounds `ch` in any `process()` loop corrupts memory silently with no diagnostic in debug builds. Since all process paths are internal, the risk is low in practice, but a `jassert(ch >= 0 && ch < numChannels_)` costs nothing in release builds and catches callers immediately in debug builds, which is the standard pattern for RT-safe DSP handle types.