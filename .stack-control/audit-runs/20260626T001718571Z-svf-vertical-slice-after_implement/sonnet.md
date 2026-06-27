I'll audit the provided diff carefully, working through each file for correctness bugs, real-time safety issues, design problems, and edge cases.

---

### Concurrent parameter writes from `loop()` race against audio ISR in Teensy adapter

Finding-ID: AUDIT-BARRAGE-claude-01
Status:     open
Severity:   high
Surface:    adapters/teensy/teensy-main.cpp:82–87 (loop body) and :34–39 (AcfxSvfNode::update)

`loop()` calls `svf.setParameter(...)` from the Arduino main-loop context while `AcfxSvfNode::update()` calls `svf.process(audioBlock)` from the Teensy Audio library's interrupt-driven audio ISR. These run concurrently with no synchronization. If `SvfEffect` stores parameters as plain `float` fields (the most natural implementation), reads in `process()` and writes in `setParameter()` form a data race — undefined behavior in C++, and practically observable as filter coefficient tearing mid-block on IMXRT1062 which lacks hardware memory ordering for non-atomic stores.

The diff shows no `std::atomic<float>` or spinlock in `teensy-main.cpp` itself, and the effect's parameter storage is in `core/` (not in this chunk), so it cannot be verified here. The fix must be one of: (a) mark parameter fields `std::atomic<float>` with relaxed or acquire/release semantics; (b) use `noInterrupts()`/`interrupts()` guards around writes in `loop()`; or (c) use a lock-free parameter ring. Until one of these is confirmed, this is a latent UB that will only manifest as an occasional audio artifact under load — the hardest class of embedded bugs to reproduce.

---

### `transport_.start()` called from the audio callback — heap allocation + async post in RT context

Finding-ID: AUDIT-BARRAGE-claude-02
Status:     open
Severity:   high
Surface:    adapters/workbench/audio-source.cpp:51–54

`fillBlock()` is the method the `AudioIODeviceCallback` calls from the audio thread. Inside it, line 52–53:

```cpp
if (!transport_.isPlaying())
    transport_.start();
```

JUCE's `AudioTransportSource::start()` calls `sendChangeMessage()`, which allocates a heap `Message` object and posts it to the `MessageManager` queue. Heap allocation from the audio thread violates real-time safety (can block on the OS allocator) and is explicitly prohibited by the project's "no heap allocation in audio-callback path" standard (CLAUDE.md / constitution). On macOS CoreAudio with a 5ms buffer this is likely to cause buffer underruns under memory pressure; on a hardened OS config it can deadlock if the lock protecting the message queue is held by the message thread during the call.

The correct fix is to start the transport once, eagerly, from `prepare()` (or a separate `start()` method called from the message thread before audio begins), and never call `transport_.start()` from inside `fillBlock`. The lazy-start pattern is understandable but makes an unsafe assumption about which thread reaches that branch first.

---

### `MidiBinding::bind()` and `handle()` share `unordered_map` with no synchronization

Finding-ID: AUDIT-BARRAGE-claude-03
Status:     open
Severity:   medium
Surface:    adapters/workbench/midi-binding.h:22–23 (bind), :26–37 (handle)

`bind()` writes to `bindings_` while `handle()` reads it. In the workbench, `bind()` is called from the UI/message thread (user interaction or MIDI-learn) and `handle()` is called from the JUCE audio/MIDI callback thread. `std::unordered_map` provides no thread-safety guarantees for concurrent read/write; a concurrent rehash during `find()` is undefined behavior.

The fix is either: (a) protect `bindings_` with a `std::mutex` (acceptable since `bind()` is not in the audio path); or (b) use an `std::array<std::optional<ParamId>, 128>` indexed by CC number, where each element is individually `std::atomic` — this avoids any lock in `handle()` and keeps the audio thread fully wait-free.

---

### Discrete combo items labeled with raw numeric indices, not parameter option names

