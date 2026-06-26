I'll audit chunk `d58ba5050d21850a` — the plugin, teensy, and workbench adapters. Walking each file for correctness, RT-safety, threading, and abstraction-leak defects.

### Runtime audio-source switch frees the reader before rebinding the transport — message-thread/audio-thread use-after-free

Finding-ID: AUDIT-BARRAGE-claude-01
Status:     open
Severity:   high
Surface:    adapters/workbench/audio-source.cpp:8-26 (useFilePlayer), :32-38 (useLiveInput)

`useFilePlayer` is documented as a "runtime source switch from the message thread" (line 18-20) while `fillBlock` runs concurrently on the audio thread calling `transport_.getNextAudioBlock`. The switch is unsynchronized against the audio thread, and the statement ordering is itself unsafe: `readerSource_ = std::make_unique<...>(reader, true)` (line 14) **destroys the old `AudioFormatReaderSource` before** `transport_.setSource(readerSource_.get())` (line 16) re-points the transport. Between those two lines the transport's internal source pointer still references the just-freed object. JUCE's `AudioTransportSource` guards `setSource`/`getNextAudioBlock` with an internal `CriticalSection`, but the `unique_ptr` reassignment that frees the old source happens **outside** that lock — so an audio callback firing in that window dereferences freed memory (use-after-free). `useLiveInput` has the same shape: `readerSource_.reset()` (line 35) frees the source the audio thread may still be pulling from. Additionally, the lock `getNextAudioBlock` takes on the audio thread is itself an RT-safety violation (priority inversion under the no-locks-in-audio-callback rule).

The blast radius: a downstream operator who switches source mid-playback (the documented use case) gets intermittent crashes/corruption that are nearly impossible to reproduce deterministically. A reasonable fix sequences the swap so the audio thread never sees a freed source — clear the transport source under its lock first (`transport_.setSource(nullptr)`), then destroy the old reader, then install and bind the new one — or, better, post the swap to the audio thread via a lock-free single-slot handoff and let the audio thread perform the pointer exchange.

### Teensy parameter writes from `loop()` race the audio-ISR `update()` with no synchronization

Finding-ID: AUDIT-BARRAGE-claude-02
Status:     open
Severity:   medium
Surface:    adapters/teensy/teensy-main.cpp:81-91 (loop), :21-46 (AcfxSvfNode::update)

`loop()` calls `svf.setParameter(...)` three times per iteration from thread context, while `AcfxSvfNode::update()` calls `svf.process()` from the Teensy Audio Library's software-interrupt context. On the Cortex-M7 the audio update preempts `loop()` (not vice-versa), so a `setParameter` call interrupted mid-write hands `process()` a partially-updated parameter/coefficient set — a classic torn read. Whether this is benign depends on `SvfEffect::setParameter`'s internals (not in this chunk): if it stores a single normalized `float` per id (atomic on M7) and recomputes coefficients inside `process()`, the race is harmless; if it computes and stores multiple coefficients, the coefficient block can be torn across the interrupt, producing a transient wrong filter state or instability.

This matters because commit `bd79479` ("Address govern findings: RT-safety, thread ownership") asserts the thread-ownership question was handled, but the MCU adapter's cross-context parameter handoff is exactly the surface that commit's title claims to cover, and nothing here documents the ownership invariant or guarantees atomic handoff. The fix is to state and enforce the invariant: either `setParameter` writes exactly one atomically-storable value per id (and the comment should say so), or the firmware must double-buffer the coefficient set and flip a single atomic pointer/flag the ISR reads. A one-line "single-float-store, recompute-in-process" comment anchoring the invariant would also resolve the audit ambiguity.

### `PluginParameters` hardcodes SVF filter-mode names into a class that advertises itself as descriptor-generic

Finding-ID: AUDIT-BARRAGE-claude-03
Status:     open
Severity:   medium
Surface:    adapters/plugin/plugin-parameters.cpp:26-35 (modeName), :47-50 (discrete build); adapters/plugin/plugin-parameters.h:11-17

The header states there is "no hand-written parameter list: each ParameterDescriptor becomes a JUCE parameter" — i.e. the class is sold as a generic, data-driven builder shared across effects (the SC-006 "identical mapping across adapters" claim rests on this). But `modeName(int)` hardcodes `"lowpass"/"highpass"/"bandpass"` and the discrete build loop (`choices.add(modeName(i))`, line 49) sources every discrete parameter's labels from it. This is a leaky abstraction: the generic builder embeds SVF-specific semantics. Two concrete failure modes: (1) any second effect with a discrete parameter gets SVF filter-mode labels regardless of meaning; (2) within SVF, if `discreteCount` ever differs from 3, indices ≥ 3 silently fall through to `default: "lowpass"` (line 32-34), yielding duplicate labels with no error.

The descriptor table is where choice labels belong — configuration that is currently code. A reasonable fix adds choice labels to `ParameterDescriptor` (the data that already owns range/skew/unit) and has `build` read them, deleting `modeName` entirely. Until then this is coupling between the plugin adapter and SVF semantics that the architecture explicitly tries to keep apart (platform-independent core, thin generic adapters).

