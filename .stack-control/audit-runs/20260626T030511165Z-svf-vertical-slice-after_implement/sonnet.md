I'll read this diff carefully and audit for the issue categories listed.

---

### Plugin discrete-parameter normalization diverges from Teensy's linear mapping, contradicting the SC-006 "identical across adapters" claim

Finding-ID: AUDIT-BARRAGE-claude-01
Status:     open
Severity:   medium
Surface:    adapters/plugin/plugin-parameters.h:37-44, adapters/teensy/teensy-main.cpp:82-84

`PluginParameters::apply()` encodes discrete indices using a midpoint scheme:

```cpp
const float norm = (static_cast<float>(index) + 0.5f) / static_cast<float>(count);
```

For the SVF's 3-mode parameter this yields exactly 0.167, 0.500, 0.833. The Teensy adapter reads the analog potentiometer and maps it raw:

```cpp
const float mode = static_cast<float>(analogRead(kModePin)) / 1023.0f;
```

That produces a continuous 0..1 value anywhere in the relevant third of the knob's travel, not a discrete midpoint. The plugin header's comment at line 17 asserts "the normalized value handed to setParameter is the same one the workbench produces, so the mapping is identical across adapters (SC-006)." The two mechanisms are not identical: one produces a fixed midpoint per bin; the other produces a continuous ADC value. As long as SvfEffect uses ⅓/⅔ thresholds the end result is the same mode, but the claim of identical mapping is false and is load-bearing documentation — a future maintainer who relies on it when implementing a new adapter (or when the threshold changes in SvfEffect) will build the wrong thing. The workbench's parameter-view.cpp is not in this diff, so whether the workbench matches the plugin's midpoint scheme is also unverifiable from the audited surface.

Reasonable fix: remove or narrow the "identical across adapters" language to "identical to the workbench adapter," and add a comment in the Teensy adapter noting that hardware knobs map the continuous ADC range to the same mode bins via SvfEffect's internal threshold, which is a different but compatible mechanism.

---

### `modeName()` silently applies SVF filter-mode labels to any discrete parameter

Finding-ID: AUDIT-BARRAGE-claude-02
Status:     open
Severity:   medium
Surface:    adapters/plugin/plugin-parameters.cpp:24-34, adapters/plugin/plugin-parameters.cpp:51-54

The private helper `modeName(int index)` returns `"lowpass"`, `"highpass"`, `"bandpass"` for indices 0–2 and the default case silently returns `"lowpass"` for any index ≥ 3. It is called unconditionally inside `build()` for every `ParamKind::discrete` descriptor:

```cpp
for (int i = 0; i < d.discreteCount; ++i)
    choices.add(modeName(i));
```

The code currently has only one discrete parameter (SVF mode), so this is not a bug today. But `build()` iterates over an arbitrary `span<const ParameterDescriptor>`, so if any future effect introduces a second discrete parameter (an LFO waveform, an envelope type, a routing choice), that parameter's JUCE choices will silently display `"lowpass"`, `"highpass"`, `"bandpass"`. The label source is coupled to the effect identity but does not check it, violating the stated design goal that "there is no hand-written parameter list" and parameters are generated from the descriptor table. The fix is to either embed choice labels directly in `ParameterDescriptor` or assert/throw that the only known discrete parameter is the SVF mode.

---

### `AudioParameterChoice` created with no validation of `discreteCount` or `defaultValue` range

Finding-ID: AUDIT-BARRAGE-claude-03
Status:     open
Severity:   medium
Surface:    adapters/plugin/plugin-parameters.cpp:51-56

```cpp
for (int i = 0; i < d.discreteCount; ++i)
    choices.add(modeName(i));
const int defaultIndex = static_cast<int>(d.defaultValue);
auto param = std::make_unique<juce::AudioParameterChoice>(paramId, name, choices, defaultIndex);
```

Two missing guards:

