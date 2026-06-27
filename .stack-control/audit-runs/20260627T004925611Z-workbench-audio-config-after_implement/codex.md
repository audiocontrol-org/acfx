### Malformed saved source settings silently fall back to live

Finding-ID: AUDIT-BARRAGE-codex-01
Status:     open
Severity:   medium
Surface:    adapters/workbench/workbench-persistence.cpp:36-43; adapters/workbench/workbench-settings.cpp:35-40

`WorkbenchPersistence::load()` only marks corruption for the device-state XML, then unconditionally assigns `out.source = parse(...)`. But `parse()` treats empty, garbage, unknown modes, and `file` with no path as `{ live, "" }` without reporting whether the persisted source value was malformed. That means a corrupted `sourceConfig` silently changes the user’s saved source selection to live input, and the destructor/save path can then overwrite the bad value with the fallback.

This matters because the feature explicitly surfaces corrupt settings rather than silently defaulting. The blast radius is medium: the app still starts, but downstream behavior contradicts the “surface startup problems” contract and can erase the user’s persisted source selection without telling them. A reasonable fix is to make source parsing return both value and validity, or let persistence validate the serialized shape before falling back, then set `LoadedSettings::corrupt` for malformed present values.

### Fresh installs still enable every MIDI input implicitly

Finding-ID: AUDIT-BARRAGE-codex-02
Status:     open
Severity:   medium
Surface:    adapters/workbench/workbench-app.cpp:117-128

The US4 path says MIDI input selection is explicit through the audio settings UI, but on first run the constructor enables every available MIDI input before registering the all-enabled-inputs callback. With the callback registered using an empty device id, any connected controller that sends CC 71 or 74 can immediately change parameters without the operator selecting that device.

The blast radius is medium: this does not break saved-state restore, but it preserves the previous broad “all controllers are active” behavior for fresh installs, which is exactly where explicit selection matters most. A reasonable fix is to leave MIDI inputs disabled by default on first run, or gate the “enable all” behavior behind an intentional default documented as part of US4 acceptance.
