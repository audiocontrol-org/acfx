I'll analyze each file in this audit chunk carefully for correctness bugs, design issues, edge cases, and code-quality concerns.

---

### `MidiBinding::bind()` / `handle()` race on `std::unordered_map`

Finding-ID: AUDIT-BARRAGE-claude-01
Status:     open
Severity:   high
Surface:    adapters/workbench/midi-binding.h:21-38

`bind()` mutates `bindings_` (called from the UI thread when the user adds a MIDI mapping) while `handle()` reads `bindings_` (called from the JUCE audio/MIDI callback thread). `std::unordered_map` is not thread-safe; concurrent read-on-write is a data race and undefined behavior. The most natural use of this workbench ("set up MIDI binding while audio is running to sketch in real time") is exactly the scenario that triggers the race. Blast radius: if the map is mid-rehash when `handle()` calls `bindings_.find()`, it reads a partially-invalidated hash table — iterator soup, likely a crash or wild-pointer dereference.

A minimal fix is to protect `bindings_` with a `std::mutex` (acceptable since `bind()` is off the audio thread) and require that `handle()` tries a `try_lock` and skips if it can't acquire — or switch to a lock-free SPSC queue that drains at the top of the audio callback, which is the RT-safe pattern used elsewhere in the codebase.

---

### `WorkbenchAudioSource::fileBuffer_` data race on file reload

Finding-ID: AUDIT-BARRAGE-claude-02
Status:     open
Severity:   high
Surface:    adapters/workbench/audio-source.cpp:30, adapters/workbench/audio-source.h:51-57

`useFilePlayer()` terminates with `fileBuffer_ = std::move(decoded)` (audio-source.cpp line 30) followed by `hasFile_.store(true, std::memory_order_release)`. The acquire/release fence guards only `hasFile_`; it says nothing about concurrent access to `fileBuffer_` itself. `juce::AudioBuffer<float>` is not atomically replaceable: the audio thread in `fillBlock()` (line 59–72) reads `fileBuffer_.getNumSamples()` and `fileBuffer_.getReadPointer(ch)` without any fence pairing `fileBuffer_` against the main-thread write.

If the user loads a second file while audio is playing — the natural workbench "A/B compare two clips" workflow — the main thread's `std::move` races with the audio thread's reads. The relaxed load on `hasFile_` in `fillBlock()` (line 57) does not provide the synchronization needed: the flag's release-store is not sequenced-before the buffer destruction in all execution models.

The header comment "The audio thread only ever reads fileBuffer_ thereafter" is a design intent, not a code invariant — there is no `jassert`, no precondition doc, and no guard in `useFilePlayer` against a second call after `prepare()`. Fix: document that `useFilePlayer` must only be called before `prepare()` AND enforce it with an assertion; or use a double-buffer / pointer-swap scheme protected by an atomic pointer.

---

### `modeName()` hard-codes SVF mode labels, decoupled from core descriptor

Finding-ID: AUDIT-BARRAGE-claude-03
Status:     open
Severity:   medium
Surface:    adapters/plugin/plugin-parameters.cpp:24-33

The function `modeName(int index)` maps integer indices 0 → "lowpass", 1 → "highpass", 2 → "bandpass" as naked magic strings. These strings have no mechanical connection to any enum or string table in the SVF core or its `ParameterDescriptor`. If the core reorders modes (e.g., adds a "notch" at index 1, pushing "highpass" to 2) the DAW's parameter display will silently show wrong labels. The `ParameterDescriptor` struct presumably carries metadata about discrete choices — the labels should come from there, not from a parallel integer-switch in the adapter. This is the "configuration ends up as code" smell called out under operator-discipline traps.

---

### Teensy targets: no C++ standard mechanically enforced

Finding-ID: AUDIT-BARRAGE-claude-04
Status:     open
Severity:   medium
Surface:    adapters/teensy/CMakeLists.txt:1-38