1. If `d.discreteCount == 0` (or negative), `choices` is empty. Passing an empty `StringArray` to `juce::AudioParameterChoice` triggers a JUCE debug assertion and is undefined behaviour in release builds.
2. `static_cast<int>(d.defaultValue)` is undefined behaviour if `d.defaultValue` is `NaN`, `±infinity`, or any value outside `[INT_MIN, INT_MAX]`. Even for normal floats, if the integer result falls outside `[0, discreteCount)`, JUCE will assert.

The continuous path below calls `normalize(d, d.defaultValue)` via a dedicated helper, so presumably there is already a normalize/clamp layer for floats. The discrete path has no equivalent guard. A pre-condition check (`d.discreteCount >= 1` and `defaultIndex` in range) or a static_assert on the descriptor struct's invariants would close this.

---

### `kMaxChannels = 8` is inconsistent with the enforced mono/stereo layout

Finding-ID: AUDIT-BARRAGE-claude-04
Status:     open
Severity:   low
Surface:    adapters/plugin/plugin-processor.h:46, adapters/plugin/plugin-processor.cpp:36-41

`isBusesLayoutSupported` (plugin-processor.cpp:23-27) rejects every layout that is not mono or stereo. The maximum possible channel count on entry to `processBlock` is therefore 2. Yet `kMaxChannels = 8` is used as the array bound:

```cpp
std::array<float*, kMaxChannels> chans{};
for (int ch = 0; ch < numChannels; ++ch)
    chans[ch] = buffer.getWritePointer(ch);
```

The six unused null pointers are harmless at runtime, but the constant advertises capability the bus layout explicitly denies, and the `jmin` guard against `kMaxChannels` is dead code. A future maintainer widening bus support to more than 2 channels would change `isBusesLayoutSupported` and would see the `kMaxChannels` constant and the `jmin` and assume 8-channel is already wired in — it isn't, because `EffectNode<SvfEffect>` and `SvfEffect` itself have not been audited for more than stereo. The constant should either be `2` (matching the enforced layout) or carry a comment explaining why headroom beyond the bus constraint exists.

---

### Misleading error message in `useFilePlayer` / `useLiveInput` when `configured_` is set

Finding-ID: AUDIT-BARRAGE-claude-05
Status:     open
Severity:   low
Surface:    adapters/workbench/audio-source.cpp:13-16, adapters/workbench/audio-source.cpp:29-31

Both `useFilePlayer()` and `useLiveInput()` throw with the message:

```
"Audio source must be selected before the stream starts; stop audio to switch sources."
```

The first clause ("must be selected before the stream starts") describes the correct usage — it reads as precondition advice to a caller who has not yet selected a source. But the guard condition is `configured_ == true`, meaning the stream **has already started**. The message is therefore inverted: it fires precisely when the source *has* been selected and the stream is running, which is the one scenario the message does not clearly describe. A developer debugging a throw will read the first clause and wonder why they are seeing it after already selecting a source. A cleaner message: "Cannot change audio source while the stream is active; call release() first."

---

### Unexplained magic numbers in Teensy setup

Finding-ID: AUDIT-BARRAGE-claude-06
Status:     open
Severity:   low
Surface:    adapters/teensy/teensy-main.cpp:71-72

```cpp
AudioMemory(12);
codec.volume(0.6f);
```

`12` is the number of audio-memory blocks allocated to the Teensy Audio Library pool. Without a comment, a maintainer expanding the audio graph (adding a reverb node, mixing a second source, adding a delay line) has no basis for knowing whether 12 is tight or generous. Starvation from an underfunded pool is silent — blocks return null, audio glitches, and there is no error path. A comment deriving the minimum (`3 blocks × 2 connections = 6, +6 headroom`) takes one line and prevents silent regressions.

`0.6f` for the codec output volume is similarly unexplained. This is a hardware-tuned value that will vary by headphone/speaker impedance; without a note, it will be treated as arbitrary and "corrected" by a future developer who doesn't know why it's 0.6.