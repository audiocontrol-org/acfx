### Teensy mode-pin normalization produces `norm = 1.0`, potentially yielding out-of-range discrete index

Finding-ID: AUDIT-BARRAGE-claude-01
Status:     open
Severity:   high
Surface:    adapters/teensy/teensy-main.cpp:86

`analogRead()` returns values in `[0, 1023]`. The mapping `static_cast<float>(analogRead(kModePin)) / 1023.0f` yields exactly `1.0f` when the pin reads 1023 — its documented maximum. If `SvfEffect::setParameter` discretizes mode using the common idiom `floor(norm * count)` (count = 3 for LP/HP/BP), then `floor(1.0f * 3) = 3`, which is an out-of-range index. This is a 1-in-1024 frequency hardware bug that would manifest as unpredictable filter behaviour when the cutoff knob sits at its physical maximum. The project constitution forbids defensive clamping in the core ("no fallbacks outside test code"), so the adapter must guard this boundary. The fix is `std::min(static_cast<float>(analogRead(kModePin)) / 1023.0f, std::nextbelow(1.0f))` or `analogRead(kModePin) * count / 1024` in integer arithmetic, depending on how the effect's discretization is defined.

---

### `modeName()` in the plugin adapter hardcodes mode labels detached from the SvfEffect descriptor

Finding-ID: AUDIT-BARRAGE-claude-02
Status:     open
Severity:   medium
Surface:    adapters/plugin/plugin-parameters.cpp:24-33

`modeName(int index)` returns `"lowpass"` / `"highpass"` / `"bandpass"` at fixed indices 0, 1, 2. These labels must match `SvfEffect`'s internal mode ordering exactly, but nothing enforces that: the descriptor table (the canonical parameter metadata contract referenced throughout the plan) carries no mode-name strings, so the adapter duplicates knowledge that only the effect owns. If the effect reorders modes — for example, if HP and BP swap during a refactor — the DAW UI silently shows wrong labels, and no compiler diagnostic fires. The correct fix is to add an optional `const char* labels[]` field to `ParameterDescriptor` (or a parallel `discreteLabels` span) so the adapter can pass `descriptors.labels[i]` to `modeName` rather than hard-coding indices.

---

### `getStateInformation` / `setStateInformation` are silent no-ops that violate the project no-fallbacks rule

Finding-ID: AUDIT-BARRAGE-claude-03
Status:     open
Severity:   medium
Surface:    adapters/plugin/plugin-processor.h:44-45

Both state-persistence overrides are empty bodies:

```cpp
void getStateInformation(juce::MemoryBlock&) override {}
void setStateInformation(const void*, int) override {}
```

The comment claims this is "intentionally no-op, not a silent fallback," but the project CLAUDE.md is unambiguous: "Never implement fallbacks or use mock data outside of test code. Throw errors with a description of the missing functionality and/or data instead." A host that calls `getStateInformation` expecting serialised state receives an empty buffer; on session reload it silently restores defaults. The semantic difference between "intentional no-op" and "silent fallback" is zero from a downstream consumer's perspective — the failure is hidden. Per the project rule the correct behaviour is to `throw std::runtime_error("preset persistence not implemented")` inside both methods, or at minimum `jassert(false)` with a message. This should be revisited the moment storage moves into scope.

---

### `discreteCount < 2` guard in `apply()` silently normalises a malformed descriptor

Finding-ID: AUDIT-BARRAGE-claude-04
Status:     open
Severity:   medium
Surface:    adapters/plugin/plugin-parameters.cpp:80

```cpp
const int count = e.descriptor.discreteCount < 2 ? 2 : e.descriptor.discreteCount;
```

A discrete parameter with `discreteCount == 0` or `== 1` is a malformed descriptor — no meaningful choice set can be expressed. Rather than failing loudly, this guard silently substitutes `count = 2`, assigning the one valid index a bin-midpoint of `0.25f` and propagating a nonsensical value to the effect. Per project guidelines the fault should be caught in `build()` where all descriptors are walked anyway, with a `throw` carrying a message naming the offending parameter. Catching it at `apply()` time — which runs every audio block, on the hot path — is the wrong place: it converts a constructor-time invariant violation into a silent per-block mismatch.

---

### `MidiBinding::bind()` and `handle()` race on `std::unordered_map` with no documented precondition

Finding-ID: AUDIT-BARRAGE-claude-05
Status:     open
Severity:   medium
Surface:    adapters/workbench/midi-binding.h:23-40

`bind()` calls `bindings_[ccNumber] = id`, which may trigger rehashing and invalidates all iterators. `handle()` calls `bindings_.find(...)`. Both are public and undocumented as to threading constraints. A natural UI workflow — user reassigns a CC while audio is running — would call `bind()` from the UI/main thread while `handle()` fires on the MIDI callback thread, yielding a data race and undefined behaviour on `std::unordered_map`. The class offers no synchronisation. The minimum fix is to document the precondition "all `bind()` calls must complete before audio starts" as a contract on the class, reinforced with a `configured_`-style guard that asserts in debug builds. A safer alternative is a small fixed-size array (CC numbers are 0–127) that admits lock-free reads.

---

### `codec.enable()` return value unchecked in Teensy `setup()`

Finding-ID: AUDIT-BARRAGE-claude-06
Status:     open
Severity:   low
Surface:    adapters/teensy/teensy-main.cpp:75

`codec.enable()` communicates over I2C and returns `bool` indicating success. If no Audio Shield is attached, or if I2C initialisation fails, the function returns `false` and audio is silently absent — `setup()` completes normally, `loop()` runs, knobs are sampled, but no sound is produced and no diagnostic fires. Given that "no fallbacks or mock data outside test code" is a project commandment, unverified hardware initialisation is the hardware-bring-up analogue of a swallowed exception. The fix is a `Serial.println` or a status-LED blink path gated on `!codec.enable()`, consistent with the project rule that failure modes must be surfaced rather than absorbed.