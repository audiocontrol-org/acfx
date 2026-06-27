I'll audit this diff carefully, working through each file independently.

---

### `modeName()` hardcodes SVF mode strings detached from the ParameterDescriptor

Finding-ID: AUDIT-BARRAGE-claude-01
Status:     open
Severity:   medium
Surface:    adapters/plugin/plugin-parameters.cpp:24–33

`modeName(int index)` returns `"highpass"`, `"bandpass"`, or `"lowpass"` based on hard-coded integer constants. The authoritative source for mode identity and ordering lives in the `SvfEffect` descriptor table in `core/effects/svf/svf-effect.h` (chunk `e7b284327d06692a`), not in this adapter. If a future commit reorders the modes (e.g., to match the Teensy knob's sweep direction), or adds a fourth mode, the plugin will display wrong labels while still sending the correctly-indexed value to the core — a silent, user-visible discrepancy that is hard to notice in automated tests.

The blast-radius: a DAW operator sees "bandpass" while the filter is actually in highpass mode. Parameter automation saved in a session will still recall the correct numeric index, but the display label will lie. The fix is to source the string array from the descriptor itself — either by adding a `names` field to `ParameterDescriptor` (so the table is the single source of truth) or by iterating `d.discreteCount` and calling a central name-resolver on the core side.

---

### `defaultIndex` for discrete params silently truncates float; convention is implicit

Finding-ID: AUDIT-BARRAGE-claude-02
Status:     open
Severity:   medium
Surface:    adapters/plugin/plugin-parameters.cpp:52

```cpp
const int defaultIndex = static_cast<int>(d.defaultValue);
```

For float parameters in the `else` branch (line ~62), `d.defaultValue` is treated as a physical value (Hz, dB, etc.) and passed through `normalize()` before reaching JUCE. For discrete parameters the same field is cast directly to `int` with no `normalize()` call and no bounds check. Two correctness risks follow:

1. **Convention inconsistency.** If a descriptor author sets `defaultValue = 1.0f` meaning "first mode" (treating it like an integer) that works, but `defaultValue = 0.5f` meaning "halfway through the normalized range" gives `static_cast<int>(0.5f) == 0` — always selecting the first mode regardless of intent. The convention is undocumented at the call site.
2. **Bounds.** If `defaultIndex >= d.discreteCount` (e.g., through a descriptor authoring mistake), `juce::AudioParameterChoice` receives an out-of-range default. JUCE's constructor clamps in release builds but asserts in debug, so a test suite built in debug mode would surface this while a release build would silently pick mode 0.

Blast-radius: the plugin's default mode in any DAW session is wrong and not immediately detectable through unit tests that only exercise the core. A one-line fix is `const int defaultIndex = std::clamp(static_cast<int>(d.defaultValue), 0, d.discreteCount - 1)` combined with a comment that states the convention, matching the `normalize()` call for the float branch.

---

### `configured_` thread-ownership comment does not match JUCE's `prepare()` dispatch pattern

Finding-ID: AUDIT-BARRAGE-claude-03
Status:     open
Severity:   low
Surface:    adapters/workbench/audio-source.h:57–62

The comment reads:

```
// Atomic like its siblings: written on the audio/device thread (prepare/
// release) and read on the message thread (the selection-call guard)…
```

In JUCE, `AudioSource::prepareToPlay` and `releaseResources` are called by the audio device manager from the **message thread** before the audio callback starts (they are not audio-ISR calls). If `WorkbenchAudioSource::prepare()` wraps or is called from `prepareToPlay`, then both the write (`configured_ = true`) and the guard read (`useFilePlayer` / `useLiveInput`) happen on the message thread — making `atomic<bool>` correct but the stated reason wrong. A developer reading this comment and reasoning "prepare() is called from the audio ISR so the atomic is load-bearing" would be drawing false conclusions about the threading model. If the comment is correct and `prepare()` is somehow pushed to the device thread, then the guard check in the selection methods needs to be verified as a full fence, not just `relaxed`. Either way the comment needs repair. The implementation in `audio-source.cpp` (absent from this diff — see AUDIT-BARRAGE-claude-06) is where the truth lives.

---

### Teensy right-channel output permanently silent; not documented

Finding-ID: AUDIT-BARRAGE-claude-04
Status:     open
Severity:   low
Surface:    adapters/teensy/teensy-main.cpp:63–64

```cpp
AudioConnection patchIn(audioIn, 0, svfNode, 0);
AudioConnection patchOut(svfNode, 0, audioOut, 0);
```

`audioIn` channel 1 (right) is unconnected to `svfNode`, and `audioOut` channel 1 (right) is never patched. With the standard Teensy Audio Shield (SGTL5000), stereo line-in feeds a stereo codec, but only the left channel is processed and sent to the left output. The right output is silence. The comments in the file mention "single-channel SVF" in passing, but neither the CMakeLists comment nor the function-level comments explain that right-channel audio is dropped entirely at the hardware level. A user connecting a stereo signal source expecting pass-through on the right channel will get only silence there with no diagnostic message.

Blast-radius: limited to user confusion at hardware bring-up. The filter still works correctly on the left channel. A sentence in the CMakeLists comment or a `// Right channel is intentionally unconnected: this is a mono SVF` comment at the connection block resolves it.

---

### `ARDUINO_TEENSY40` hardcoded; Teensy 4.1 users must edit the build file manually

Finding-ID: AUDIT-BARRAGE-claude-05
Status:     open
Severity:   low
Surface:    adapters/teensy/CMakeLists.txt:24

```cmake
target_compile_definitions(teensy_platform PUBLIC ARDUINO=10813 TEENSYDUINO=159 ARDUINO_TEENSY40)
```

The board definition is a hard-coded `ARDUINO_TEENSY40`. Teensy 4.1 (IMXRT1062 with extra RAM and SD card) requires `ARDUINO_TEENSY41`; some Teensy core files guard features behind these macros. There is no CMake cache variable (e.g., `ACFX_TEENSY_BOARD`) to override the board at configure time without editing the source. The `teensy` CMake preset presumably invokes this CMakeLists but provides no board-selection variable either (the preset file is in chunk `6a56babffbf5b038`, not visible here, but the guard at the top of this file checks only `teensy_cores_SOURCE_DIR`, not any board variable).

Blast-radius: a Teensy 4.1 user builds firmware that links against the wrong board defines, potentially missing 4.1-specific PSRAM initialization or SD controller setup, and gets no compile-time warning about the mismatch. Adding `set(ACFX_TEENSY_BOARD "TEENSY40" CACHE STRING "…")` with a fallthrough to the compile-definition is a minimal fix.

---

### `adapters/workbench/audio-source.cpp` is in chunk scope but absent from the diff

Finding-ID: AUDIT-BARRAGE-claude-06
Status:     open
Severity:   informational
Surface:    adapters/workbench/audio-source.cpp (not shown)

The chunk scope listing names `audio-source.cpp` as a file under review, and the corresponding header (`audio-source.h`) describes non-trivial RT-safe contracts: the `configured_` guard that throws on setup-time races, the file-decode pre-buffering, and the `fillBlock()` no-throw/no-alloc/no-lock guarantee. None of these can be verified without the implementation diff. Specifically unaudited: whether `useFilePlayer`/`useLiveInput` actually check `configured_` before proceeding, whether `fillBlock()` for the file-player path does lock-free index arithmetic or silently takes a lock, and whether the `AudioSourceError` is correctly thrown (not swallowed) on decode failure. The header's contracts are load-bearing — `audio-source.h` is the surface a future agent would build against — so the absence of the implementation from this diff is a gap worth flagging to the operator for a follow-up audit pass on `audio-source.cpp` alone.