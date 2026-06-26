I'm the audit reviewer for this barrage instance. The prompt delivered the diff directly — I'll walk the code and emit findings now.

---

### Race condition: `svf.setParameter()` in `loop()` is not guarded against audio ISR

Finding-ID: AUDIT-BARRAGE-claude-01
Status:     open
Severity:   high
Surface:    adapters/teensy/teensy-main.cpp:83-89 (`loop()`) vs :35 (`svf.process()` in `update()`)

On Teensy 4.x the Teensy Audio Library fires `AcfxSvfNode::update()` from a hardware interrupt at ~344 Hz (44100 / 128). `loop()` runs on the main (non-interrupt) thread. The global `svf` object (`acfx::SvfEffect svf`, line ~18) is accessed from both contexts without synchronization:

- `loop()` lines 83-89 call `svf.setParameter(...)` three times in sequence.
- `update()` line 35 calls `svf.process(audioBlock)`, which reads the same parameter state the loop is writing.

There is no `AudioNoInterrupts()` / `AudioInterrupts()` guard around the `setParameter` calls in `loop()`. On ARM Cortex-M7 (IMXRT1062), non-atomic multi-word writes to struct members are not ISR-safe — the audio interrupt can fire between any two `setParameter` calls and observe a torn state. This is the standard Teensy Audio Library data-race failure mode.

The fix is to wrap the three `setParameter` calls in `loop()` with `AudioNoInterrupts()` / `AudioInterrupts()`, which disables the audio interrupt for the duration of the parameter update:

```cpp
AudioNoInterrupts();
svf.setParameter(acfx::ParamId{acfx::SvfEffect::kCutoff}, ...);
svf.setParameter(acfx::ParamId{acfx::SvfEffect::kResonance}, ...);
svf.setParameter(acfx::ParamId{acfx::SvfEffect::kMode}, ...);
AudioInterrupts();
```

---

### `modeName()` is hardcoded and falls through to wrong label for index ≥ 3

Finding-ID: AUDIT-BARRAGE-claude-02
Status:     open
Severity:   medium
Surface:    adapters/plugin/plugin-parameters.cpp:23-31 (`modeName`) and :55-57 (`build` loop)

`modeName()` handles only three indices (0, 1, 2). The `build` loop iterates `for (int i = 0; i < d.discreteCount; ++i)` — if the SVF effect ever gains a 4th mode and `discreteCount` rises to 4, `modeName(3)` hits the `default:` arm and returns `"lowpass"`, silently mislabeling the new mode in every host that displays the choice parameter.

This is also a design-layer violation: mode labels are effect semantics. Hardcoding them in the plugin adapter rather than deriving them from the effect's descriptor (or a shared constant) means the adapter has private knowledge of the effect's internal enumeration. When the effect gains a mode, the adapter breaks silently — the build does not catch it.

The fix is either (a) add mode name strings to `ParameterDescriptor` (alongside `discreteCount`) so the adapter never needs to know what the choices mean, or (b) at minimum add a `jassert(index < d.discreteCount)` inside `modeName` and a `static_assert` or runtime check that `discreteCount == 3` when constructing discrete parameters in this adapter.

---

### `getStateInformation`/`setStateInformation` are silent no-ops against project's "throw, don't fallback" rule

Finding-ID: AUDIT-BARRAGE-claude-03
Status:     open
Severity:   medium
Surface:    adapters/plugin/plugin-processor.h:43-44

The project's constitution (CLAUDE.md) is explicit: "Never implement fallbacks or use mock data outside of test code. Throw errors with a description of the missing functionality." The two state-persistence overrides are pure no-ops with no comment beyond "out of scope":

```cpp
void getStateInformation(juce::MemoryBlock&) override {}
void setStateInformation(const void*, int) override {}
```

