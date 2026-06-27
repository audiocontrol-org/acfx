### setNormalized computes index against count=max(2,discreteCount) but combo holds only discreteCount items

Finding-ID: AUDIT-BARRAGE-claude-01
Status:     open
Severity:   medium
Surface:    adapters/workbench/parameter-view.cpp:55-65 and :17-22

In the constructor, combo items are added for indices 0 to `d.discreteCount-1`:

```cpp
for (int i = 0; i < d.discreteCount; ++i)
    row.combo->addItem(juce::String(i), i + 1);
```

So a `discreteCount == 1` parameter gets a single item at index 0. In `setNormalized`, however, the denominator is floored to 2:

```cpp
const int count = row.descriptor.discreteCount < 2 ? 2 : row.descriptor.discreteCount;
int index = static_cast<int>(normalized * static_cast<float>(count));
if (index >= count)
    index = count - 1;
row.combo->setSelectedItemIndex(index, juce::dontSendNotification);
```

For `discreteCount == 1` and any `normalized >= 0.5`, `index` resolves to 1, which is `>= count (2)` — wait, the clamp fires only when `index >= count == 2`, so for `normalized = 0.5`, `index = floor(0.5 * 2) = 1`, the clamp does not fire, and `setSelectedItemIndex(1, …)` is called on a combo with only item-index 0 present. JUCE will silently deselect (or do nothing), leaving the display inconsistent with the actual parameter value pushed through `node_->setParameter`. The blast radius is incorrect visual feedback for any effect that exposes a single-choice discrete parameter; the error is silent (no assert or log line).

The fix is to use `row.descriptor.discreteCount` directly as the divisor in `setNormalized` (matching the constructor), or to guard against `discreteCount < 1` at descriptor-construction time and cap the visual read-back to `discreteCount - 1`.

---

### MidiBinding::bind() is publicly mutable but bindings_ is read lock-free on the MIDI thread

Finding-ID: AUDIT-BARRAGE-claude-02
Status:     open
Severity:   medium
Surface:    adapters/workbench/midi-binding.h:24 and adapters/workbench/workbench-app.cpp:45-46

`bind()` writes to `bindings_` (an `std::unordered_map`) without any synchronization mechanism:

```cpp
void bind(int ccNumber, ParamId id) { bindings_[ccNumber] = id; }
```

`handle()` reads `bindings_` on the JUCE MIDI callback thread:

```cpp
const auto it = bindings_.find(msg.getControllerNumber());
```

In the current call-sites, both `bind()` calls happen inside `WorkbenchComponent`'s constructor before `addMidiInputDeviceCallback` is registered, so the initialization is sequentially consistent and the code is safe as written. But `bind()` is `public`, carrying no documentation that calls after construction are forbidden. A UI action (e.g., a future "remap CC" dialog) that calls `bind()` on the message thread while `handle()` runs on the MIDI thread would be a data race on the map, producing undefined behaviour. The interface contracts ownership but not the threading discipline required to uphold it.

A minimal fix is to add a comment that `bind()` is single-writer–construction-only, or change the design so `bind()` accepts a `const std::unordered_map<int, ParamId>&` to install the binding table atomically (store a `std::atomic<const Map*>` or swap under a mutex). An `informational` note would suffice if the intent is never to call `bind()` post-construction — but the public API does not communicate that.

---

### Fragile assumption: WorkbenchAudioSource::prepare() must throw when neither source branch is taken

Finding-ID: AUDIT-BARRAGE-claude-03
Status:     open
Severity:   medium
Surface:    adapters/workbench/workbench-app.cpp:81-97

The source-selection block in `prepareToPlay` is:

```cpp
if (const char* path = std::getenv("ACFX_WORKBENCH_FILE")) {
    source_.useFilePlayer(juce::File(juce::String::fromUTF8(path)));
} else if (inputs > 0) {
    source_.useLiveInput(inputs);
}
source_.prepare(sampleRate, blockSize);
sourceReady_ = true;
```

If `ACFX_WORKBENCH_FILE` is not set and `numInputChannels()` returns 0 (e.g., on a headless CI runner or a machine with no audio input), neither branch executes and `source_` is left in whatever default-constructed state `WorkbenchAudioSource` has. The code then calls `source_.prepare(sampleRate, blockSize)` without guarding. The comment says "else a surfaced error", implying `prepare()` is supposed to throw `AudioSourceError` in this case.

`audio-source.h` and its implementation are not in this diff, so whether that contract is met cannot be verified here. If `prepare()` silently succeeds on an unconfigured source, `sourceReady_` is set to `true` and `source_.fillBlock(region)` is called during every audio callback — the undefined behaviour of `fillBlock` on an uninitialized source could corrupt the output buffer. The blast radius is silent audio corruption or a crash that is hard to attribute post-hoc.

A defensively correct fix is to add an explicit else branch that throws before `prepare()` is called, making the missing-source case an unconditional error rather than relying on the `source_` object to remember it was never configured:

```cpp
} else {
    throw AudioSourceError("No audio source: set ACFX_WORKBENCH_FILE or enable an audio input.");
}
```

---

### params_ is stored in a member but never read

Finding-ID: AUDIT-BARRAGE-claude-04
Status:     open
Severity:   low
Surface:    adapters/workbench/workbench-app.cpp:40 (member decl) and :44 (assignment)

```cpp
params_ = node_->parameters();
```

`params_` is a `span<const ParameterDescriptor>` member variable that is assigned in the constructor body but never read in any method visible in this diff. It holds a non-owning view into `node_`'s internal descriptor table; the same view is already consumed by `paramView_` (which received it in the constructor's initializer list). The dead member adds noise: it implies external callers or future methods will need it, and it silently aliases `node_`'s memory without a lifetime comment. If `node_` were ever reassigned (e.g., to switch effects), `params_` would dangle while remaining nominally valid-looking.

Either remove `params_` if it genuinely has no remaining use, or document why it must outlive the initializer-list usage.