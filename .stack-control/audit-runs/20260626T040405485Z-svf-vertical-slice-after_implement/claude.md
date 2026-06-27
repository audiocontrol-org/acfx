I'm being invoked as one audit lane in the cross-model barrage. I walked chunk `a70ef633c8635ce4` (plugin + teensy + workbench-source adapters) against the diff. Findings below.

### Discrete-parameter choice labels are a hand-written, index-keyed list that silently desyncs from the descriptor

Finding-ID: AUDIT-BARRAGE-claude-01
Status:     open
Severity:   medium
Surface:    adapters/plugin/plugin-parameters.cpp:18-27, :40-47

`modeName(int index)` hardcodes `"lowpass"/"highpass"/"bandpass"` keyed positionally, and `build()` applies it to **every** `ParamKind::discrete` descriptor (`for (int i = 0; i < d.discreteCount; ++i) choices.add(modeName(i))`). This directly contradicts the file's own header claim (plugin-parameters.h:9-10): *"There is no hand-written parameter list: each ParameterDescriptor becomes a JUCE parameter."* The choice **labels** are exactly that hand-written list, and they assume every discrete parameter is the SVF mode knob.

Two concrete blast-radii. (1) The `default:` branch returns `"lowpass"` for any index ≥ 3, so the moment the SVF gains a 4th mode (notch/allpass are the obvious next entries for a state-variable filter) the GenericAudioProcessorEditor renders a **duplicate "lowpass" label** for the new mode — an adopter selects "lowpass" in the host and silently gets notch. (2) Any second discrete descriptor added to the table inherits filter-mode names regardless of meaning. The descriptor table is asserted to be the single source of truth (SC-006); the fix is to carry the choice names on the descriptor (e.g. a `span<const char*>` of labels) and have `build()` read them, so labels can never desync from `discreteCount`. Today (3 modes) it is correct, which is why this is medium rather than high — but it is a latent correctness defect, not just hygiene, the instant the table grows.

### Teensy int16 output conversion is not NaN-safe — the round-4 NaN-safe-clamp invariant was not applied to this surface

Finding-ID: AUDIT-BARRAGE-claude-02
Status:     open
Severity:   medium
Surface:    adapters/teensy/teensy-main.cpp:35-43

The output stage clamps with two one-sided comparisons:
```cpp
float v = samples[i] * kFloatToInt16;
if (v > 32767.0f)  v = 32767.0f;
if (v < -32768.0f) v = -32768.0f;
block->data[i] = static_cast<int16_t>(v);
```
A NaN sample passes **both** comparisons (every comparison with NaN is false), so `v` stays NaN and `static_cast<int16_t>(NaN)` is undefined behavior — on the IMXRT1062 this writes an arbitrary/garbage 16-bit value straight to the SGTL5000 DAC. This is the same defect shape the project already dispositioned in round 4 ("NaN-safe clamp", commit 3262fb3) for the core/host path, but the invariant was not carried to the firmware's last-line output conversion. Per the round-0 self-red-team driver, this fix-range opened a hardware-output edge that re-introduces the exact pattern an earlier round closed elsewhere.

The header comment leans on the core being NaN-safe, but a hardware DAC boundary should not depend on an upstream guarantee for a UB-vs-defined distinction — a single denormal or bad coefficient anywhere upstream becomes UB at the codec. The fix is the NaN-safe clamp form already adopted in core: order the test so NaN falls into a defined branch (e.g. `v = (v >= -32768.0f) ? (v <= 32767.0f ? v : 32767.0f) : -32768.0f;` with an explicit `if (std::isnan(v)) v = 0.0f;` guard, or reuse the core clamp primitive). Blast radius is bounded by the core guard making NaN unlikely, hence medium, but the consequence if it occurs is UB on the audio output of a shipped firmware.

### audio-source.cpp is declared in scope but its diff is absent, so the header's RT-safety and enforced-precondition claims are unverifiable in this chunk

Finding-ID: AUDIT-BARRAGE-claude-03
Status:     open
Severity:   low
Surface:    adapters/workbench/audio-source.h (whole), missing adapters/workbench/audio-source.cpp

The chunk's "Files in scope" lists `adapters/workbench/audio-source.cpp`, but only `audio-source.h` appears under Diffs. The header makes strong, load-bearing safety assertions that live entirely in the `.cpp`: that `fillBlock()` "never throws, never allocates, takes no locks"; that source selection "is ENFORCED — a selection call while already configured throws"; and that the file is fully decoded before the stream starts. None of these can be checked from the declaration alone — `configured_` being an atomic only makes the *read* well-defined; whether `useFilePlayer`/`useLiveInput` actually consult it and throw, and whether `fillBlock` is genuinely allocation/lock-free, is invisible here.

This is reported low because it is most likely a chunk-splitting artifact rather than a real omission — but it is worth surfacing because the enforcement guarantee (the throw-on-reconfigure that prevents the audio thread from reading a buffer mid-reassignment) is exactly the kind of claim a downstream reader would take on faith from the header. The operator should confirm the `.cpp` lands in a sibling chunk and that the guard + RT-safety are actually implemented, not merely documented.