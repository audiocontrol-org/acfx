### No-source path in `prepareToPlay` relies on unseen `WorkbenchAudioSource::prepare()` throwing

Finding-ID: AUDIT-BARRAGE-claude-01
Status:     open
Severity:   medium
Surface:    adapters/workbench/workbench-app.cpp:74-97 (the `prepareToPlay` source-selection block)

When `ACFX_WORKBENCH_FILE` is unset and `numInputChannels()` returns 0 (headless CI run, machine with output-only audio device, `AudioAppComponent` before the device opens), neither the file-player nor the live-input branch executes. `source_.prepare(sampleRate, blockSize)` is then called on a `WorkbenchAudioSource` that has been neither configured as a file player nor as a live-input capture. Whether `prepare()` throws `AudioSourceError` in that state is entirely determined by the `WorkbenchAudioSource` implementation, which is not visible in this chunk. If it throws, the catch block surfaces the error and `sourceReady_` stays false — acceptable. If it does not throw, `sourceReady_` is set to `true` and `getNextAudioBlock` subsequently calls `source_.fillBlock(region)` on an object with no configured source, whose behavior is undefined.

A correct fix does not require `WorkbenchAudioSource` to carry that obligation. The call-site should raise the error explicitly before reaching `source_.prepare`:
```cpp
if (!path && inputs == 0)
    throw AudioSourceError("No audio source: set ACFX_WORKBENCH_FILE or connect an input device");
```
This satisfies Constitution §V (no silent fallbacks), makes the invariant visible without reading the hidden implementation, and prevents the precondition leak.

---

### Dead `params_` member is assigned but never read

Finding-ID: AUDIT-BARRAGE-claude-02
Status:     open
Severity:   low
Surface:    adapters/workbench/workbench-app.cpp:35 (assignment), ~172 (declaration)

`params_ = node_->parameters()` is called in the constructor body after `paramView_` is already initialized in the member-initializer list via `node_->parameters()` directly. `params_` is a `span<const ParameterDescriptor>` member that is assigned exactly once and subsequently never read by any method. The field has zero runtime consequence but creates a maintenance trap: a reader scanning the class infers that `params_` must be consumed somewhere (why else store it?), wasting attention during future edits or audits. Remove the field and the assignment entirely; `paramView_` already holds the view it needs.

---

### `setNormalized` floor-decoding is inconsistent with the combo lambda's centre-of-bucket encoding

Finding-ID: AUDIT-BARRAGE-claude-03
Status:     open
Severity:   low
Surface:    adapters/workbench/parameter-view.cpp:22-33 (encoding), 52-60 (decoding)

The `ComboBox::onChange` lambda (line 28–31) encodes the selected index as the centre of its bucket:
```cpp
const float norm = (static_cast<float>(index) + 0.5f) / static_cast<float>(count);
```
`setNormalized` (line 54–57) decodes by flooring:
```cpp
int index = static_cast<int>(normalized * static_cast<float>(count));
```
The centre encoding was chosen specifically so that any normalized value the SVF effect internally snaps the discrete parameter to is unambiguously within one bucket; the floor decoding is safe for all values produced by the centre encoding. However, `setNormalized` also applies its own `discreteCount < 2 ? 2 : count` guard that the encoding lambda omits. For `discreteCount == 1` (a combo constructed with exactly one item at index 0), the encoding uses `count = 1` and produces `norm = 0.5`, but decoding substitutes `count = 2`, maps `norm = 0.5` to `index = 1`, and calls `setSelectedItemIndex(1, …)` on a single-item combo. JUCE silently ignores the out-of-bounds index, leaving the displayed selection unchanged. In practice `discreteCount == 1` is semantically degenerate, but the contract between encoder and decoder should be stated once and shared — the inconsistency will mislead the next developer who touches this arithmetic.

---

### All available MIDI inputs are bulk-enabled at startup without operator control

Finding-ID: AUDIT-BARRAGE-claude-04
Status:     open
Severity:   low
Surface:    adapters/workbench/workbench-app.cpp:57-61

```cpp
for (const auto& input : juce::MidiInput::getAvailableDevices())
    deviceManager.setMidiInputDeviceEnabled(input.identifier, true);
deviceManager.addMidiInputDeviceCallback({}, this);
```

Every MIDI device present on the machine is opened unconditionally. This works for the intended workbench use-case (quick CC-to-filter mapping) but is surprising in shared-studio environments: a hardware controller sending CCs on channels bound to different parameters in another DAW instance would invisibly drive the workbench's filter. The blast-radius here is low (the workbench is a single-effect sketch tool), but opening hardware devices without user consent violates the principle of least surprise and may interfere with other running applications that expect exclusive device ownership. The fix is standard JUCE practice: use `AudioDeviceSelectorComponent` or an `AudioDeviceManager` XML settings restore to let the operator choose which inputs to enable.