A DAW that saves a session calls `getStateInformation`, receives empty bytes, and silently records nothing. On session reload it calls `setStateInformation` with whatever it saved (possibly from a later version of the plugin that does implement state), and the call is silently discarded. This means two failure modes — lost parameters on re-open, and stale saved state silently ignored — with no diagnostic signal. The project's no-fallback rule exists precisely to prevent this class of hidden failure.

JUCE does not permit throwing from these methods (the host calls them on the message thread and a throw would terminate the process), so a literal `throw` is not an option. The compliant alternative is `jassertfalse` (debug assertion) in both methods, plus a `DBG("state persistence not implemented")` log, so that the gap is at least visible during development. The comment should also be updated to say what observable behavior a user will experience (parameters reset to defaults on every session reload).

---

### `MidiBinding::bind()` and `handle()` have undocumented cross-thread ownership

Finding-ID: AUDIT-BARRAGE-claude-04
Status:     open
Severity:   medium
Surface:    adapters/workbench/midi-binding.h:25 (`bind`) and :28-36 (`handle`)

`bind()` mutates `bindings_` (an `std::unordered_map<int, ParamId>`). `handle()` reads `bindings_` — and is presumably called from the JUCE audio callback thread when MIDI messages arrive. If `bind()` is called from the message thread (the natural call site for a UI-driven binding setup) while `handle()` is running on the audio thread, the unordered_map is concurrently mutated and read without any lock or atomic. This is undefined behavior on all platforms.

The class carries no documentation of its thread-ownership model, no `jassert(MessageManager::existsAndIsCurrentThread())` guard on `bind()`, and no lock. Even if bindings are set once at startup before audio begins (the current likely usage), the API exposes `bind()` as re-callable at any time, making it a latent race that a future caller will trigger.

Fix: add a comment to `bind()` stating it must only be called off the audio thread while audio is not running, OR protect `bindings_` with a lock or lock-free structure, OR use JUCE's standard pattern of pushing parameter changes through an `AsyncUpdater` or `ChangeBroadcaster`.

---

### `analogRead` normalization assumes 10-bit ADC resolution without asserting it

Finding-ID: AUDIT-BARRAGE-claude-05
Status:     open
Severity:   low
Surface:    adapters/teensy/teensy-main.cpp:83-88

All three `analogRead()` calls divide by `1023.0f`, assuming 10-bit ADC resolution. Teensy 4.x ships with a 12-bit ADC (0..4095 native) and exposes `analogReadResolution(bits)`. The Teensy Arduino compatibility layer defaults to 10-bit (0..1023), so the divisor is correct *today* with no explicit resolution call. However, if any other code in the firmware (or a future library pulled into the project) calls `analogReadResolution(12)`, the knob range shrinks to roughly 25% of travel with no compile-time or runtime warning.

The fix is to add `analogReadResolution(10);` in `setup()` to make the assumption explicit and robust against future changes, or alternatively use `(float)analogRead(pin) / (float)((1 << analogReadResolution()) - 1)` if the Teensyduino runtime exposes the current resolution setting.

---

### `kMaxChannels = 8` is an unexplained magic number inconsistent with the stereo gate

Finding-ID: AUDIT-BARRAGE-claude-06
Status:     open
Severity:   low
Surface:    adapters/plugin/plugin-processor.h:48, adapters/plugin/plugin-processor.cpp:39-43

`kMaxChannels` is set to 8 in the header but `isBusesLayoutSupported` rejects everything except mono and stereo (maximum 2 channels). The `processBlock` loop allocates an 8-slot `std::array<float*, 8>` and then uses `jmin(buffer.getNumChannels(), 8)` — but since the bus validator already rejects anything beyond stereo, `buffer.getNumChannels()` will never exceed 2 at this point. The cap at 8 is belt-and-suspenders but the constant implies a future-proofing intent that isn't implemented, and it will confuse a reader who wonders whether the plugin actually supports surround formats. Either lower it to 2 (the true maximum) and document it as matching the bus constraint, or document why 8 was chosen.