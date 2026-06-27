I walked this chunk carefully. Here are my findings.

```
### File player ignores the device sample rate — no resampling, audible pitch/speed shift on SR mismatch

Finding-ID: AUDIT-BARRAGE-claude-01
Status:     open
Severity:   medium
Surface:    adapters/workbench/audio-source.cpp:38-82, adapters/workbench/audio-source.h:46

`WorkbenchAudioSource::prepare(double sampleRate, int blockSize)` takes a sample
rate but discards it entirely (both parameters are unnamed in the .cpp
definition: `void WorkbenchAudioSource::prepare(double, int)`). The file is
decoded into `fileBuffer_` at the file's *native* rate by
`reader->createReaderFor`/`reader->read`, and `fillBlock` then copies it into the
device block one stored sample per output sample (`dst[i] = src[pos]; ++pos`) with
no sample-rate conversion. A 44.1 kHz file played through a device opened at
48 kHz therefore plays ~8.8% fast and sharp; any SR mismatch is silently
mis-pitched.

The whole point of the sketch-and-hear workbench is to audition the filter on
real audio, and the documented role of the player is a "deterministic default for
reproducible A/B" — but an A/B at the wrong pitch is not reproducible against the
source material, and a user will hit this immediately because 44.1 kHz files on
48 kHz interfaces are the common case. The discarded `sampleRate` parameter is
the evidence the conversion was simply never wired. The filter demonstration
itself still works (so this is not blocking), but the audio is wrong. A fix
should either resample the decoded buffer to the prepared device rate at setup
(off the audio thread, where allocation is allowed) or store the ratio and
advance `playPos` by a fractional step with interpolation in `fillBlock`.
```

```
### Discrete choice names are hardcoded in the plugin, decoupling them from the SvfEffect mode definition

Finding-ID: AUDIT-BARRAGE-claude-02
Status:     open
Severity:   medium
Surface:    adapters/plugin/plugin-parameters.cpp:28-37, 50-56

The file's stated premise (plugin-parameters.h:9-15) is that there is "no
hand-written parameter list" — each `ParameterDescriptor` becomes a JUCE
parameter so the mapping is identical across adapters. But the *names* of the
discrete choices are hand-written by integer index in `modeName(int index)`
(0→"lowpass", 1→"highpass", 2→"bandpass"), and `build()` populates the
`AudioParameterChoice` choices purely from `d.discreteCount` plus this local
table. The descriptor carries the count but not the labels, so the labels live as
code here, divorced from wherever `SvfEffect` actually defines its mode enum.

If `SvfEffect`'s mode ordering ever changes (or a fourth mode is added — notch is
a common SVF output), this table silently mislabels every mode in the host UI:
`discreteCount` would grow to 4, `build()` would call `modeName(3)` which hits the
`default` and labels mode 3 "lowpass" too, and modes 1/2 could be swapped with no
compile error. This is config-as-code: the choice labels should travel in the
descriptor table alongside `discreteCount` (e.g. a `span<const char* const>` of
names) so the single-source-of-truth claim actually holds. As written, the plugin
and the effect can disagree about what mode 2 is, and nothing catches it.
```

```
### Teensy version defines (TEENSYDUINO/ARDUINO) are baked as literals, decoupled from the pinned core version

Finding-ID: AUDIT-BARRAGE-claude-03
Status:     open
Severity:   medium
Surface:    adapters/teensy/CMakeLists.txt:30

`target_compile_definitions(teensy_platform PUBLIC ARDUINO=10813 TEENSYDUINO=159
ARDUINO_TEENSY40)` hardcodes `TEENSYDUINO=159` (Teensyduino 1.59) and
`ARDUINO=10813` as literals, while the actual Teensy cores/Audio sources are
fetched and *version-pinned* in cmake/dependencies.cmake (a sibling chunk, per
the file list for 4cfb00d5b3480886). These two facts are independent: if the pin
in dependencies.cmake is ever bumped (or already differs) from 1.59, the firmware
compiles against newer/older core sources while announcing `TEENSYDUINO=159` to
every `#if TEENSYDUINO >= N` guard inside those sources.

The blast radius is real because the Teensy core uses these macros for
conditional compilation of the IMXRT runtime; a mismatched value selects the
wrong code path against the actual sources, producing subtle runtime breakage
that no build error reveals. The version macro should be derived from the same
pinned version variable dependencies.cmake uses, not duplicated as a literal in a
second file, so the two cannot drift. At minimum, add a comment + a CMake check
asserting the literal equals the pinned version.
```

```
### Board is hard-pinned to ARDUINO_TEENSY40 despite the "Teensy 4.x" claim

Finding-ID: AUDIT-BARRAGE-claude-04
Status:     open
Severity:   low
Surface:    adapters/teensy/CMakeLists.txt:1-3, 30

The header comment describes this as a "Teensy 4.x firmware" target, but
`target_compile_definitions` hardcodes `ARDUINO_TEENSY40`, restricting the build
to the Teensy 4.0 specifically. The Teensy core uses `ARDUINO_TEENSY40` vs
`ARDUINO_TEENSY41` to gate board-specific facilities (4.1 adds PSRAM, extra
pins/SD). A user with a Teensy 4.1 — the more common board for audio projects —
who configures the `teensy` preset gets a 4.0-defined build, with any
4.1-specific behavior compiled out.

This is config-as-code: the board variant is a real configuration axis that
should be a cache variable (e.g. `ACFX_TEENSY_BOARD`) defaulting to one value,
not a literal contradicting the "4.x" comment. Low severity because both boards
share the imxrt1062 linker script and the SGTL5000 path works on both, so the
audio chain still functions; the cost is the documentation/code mismatch and
silently disabled 4.1 features.
```

```
### useLiveInput() leaves hasFile_ set, creating asymmetric state that relies on fillBlock's check order

Finding-ID: AUDIT-BARRAGE-claude-05
Status:     open
Severity:   low
Surface:    adapters/workbench/audio-source.cpp:33-40 (vs 17-31)

`useFilePlayer` carefully resets the *other* source's flag
(`live_.store(false, release)` at line 30) so the two selection paths are
mutually exclusive. `useLiveInput` does not do the mirror: it sets
`live_.store(true)` but never clears `hasFile_`. So the sequence
`useFilePlayer(f)` then `useLiveInput(n)` (both legal before `prepare()`, since
`configured_` is still false) leaves `hasFile_ == true` AND `live_ == true`
simultaneously.

Today this is masked because `fillBlock` checks `live_` first and returns
(line 53), so live correctly wins. But the invariant "exactly one source
selected" is not actually maintained in the state — it's maintained only by the
ordering of two reads in a different function. If that check order is ever
reordered, or `isLiveInput()` is consulted alongside `hasFile_` elsewhere, the
inconsistency becomes a bug. Add `hasFile_.store(false, std::memory_order_release)`
to `useLiveInput` to make the two selectors symmetric and the state
self-consistent regardless of who reads it.
```