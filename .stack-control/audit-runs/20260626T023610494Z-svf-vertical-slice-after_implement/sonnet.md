### Memory ordering in `fillBlock()` leaves `fileBuffer_` unprotected on non-TSO architectures

Finding-ID: AUDIT-BARRAGE-claude-01
Status:     open
Severity:   high
Surface:    adapters/workbench/audio-source.cpp:62–80 + adapters/workbench/audio-source.h:52–55

`useFilePlayer()` publishes `fileBuffer_` via a release-store to `hasFile_`:

```cpp
fileBuffer_ = std::move(decoded);          // non-atomic write
playPos_.store(0, std::memory_order_relaxed);
hasFile_.store(true, std::memory_order_release);   // release
```

For the release to guard `fileBuffer_`, the consuming thread must pair it with an acquire-load. `fillBlock()` does not:

```cpp
if (!hasFile_.load(std::memory_order_relaxed) || fileLen <= 0)   // relaxed — no sync
```

`memory_order_relaxed` establishes no synchronize-with relationship with the release-store, so the C++ memory model gives no happens-before guarantee for the `fileBuffer_` writes. On x86 (TSO) this is invisible because of the strong memory model; on ARM (the very platform this codebase targets for the Daisy and Teensy adapters), compiler reordering and the weak memory model make this a genuine data race on `fileBuffer_`. The same analysis applies to `live_.load(memory_order_relaxed)` in the same function (line ~62). Fix: change both loads in `fillBlock()` to `memory_order_acquire`. The comment in `audio-source.h:17–19` correctly describes the intended protocol ("file decoded into memory before the stream starts; fillBlock reads from that point on") but the implementation does not enforce the memory model invariant that makes it work.

---

### `modeName()` hardcodes SVF internals in the plugin adapter with a silent wrong-label fallback

Finding-ID: AUDIT-BARRAGE-claude-02
Status:     open
Severity:   medium
Surface:    adapters/plugin/plugin-parameters.cpp:23–33

```cpp
juce::String modeName(int index) {
    switch (index) {
    case 1: return "highpass";
    case 2: return "bandpass";
    case 0:
    default: return "lowpass";
    }
}
```

This function bakes SVF-specific domain knowledge (mode count = 3, name set = lowpass/highpass/bandpass) into the generic plugin adapter layer. The `default:` arm silently maps any index ≥ 3 to `"lowpass"`, so if `discreteCount` in the descriptor is ever extended (a 4th mode, a notch, etc.), the plugin will display the wrong label without any compile-time or runtime signal. The adapter's stated design goal is to derive everything from the descriptor table so that no hand-written parameter lists are needed; `modeName()` is exactly the hand-written parameter list that goal was meant to eliminate. The mode labels should come from a `names` array in the descriptor, or the descriptor should at minimum carry a `const char* const*` for discrete option names. The blast-radius is wrong DAW automation lane labels silently persisting in saved sessions, invisible to users.

---

### Silent `discreteCount < 2` fallback in `apply()` violates the project's no-fallback rule

Finding-ID: AUDIT-BARRAGE-claude-03
Status:     open
Severity:   medium
Surface:    adapters/plugin/plugin-parameters.h:39–41

```cpp
const int count = e.descriptor.discreteCount < 2 ? 2 : e.descriptor.discreteCount;
const float norm = (static_cast<float>(index) + 0.5f) / static_cast<float>(count);
```

When `discreteCount` is 0, using it as a divisor would be division by zero, so a guard is necessary. But clamping to 2 silently replaces an invalid descriptor with a plausible-looking value — precisely the silent-fallback pattern the project's constitution forbids ("Throw errors with a description of the missing functionality instead. Fallbacks are bug factories"). A `discreteCount` of 0 or 1 indicates a malformed `ParameterDescriptor`; the correct response is an assertion or an exception, not a magic minimum that lets incorrect descriptors produce incorrect normalized values without any diagnostic. Additionally, there is no comment explaining why 2 is the floor, so readers cannot distinguish "intentional guard" from "forgotten validation."

---

### Cross-adapter discrete-parameter encoding is inconsistent between plugin and Teensy

Finding-ID: AUDIT-BARRAGE-claude-04
Status:     open
Severity:   medium
Surface:    adapters/plugin/plugin-parameters.h:39–41 + adapters/teensy/teensy-main.cpp:79–81

The plugin encodes the mode discrete parameter as a midpoint-normalized value:

```cpp
// plugin-parameters.h:40
const float norm = (static_cast<float>(index) + 0.5f) / static_cast<float>(count);
// → sends 0.167, 0.500, 0.833 for indices 0/1/2 of a 3-choice param
```

The Teensy sends the raw analog reading directly:

```cpp
// teensy-main.cpp:79
const float mode = static_cast<float>(analogRead(kModePin)) / 1023.0f;
// → sends any value in [0.0, 1.0] continuously
```

These are structurally different: the plugin sends three discrete midpoints; the Teensy sweeps a continuous range. Whether `SvfEffect::setParameter` for the mode parameter handles both correctly (i.e., uses `floor(norm * count)` quantization that maps the plugin's midpoints and the Teensy's full sweep to the same discrete indices) is unverifiable from this diff — the effect's `setParameter` implementation is in a chunk that was not included. The comment at `plugin-parameters.h:14` explicitly claims "the mapping is identical across adapters (SC-006/SC-007)" but this claim cannot be confirmed from the adapter code alone. If the effect uses the midpoint formula to reconstruct the index (i.e., `round(norm * count - 0.5)`), the Teensy's continuous input would produce correct quantization at exactly the same indices, but values near mode boundaries (≈0.333 and ≈0.667) would be ambiguous and potentially noisy. A test fixture or a cross-reference to the effect's denormalization logic is needed to close this gap.

---

### Teensy CMakeLists `GLOB` patterns miss assembly translation units in the core

Finding-ID: AUDIT-BARRAGE-claude-05
Status:     open
Severity:   low
Surface:    adapters/teensy/CMakeLists.txt:22–23

```cmake
file(GLOB _teensy_core_sources CONFIGURE_DEPENDS "${_teensy4}/*.c" "${_teensy4}/*.cpp")
file(GLOB _teensy_audio_sources CONFIGURE_DEPENDS "${teensy_audio_SOURCE_DIR}/*.cpp")
```

The Teensy 4.x (iMXRT1062) toolchain build occasionally includes `.S` or `.s` assembly files in the core directory (startup sequences, interrupt vectors for certain Teensyduino versions). These patterns would miss them silently — CMake would configure without error, and the build would fail at link time with undefined symbol errors. The fix is to add `"${_teensy4}/*.S"` and `"${_teensy4}/*.s"` to the first glob, and to enable ASM in the CMake project so the compiler driver is invoked correctly for those translation units. Because this is conditionally an issue (only if the fetched core revision includes assembly files), severity is low, but the failure mode when triggered would be an opaque linker error rather than a useful diagnostic.