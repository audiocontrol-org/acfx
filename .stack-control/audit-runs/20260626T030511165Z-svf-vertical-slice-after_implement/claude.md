I'll walk this chunk of the `svf-vertical-slice` diff carefully and report what I find independently.

### Missing-synchronization defect: the `configured_` guard meant to prevent the audio-thread race is itself a non-atomic cross-thread bool

Finding-ID: AUDIT-BARRAGE-claude-01
Status:     open
Severity:   high
Surface:    adapters/workbench/audio-source.h:60 (`bool configured_ = false;`); adapters/workbench/audio-source.cpp:10,33,52,55

The header makes a strong safety claim (audio-source.h:18-22): *"That precondition is ENFORCED — a selection call while already configured throws — so the audio thread never reads a buffer being reassigned."* The enforcement is `if (configured_) throw …` in `useFilePlayer`/`useLiveInput` (audio-source.cpp:10,33). But `configured_` is a plain `bool`, written in `prepare()`/`release()` (cpp:52,55) and read in the selection calls. `prepare()`/`release()` run on JUCE's audio/device thread (via `prepareToPlay`/`releaseResources`); the selection calls run on the message thread. Every other shared field here (`hasFile_`, `live_`, `playPos_`) is correctly a `std::atomic` with acquire/release ordering — `configured_` is the one that isn't.

The consequence is not merely formal UB on a bool. The misuse this guard exists to catch is "call `useFilePlayer` while the stream is running." In exactly that case, with no acquire/release on `configured_`, the message thread may fail to observe the `configured_ = true` written by the audio-setup thread, the guard passes, and `useFilePlayer` then does `fileBuffer_ = std::move(decoded)` (cpp:38) — reassigning a non-atomic `juce::AudioBuffer` that the audio thread is concurrently reading in `fillBlock` (cpp:78). That is a real data race on `fileBuffer_` (torn read of a half-moved buffer → garbage/crash), and the guard the comment relies on does not reliably fire to stop it.

Fix: make `configured_` `std::atomic<bool>` with `store(release)`/`load(acquire)`, mirroring the treatment already applied to `hasFile_`/`live_`. That at least gives the guard the visibility guarantee its own comment promises (a fuller fix would also close the check-then-reassign TOCTOU window).

### Effect-specific filter-mode names hardcoded inside the "no hand-written parameter list" generic builder

Finding-ID: AUDIT-BARRAGE-claude-02
Status:     open
Severity:   medium
Surface:    adapters/plugin/plugin-parameters.cpp:25-34, 56-58; plugin-parameters.h:12-14

`PluginParameters::build` is documented (plugin-parameters.h:12-14) as a purely descriptor-driven builder: *"There is no hand-written parameter list: each ParameterDescriptor becomes a JUCE parameter."* Yet the discrete branch fills choices by calling `modeName(i)` (cpp:56-58), and `modeName` hardcodes SVF-specific strings — `"lowpass"`/`"highpass"`/`"bandpass"` (cpp:25-34) — with a `default` of `"lowpass"`. So the "generic" builder embeds effect-specific knowledge and silently assumes every discrete parameter is the SVF mode with exactly three choices.

Two concrete failure channels this opens, neither covered by a fixture: (1) **value channel** — a discrete descriptor with `discreteCount > 3` yields multiple choices all labelled `"lowpass"` (indices ≥3 hit the `default`), producing duplicate `AudioParameterChoice` entries that hosts disambiguate poorly; (2) **composition channel** — any second discrete parameter added later inherits filter-mode names regardless of what it represents. The label table belongs in the descriptor (e.g. a `choiceLabels` span on `ParameterDescriptor`), not in a `switch` inside the adapter. As written the SC-006 "identical mapping across adapters" claim holds only by accident of there being exactly one discrete parameter with exactly three modes.

### `processBlock` clamps channel count to a magic `kMaxChannels=8` rather than the prepared channel count → possible out-of-bounds on per-channel state

Finding-ID: AUDIT-BARRAGE-claude-03
Status:     open
Severity:   medium
Surface:    adapters/plugin/plugin-processor.cpp:14-15,32-38; plugin-processor.h:50

`prepareToPlay` builds the `ProcessContext` with `getTotalNumOutputChannels()` (cpp:14), so the effect allocates/prepares per-channel state for that many channels. But `processBlock` then sizes the work to `juce::jmin(buffer.getNumChannels(), kMaxChannels)` with `kMaxChannels = 8` (cpp:32; h:50) — a magic number unrelated to what was actually prepared. The two only agree because `isBusesLayoutSupported` (cpp:18-25) currently restricts to mono/stereo, capping the buffer at 2 channels well under 8.

The latent bug is that the clamp's invariant is wrong: it bounds to an arbitrary constant rather than to the prepared channel count. If the layout constraint is ever relaxed, or a host probes `processBlock` with a buffer wider than the prepared count (some hosts do during scanning/initialisation before a matching `prepareToPlay`), the effect will be handed up to 8 channel pointers while its internal per-channel state was sized for fewer — an out-of-bounds index inside the SVF. The clamp should be `juce::jmin(buffer.getNumChannels(), preparedChannels_)`, capturing the value passed to `prepare`, so the process path can never exceed what was prepared. `kMaxChannels=8` as the `std::array` capacity is fine; using it as the *processing* bound is the defect.