Finding-ID: AUDIT-BARRAGE-claude-04
Status:     open
Severity:   medium
Surface:    adapters/workbench/parameter-view.cpp:17–18

```cpp
for (int i = 0; i < d.discreteCount; ++i)
    row.combo->addItem(juce::String(i), i + 1);  // items labeled "0", "1", "2", …
```

For SVF filter mode, the workbench combo will display "0", "1", "2" rather than "Lowpass", "Highpass", "Bandpass". This makes the workbench — whose purpose is to "sketch and hear" the effect — opaque to anyone using it. The `ParameterDescriptor` struct does not appear to carry per-option label strings in this diff; if that's the case it is a design gap at the descriptor level that surfaces visibly here. The fix is to add an `optionNames` span (or similar) to `ParameterDescriptor` and use it in `addItem()`. If the descriptor intentionally omits option names, the combo should at minimum derive labels from the parameter name + index (e.g., "Mode 0") rather than a bare integer.

---

### `kMaxChannels = 8` mismatches the declared mono/stereo bus constraint

Finding-ID: AUDIT-BARRAGE-claude-05
Status:     open
Severity:   low
Surface:    adapters/plugin/plugin-processor.h:48, adapters/plugin/plugin-processor.cpp:39–43

`isBusesLayoutSupported` (plugin-processor.cpp:21–26) rejects any layout that is not mono or stereo, so the effective channel ceiling is 2. Yet `kMaxChannels = 8` drives a stack-allocated `std::array<float*, 8>` in every `processBlock` call, with indices 2–7 left null and unreachable. The constant is harmless in isolation but misleading — a future maintainer widening the bus support might assume `kMaxChannels` already covers their new layout, skip updating `isBusesLayoutSupported`, and ship a processor that accepts 8-channel buses it was never verified to handle. Rename to `kMaxSupportedChannels = 2` or derive it from the bus definition.

---

### Int16↔float round-trip loses precision: mismatched divisor/multiplier constants

Finding-ID: AUDIT-BARRAGE-claude-06
Status:     open
Severity:   low
Surface:    adapters/teensy/teensy-main.cpp:20–21, :42–49

```cpp
constexpr float kInt16ToFloat  = 1.0f / 32768.0f;   // divides by 32768
constexpr float kFloatToInt16  = 32767.0f;           // multiplies by 32767
```

A full-scale positive input (int16 = 32767) converts to `32767 / 32768 ≈ 0.999969`, then back to `0.999969 × 32767 ≈ 32766.0`, truncated to `32766` — off by 1 LSB. Full-scale negative (int16 = −32768) converts to exactly −1.0, then back to −32767.0, giving −32767 — also off by 1 LSB in the opposite direction. The two constants should use the same scale; the conventional symmetric choice is both 32768.0f (accepting that +32767 never quite reaches +1.0 but −32768 maps exactly to −1.0).

Additionally, `static_cast<int16_t>` truncates toward zero rather than rounding to nearest, introducing a ±0.5 LSB DC bias in dense signal passages. For a demo firmware the audible impact is negligible, but the asymmetry between the two constants is a latent bug if this code is used as a template for other adapters.

---

### `AudioMemory(12)` is an undocumented magic number without headroom reasoning

Finding-ID: AUDIT-BARRAGE-claude-07
Status:     open
Severity:   informational
Surface:    adapters/teensy/teensy-main.cpp:72

`AudioMemory(12)` sets the global pool for the Teensy Audio Library to 12 blocks. The value is the right order of magnitude for a two-connection mono graph (line-in → SVF node → line-out), but there is no comment explaining the allocation budget or headroom margin. The Teensy Audio library's design guide recommends accounting for all nodes plus at least 2–3 blocks of buffer headroom; if the graph is extended (e.g., adding a reverb node, a mixer, or stereo channels), the current value may cause silent drop-outs rather than a compile-time or runtime error. A comment citing the block budget would prevent silent regression when the graph is extended.