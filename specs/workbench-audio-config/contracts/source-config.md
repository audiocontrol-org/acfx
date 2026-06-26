# Contract — `SourceConfig` serialize/parse (workbench-settings)

The one pure, device-free seam of this feature — the part with deterministic
behaviour worth a host-side unit test. Everything else (device dropdowns, the
restart cycle, the file chooser, MIDI selection) is interactive JUCE UI verified by
manual acceptance + compile-verification.

## Types

```cpp
enum class SourceMode { live, file };

struct SourceConfig {
    SourceMode mode = SourceMode::live;
    juce::String filePath; // empty unless mode == file
};
```

## Functions (pure — no device, no audio thread, no UI)

```cpp
juce::String serialize(const SourceConfig& cfg);   // -> a stable settings string
SourceConfig parse(const juce::String& text);      // <- never throws
```

## Behavioral guarantees (normative)

- **Round-trip**: `parse(serialize(cfg)) == cfg` for every valid `cfg` (both modes,
  arbitrary file paths including spaces/unicode).
- **Safe default on garbage**: `parse("")`, `parse("nonsense")`, or a string with an
  unknown mode token returns the default `SourceConfig{ live, "" }` — it never throws
  and never returns a half-populated `file` mode with an empty path. (Defaulting an
  absent/corrupt *setting* is config parsing, distinct from the Constitution-V ban on
  silent *audio* fallbacks: a missing setting has a legitimate default; a missing
  audio source is surfaced at the audio layer.)
- **`file` validity**: `serialize` of `file` mode includes the path verbatim;
  `parse` returns `file` mode only when a non-empty path is present, else `live`.
- **Allocation/threading**: pure value transform on the message thread; never called
  from the audio callback.

## Consumers

- The persistence unit (`workbench-settings`) writes `serialize(cfg)` into the
  settings file and reads it back with `parse`.
- The unit test (`tests/core/workbench-settings-test.cpp`) asserts the round-trip and
  the safe-default-on-garbage guarantees.

## Why this is the only contract

The feature exposes no API to other systems; it is a desktop-app adapter. The
device/MIDI/persistence surfaces are driven by JUCE's own components and the
`AudioDeviceManager` XML, which are not ours to re-specify. `SourceConfig` serde is
the single piece of bespoke, testable logic — so it is the single contract.