### Teensy float→int16 conversion clamps magnitude but is not NaN-safe; `static_cast<int16_t>(NaN)` is UB

Finding-ID: AUDIT-BARRAGE-claude-04
Status:     open
Severity:   low
Surface:    adapters/teensy/teensy-main.cpp:33-40

The output stage clamps with two ordered `if` comparisons (`v > 32767 → 32767`, `v < -32768 → -32768`) and then `static_cast<int16_t>(v)` (teensy-main.cpp:33-40). If `v` is `NaN`, both comparisons are false (NaN compares false against everything), the clamp is skipped, and `static_cast<int16_t>(NaN)` is undefined behaviour — on ARM it typically yields 0 or 0x8000, i.e. a full-scale click. Round-4 (commit 3262fb3, *"NaN-safe clamp"*) explicitly hardened a clamp elsewhere against exactly this; the Teensy egress path was not given the same treatment.

This depends on the SVF emitting a `NaN`, which a stable filter should not under bounded input — hence low — but the firmware integerises whatever the DSP produces without a guard, so any transient instability (e.g. denormal/Inf during a fast cutoff sweep on the MCU) converts to a hardware-audible artifact instead of silence. A NaN-first ordering (`if (!(v >= -32768.0f)) v = -32768.0f; else if (v > 32767.0f) v = 32767.0f;` or an explicit `isnan` check) closes it and matches the convention round-4 established for the rest of the codebase.

### Generic float parameters have a `stringFromValue` but no `valueFromString`, so host/editor text entry round-trips to the wrong value

Finding-ID: AUDIT-BARRAGE-claude-05
Status:     open
Severity:   low
Surface:    adapters/plugin/plugin-parameters.cpp:48-60

Continuous parameters are created over a normalised `0..1` range (cpp:58-60) with a `withStringFromValueFunction` that *displays* the denormalised plain value (cpp:53-56), but no matching `withValueFromStringFunction`. With `GenericAudioProcessorEditor` (and host generic UIs) the display now reads e.g. `"440.00"` for cutoff, but when the operator types a value back, JUCE's default string→value path parses the entered number against the parameter's own `0..1` range, not the plain-unit range used for display. Typing the plain value the UI just showed (e.g. `440`) is interpreted as a normalised `440`, clamped to `1.0` — i.e. jumps the parameter to maximum.

So the display/entry pair is asymmetric: read-out is in Hz/dB, write-back is in normalised units, and the two disagree by the skew. Blast radius is confined to manual text entry in a generic UI (sliders/automation are unaffected), hence low, but it's a real round-trip defect for the exact display function this code adds. The fix is to pair every `withStringFromValueFunction` with a `withValueFromStringFunction` that calls `normalize(desc, parsedPlain)`.

### Teensy parameter publish recomputes coefficients inside `AudioNoInterrupts()`, potentially extending the ISR-blocking critical section

Finding-ID: AUDIT-BARRAGE-claude-06
Status:     open
Severity:   low
Surface:    adapters/teensy/teensy-main.cpp:88-96

`loop()` brackets the three `svf.setParameter(...)` calls in `AudioNoInterrupts()/AudioInterrupts()` (teensy-main.cpp:91-95) so the audio ISR can't observe a partial parameter group — a reasonable goal. The hidden cost is that `AudioNoInterrupts` disables the audio interrupt for the *entire* duration of those three calls. If `SvfEffect::setParameter` recomputes filter coefficients eagerly (cutoff/resonance typically imply trig + division), that recompute now runs with audio interrupts masked, and on a Teensy-class MCU three coefficient recomputes can be a non-trivial number of cycles to hold the ISR off — risking dropped audio blocks under rapid knob movement.

I can't see `svf-effect.h` in this chunk, so this is conditional: if `setParameter` only stores the normalised value and defers coefficient math to `process()`, the critical section is three float stores and this is a non-issue. If it recomputes eagerly, the masked region is unbounded from this file's view. The safe shape is to publish the three normalised values into a small lock-free snapshot the ISR latches at block boundaries (so the group is still atomic) without masking interrupts across the DSP math — keeping the no-interrupt window to plain stores only. Flagging so the operator confirms which side of that line `setParameter` falls on.

---

I checked the parameter round-trip math (discrete midpoint `(index+0.5)/count` denormalises back to the correct index for `count=3`; the `count<2?2` floor is degenerate-only), the file-player loop wrap (`pos` reset per channel keeps channels phase-aligned, and the final `%fileLen` store is correct even for sub-block-length files), the 64-bit length validation before the `int` narrow in `useFilePlayer` (cpp:23-31, sound), and the bus-layout in==out constraint (cpp:18-25, correct). Those came back clean. The six findings above are where the audited surfaces either make a safety claim the code doesn't quite back (01), embed effect-specific data as code (02), or bound a loop/critical section by something other than the invariant that actually governs it (03, 06).