### Silent `discreteCount < 2 → 2` fallback (and unvalidated empty choice list) mask malformed descriptors

Finding-ID: AUDIT-BARRAGE-claude-04
Status:     open
Severity:   medium
Surface:    adapters/plugin/plugin-parameters.cpp:88 (apply), :47-49 (build)

`apply()` computes `const int count = e.descriptor.discreteCount < 2 ? 2 : e.descriptor.discreteCount;` — silently substituting `2` when a discrete descriptor reports `discreteCount < 2`. Per the project's "no fallbacks — throw a descriptive error" rule, a discrete parameter with `discreteCount` of 0 or 1 is a malformed descriptor, and substituting `2` hides it: the normalized value handed to `setParameter` is then computed against a denominator the effect never agreed to, silently desyncing the plugin's mapping from the effect's own denormalize (breaking the very SC-006 cross-adapter-identity invariant the class claims). Correspondingly in `build`, the discrete loop `for (int i = 0; i < d.discreteCount; ++i)` produces an **empty** `juce::StringArray` when `discreteCount == 0`, and `juce::AudioParameterChoice` with zero choices is invalid — no guard exists.

The blast radius is a downstream effect author who ships a discrete descriptor with a bad count and gets a plugin that maps parameters wrong (or constructs an invalid JUCE parameter) with no diagnostic. The fix is a precondition check at `build` time: throw `std::invalid_argument` (or the adapter's error type) when a `ParamKind::discrete` descriptor has `discreteCount < 2`, rather than papering over it at apply time.

### `defaultValue` is interpreted as a raw index for discrete params but as a plain (pre-normalize) value for continuous — inconsistent contract

Finding-ID: AUDIT-BARRAGE-claude-05
Status:     open
Severity:   low
Surface:    adapters/plugin/plugin-parameters.cpp:50 (discrete default), :58 (continuous default)

In `build`, the continuous branch treats `d.defaultValue` as a plain unit value and runs it through `normalize(d, d.defaultValue)` (line 58). The discrete branch instead treats `d.defaultValue` as a raw choice index: `const int defaultIndex = static_cast<int>(d.defaultValue)` (line 50). So the same `ParameterDescriptor::defaultValue` field carries two different meanings depending on `kind`. This may be the intended convention (discrete defaults stored as indices), but nothing in this chunk documents it, and the asymmetry is exactly the kind of quietly-divergent contract that produces a wrong default in a future discrete parameter — e.g. if an author stores a discrete default as a normalized 0..1 value (mirroring how they think of continuous defaults), `static_cast<int>(0.5f)` collapses to index 0.

The blast radius is bounded (one effect, one parameter, wrong startup default — a reader would likely notice), so this is low, but it is a load-bearing convention that should be stated where `ParameterDescriptor` is defined. Fix: document the discrete-default-is-an-index rule on the descriptor field, or normalize the discrete default through the same path for a single consistent meaning.

### `ARDUINO_TEENSY40` and pinned toolchain version numbers are hardcoded while the surrounding docs say "Teensy 4.x"

Finding-ID: AUDIT-BARRAGE-claude-06
Status:     open
Severity:   low
Surface:    adapters/teensy/CMakeLists.txt:31 (compile defs), :1-2 (header comment), :36 (linker script)

The file header and `teensy-main.cpp` both describe the target as "Teensy 4.x", but `target_compile_definitions` hardcodes `ARDUINO_TEENSY40` (line 31), pinning specifically to the Teensy 4.0 board, and bakes in `ARDUINO=10813 TEENSYDUINO=159` as magic version literals with no check that the fetched `teensy_cores`/`teensy_audio` actually match those versions. A Teensy 4.1 build needs `ARDUINO_TEENSY41` (different pin map / PSRAM), so the "4.x" framing in the comments overstates what this build actually produces — documentation drift between the comment and the single board the defines select.

The blast radius is small (a 4.1 user would hit build/pinout problems and investigate), so this is low/hygiene, but worth either narrowing the comments to "Teensy 4.0" or parameterizing the board define (and the implied linker-script choice on line 36) so the doc and the build agree. Surfacing it here because the govern commit specifically targeted "doc drift."

---

**Summary for triage:** the two findings worth prioritizing are **claude-01** (a genuine message-thread/audio-thread use-after-free in the workbench source switch, with unsafe statement ordering on top of the missing synchronization) and **claude-03** (the SVF-specific mode names leaking into a class sold as descriptor-generic, which also silently mislabels any `discreteCount != 3`). Findings 02/04/05/06 are correctness-hardening and contract/doc-consistency notes. I checked the plugin `processBlock` channel handling (`kMaxChannels=8` is safe given `isBusesLayoutSupported` caps at stereo), the MIDI binding's audio-thread `find()` (RT-safe, no alloc on read), and the teensy int16↔float scaling asymmetry (32768-in / 32767-out is conventional clip-avoidance, not a bug) — those came back clean.