The CMake comment at line 7 states "The C++ standard is the highest the installed Teensy toolchain supports (>=17)", but neither `teensy_platform` nor `acfx_teensy` has a `target_compile_features(... cxx_std_17)` or `CMAKE_CXX_STANDARD` setting. If the ARM cross-compiler defaults to C++14, headers that use C++17 features (structured bindings, `if constexpr`, deduction guides) from `core/dsp/` will produce hard compile errors — but the failure mode is "configure-time mystery error pointing at the toolchain" rather than a clear message. More importantly, the C++20 `Effect` concept referenced in the comment "degrades to a duck-typed template" on C++17 — that degradation is described but not verified by the build itself. The stated standard floor is load-bearing; enforce it.

---

### Discrete `discreteCount` not validated in `build()`, patched asymmetrically in `apply()`

Finding-ID: AUDIT-BARRAGE-claude-05
Status:     open
Severity:   medium
Surface:    adapters/plugin/plugin-parameters.cpp:50-54 (build), 80-83 (apply)

In `build()`, a discrete parameter is registered as a `juce::AudioParameterChoice` with `choices` sized to `d.discreteCount` (lines 50-53) without any guard on `discreteCount >= 2`. `AudioParameterChoice` with zero or one option is undefined/degenerate JUCE behavior. In `apply()` (line 81), a defensive clamp forces `count = max(discreteCount, 2)`, preventing a division-by-zero — but this clamp is in the wrong place. A param that was built with 0 choices now produces a normalized value based on a `count` of 2 that has no relation to the JUCE parameter's actual choice count. The root cause (unvalidated `discreteCount` in `build()`) should be fixed; `apply()` should be able to trust the invariant instead of paper-covering it.

---

### `configured_` field set but never read — dead code

Finding-ID: AUDIT-BARRAGE-claude-06
Status:     open
Severity:   low
Surface:    adapters/workbench/audio-source.cpp:48, adapters/workbench/audio-source.h:57

`configured_` is assigned `true` in `prepare()` and `false` in `release()` but is never read in any method: not in `fillBlock()`, not in `useFilePlayer()`, not in `useLiveInput()`. The field is vestigial. If the intent was to guard `fillBlock()` against running before `prepare()`, that guard is absent; if the intent was to assert in `useFilePlayer()` that files must be configured before `prepare()`, that assertion is absent. The dead field should either be activated (turn it into the enforcement mechanism for the "call before prepare" precondition) or deleted.

---

### `lengthInSamples` narrowing cast silently truncates long files

Finding-ID: AUDIT-BARRAGE-claude-07
Status:     open
Severity:   low
Surface:    adapters/workbench/audio-source.cpp:17

`reader->lengthInSamples` is `int64_t`; it is cast to `int` at line 17 with no range check. At 48 kHz, files longer than ~12.4 hours (~2 GB uncompressed WAV) wrap to a negative value. The following guard `if (numChannels <= 0 || numSamples <= 0)` would catch the negative case and throw, which is acceptable behavior — but the error message ("Audio file is empty") is misleading. A pre-cast range check with a descriptive error ("File too long to load into memory: …") would diagnose the real condition.

---

### `std::function` construction on the audio thread in `processBlock`

Finding-ID: AUDIT-BARRAGE-claude-08
Status:     open
Severity:   informational
Surface:    adapters/plugin/plugin-processor.cpp:33-35

`parameters_.apply(...)` accepts `const ApplyFn&` where `ApplyFn = std::function<void(ParamId, float)>`. In `processBlock`, a lambda `[this](ParamId id, float normalized){ ... }` is passed inline, causing implicit construction of a `std::function` object on the audio thread. For a `[this]`-only capture (one pointer), all major STL implementations (libstdc++, libc++) fit this into SBO without heap allocation — so in practice this is benign. However, the pattern is fragile: if any future change to the lambda adds a larger capture (e.g., a `std::string`, a counter, a second pointer), it silently graduates to a heap allocation in a hot RT path. The `apply()` API accepting a plain function pointer or a templated callable would eliminate the risk entirely.