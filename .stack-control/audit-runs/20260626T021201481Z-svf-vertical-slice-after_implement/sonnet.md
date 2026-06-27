I'll audit the diff carefully across the files in this chunk.
### `MidiBinding::bindings_` data race between UI thread and audio thread

Finding-ID: AUDIT-BARRAGE-claude-01
Status:     open
Severity:   high
Surface:    adapters/workbench/midi-binding.h:38-40 (bind), 27-37 (handle)

`MidiBinding::bindings_` is a `std::unordered_map<int, ParamId>`. `bind()` writes to it (and can trigger rehashing and internal allocation); `handle()` reads from it. In a JUCE workbench, `handle()` is called from the audio callback thread, while `bind()` is called from the message/UI thread when the user sets up CC mappings. There is no lock, no atomic, and no documented rule restricting `bind()` to before the stream starts. Concurrent read + write on `std::unordered_map` is undefined behaviour in C++, regardless of whether the write is a rehash or a simple `operator[]` insert.

Even if in practice bindings are configured before `prepare()`, the class contract does not enforce this. A future caller who calls `bind()` during playback (e.g., from a UI control that re-maps CCs at runtime) will trigger a silent data race. The fix is either (a) require `bind()` only before `prepare()` and document/enforce that precondition, or (b) guard access with a `std::mutex` in `bind()`/`handle()`, or (c) use a lock-free snapshot scheme (copy the map on write, swap atomically) consistent with the RT-safety policy elsewhere.

---

### `getStateInformation`/`setStateInformation` silent no-ops violate the project's error-surfacing policy

Finding-ID: AUDIT-BARRAGE-claude-02
Status:     open
Severity:   medium
Surface:    adapters/plugin/plugin-processor.h:47-48, adapters/plugin/plugin-processor.cpp (entire file)

The comment at line 47 of plugin-processor.h reads: "Preset/state persistence is out of scope for this milestone — intentionally no-op, not a silent fallback." But the body is `{}` — it *is* a silent fallback. When a host saves state (e.g., stores a DAW session), it calls `getStateInformation`; the `MemoryBlock&` is left untouched and no diagnostic is emitted. When the host restores state, `setStateInformation` silently discards the data. The user's session appears to save and load successfully while actually round-tripping nothing.

The project constitution (CLAUDE.md, global instructions) is explicit: "Never implement fallbacks or use mock data outside of test code. Throw errors with a description of the missing functionality instead. Errors let us know that something isn't implemented." A descriptive `jassertfalse` (audio plugin equivalent) or a logged `DBG("state persistence not implemented")` / thrown exception would satisfy this policy and make the omission visible to integrators rather than burying it as a silent noop.

---

### Teensy CMakeLists.txt produces `.elf` with no post-build `.hex` conversion — firmware cannot be flashed

Finding-ID: AUDIT-BARRAGE-claude-03
Status:     open
Severity:   medium
Surface:    adapters/teensy/CMakeLists.txt:35-37

```cmake
set_target_properties(acfx_teensy PROPERTIES SUFFIX ".elf")
```

Teensy Loader (and the Arduino IDE upload path) requires a `.hex` file; it cannot flash an `.elf` directly. The CMakeLists.txt produces only the `.elf` with no `add_custom_command` / `objcopy` post-build step to generate the `.hex`. A developer building the Teensy target gets an output they cannot flash without manually running:

```
arm-none-eabi-objcopy -O ihex acfx_teensy.elf acfx_teensy.hex
```

This is not documented anywhere in the diff. The Daisy adapter (in the other chunk) may have a similar or different convention. The fix is a CMake post-build command:

```cmake
add_custom_command(TARGET acfx_teensy POST_BUILD
    COMMAND ${CMAKE_OBJCOPY} -O ihex $<TARGET_FILE:acfx_teensy> acfx_teensy.hex)
```

---

### Discrete parameter normalization uses midpoint scheme — SC-006 "identical across adapters" claim is false

Finding-ID: AUDIT-BARRAGE-claude-04
Status:     open
Severity:   medium
Surface:    adapters/plugin/plugin-parameters.cpp:76-84, adapters/plugin/plugin-parameters.h:16-17

The plugin's `apply()` converts a discrete choice to a normalized float using midpoint bucketing:

```cpp
const float norm = (static_cast<float>(index) + 0.5f) / static_cast<float>(count);
```

For 3 modes this yields {1/6 ≈ 0.167, 0.5, 5/6 ≈ 0.833}.

The workbench's MIDI binding (`midi-binding.h:32`) converts CC values linearly:

```cpp
const float normalized = static_cast<float>(msg.getControllerValue()) / 127.0f;
```

A CC value of 0 → 0.0, 64 → ~0.504, 127 → 1.0. These are not the same values. The header comment at plugin-parameters.h line 16–17 states: "The normalized value handed to setParameter is the same one the workbench produces, so the mapping is identical across adapters (SC-006)." This claim is false for the discrete mode parameter.

Whether this produces the correct *final* mode selection depends on the thresholds inside `SvfEffect::setParameter` for the mode param — if the thresholds are bucketed (0..1/3, 1/3..2/3, 2/3..1), both schemes land in the right bucket for the common cases. But the claim of SC-006 consistency is misleading, the divergence is undocumented, and an extreme CC value (e.g., 127 → 1.0) landing exactly on a threshold boundary could select differently. The comment should be corrected or the mapping genuinely unified.

---

### Mode names hardcoded in `modeName()`, not derived from effect descriptor — contradicts "no hand-written parameter list" claim

Finding-ID: AUDIT-BARRAGE-claude-05
Status:     open
Severity:   low
Surface:    adapters/plugin/plugin-parameters.cpp:23-31, adapters/plugin/plugin-parameters.h:14-15

The header states "There is no hand-written parameter list: each `ParameterDescriptor` becomes a JUCE parameter." However, the `modeName()` helper function at cpp lines 23–31 hand-codes `{"lowpass", "highpass", "bandpass"}` for indices {0, 1, 2}. These strings are not sourced from the `ParameterDescriptor` or from `SvfEffect`'s constants. If the effect descriptor gains a `modeNames` field (or changes the index ordering for modes), `modeName()` will silently produce wrong UI labels in the host. The fix is to either provide mode-name strings in `ParameterDescriptor` (so all adapters share them) or derive them from the same enum constants `SvfEffect` exposes.

---

### `kMaxChannels = 8` is dead capacity inconsistent with `isBusesLayoutSupported` limit of 2

Finding-ID: AUDIT-BARRAGE-claude-06
Status:     open
Severity:   low
Surface:    adapters/plugin/plugin-processor.h:49, adapters/plugin/plugin-processor.cpp:20-24, 31-33

`isBusesLayoutSupported` (cpp lines 20–24) rejects any layout other than mono (1) or stereo (2). `processBlock` then allocates `std::array<float*, kMaxChannels> chans{}` with `kMaxChannels = 8`, iterates up to `numChannels = jmin(buffer.getNumChannels(), 8)`, and the bus layout guarantee means `numChannels` is always ≤ 2. The 6 trailing nullptr-slots are zero-initialized and never touched, but `kMaxChannels = 8` misleads a reader into thinking the processor handles up to 8 channels. The constant should be 2 (or `kMaxBusChannels = 2`) to match the bus constraint, or the bus constraint should be widened and tested.