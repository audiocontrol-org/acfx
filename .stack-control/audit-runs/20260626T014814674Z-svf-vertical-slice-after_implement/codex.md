### `std::function` keeps the plugin audio path from being RT-safe

Finding-ID: AUDIT-BARRAGE-codex-01
Status:     open
Severity:   high
Surface:    adapters/plugin/plugin-parameters.h:21-27, adapters/plugin/plugin-processor.cpp:28-32

`PluginParameters::apply` is declared as `apply(const ApplyFn& fn)`, where `ApplyFn` is `std::function<void(ParamId, float)>`, and `processBlock` passes a capturing lambda into it on every audio callback. That requires constructing a `std::function` temporary at the call site before `apply` runs. Even if this particular lambda often fits a small-object buffer, the standard does not guarantee allocation-free construction or invocation behavior, so the comment in `plugin-processor.cpp:28-30` claiming “Allocation-free” is stronger than the code can support.

The blast radius is high because this is directly in the DAW audio callback. A downstream adopter relying on the stated RT-safety contract could ship a plugin with host-dependent heap traffic or non-deterministic dispatch in `processBlock`. A reasonable fix is to make `apply` a templated callable method, or otherwise avoid `std::function` on the audio thread entirely.

### Plugin state persistence is silently absent for DAW-hosted parameters

Finding-ID: AUDIT-BARRAGE-codex-02
Status:     open
Severity:   medium
Surface:    adapters/plugin/plugin-processor.h:42-45

`getStateInformation` and `setStateInformation` are implemented as no-ops while the plugin exposes host-automation parameters. The comment says state persistence is “out of scope for this milestone,” but the code still presents a normal DAW plugin surface where users expect saved projects and presets to restore parameter values. Hosts do not universally persist raw `AudioProcessorParameter` values unless the plugin serializes its state, so sessions can reopen with defaults despite automation controls appearing functional.

The blast radius is medium: this does not break live audio processing, but it compounds into DAW integration data loss and gives downstream consumers a misleadingly complete plugin. A reasonable fix is to serialize the generated parameter values by stable parameter ID and restore them in `setStateInformation`, or make the limitation explicit outside the runtime implementation surface if this adapter is intentionally non-persistent.

### Teensy adapter only routes the left channel despite describing line-in to line-out behavior

Finding-ID: AUDIT-BARRAGE-codex-03
Status:     open
Severity:   medium
Surface:    adapters/teensy/teensy-main.cpp:52-61

The Teensy audio graph declares “line in -> SVF -> line out,” but only creates one input connection and one output connection: `audioIn` channel 0 to `svfNode`, then `svfNode` to `audioOut` channel 0. On the SGTL5000/I2S path this leaves the right input unprocessed and the right output unconnected or silent, while the comment reads like the whole line I/O path is covered.

The blast radius is medium because an adopter wiring a normal stereo Audio Shield setup would hit asymmetric output immediately, even though the adapter appears to be the hardware proof point for the shared core. A reasonable fix is either to make the mono behavior explicit in comments/docs and hardware expectations, or provide a second SVF node/connection path for channel 1.
