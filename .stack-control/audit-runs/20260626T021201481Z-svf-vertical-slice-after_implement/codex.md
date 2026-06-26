### processBlock constructs a `std::function` callback on the audio thread

Finding-ID: AUDIT-BARRAGE-codex-01
Status:     open
Severity:   high
Surface:    adapters/plugin/plugin-processor.cpp:28-33, adapters/plugin/plugin-parameters.h:21-27

`PluginProcessor::processBlock()` calls `parameters_.apply([this](ParamId id, float normalized) { ... });` on every audio callback, but `PluginParameters::apply` takes `const ApplyFn&`, where `ApplyFn` is `std::function<void(ParamId, float)>`. That means each block converts the lambda into a `std::function` object on the realtime path. Small-object optimization often avoids heap allocation for this exact lambda, but that is an implementation detail, not a contract; the code comment explicitly claims “Allocation-free” at `plugin-processor.cpp:28-30`.

The blast radius is high because this is plugin audio-thread code and the feature has explicit RT-safety goals. A downstream adopter can ship this and hit allocator or lock behavior in a DAW callback depending on standard library implementation or future callback shape. A reasonable fix is to make `apply` a templated callback method, or expose a non-allocating loop/API so `processBlock()` never constructs type-erased callable state.

### Discrete plugin parameters are translated through hand-rolled midpoint normalization

Finding-ID: AUDIT-BARRAGE-codex-02
Status:     open
Severity:   medium
Surface:    adapters/plugin/plugin-parameters.cpp:75-84

For `AudioParameterChoice`, `PluginParameters::apply()` maps the selected index to `(index + 0.5f) / count` before calling `setParameter`. That is a separate encoding from the explicit choice index the host parameter already owns. It relies on the effect’s discrete normalization logic interpreting midpoint bins exactly the same way, and it silently changes the value channel from “choice index” to “floating midpoint in an inferred bin.”

The blast radius is medium: for the current three-mode SVF this probably works if the core bins normalized values by floor/clamp, but it is fragile and will compound when another discrete parameter has different semantics or if the descriptor’s discrete mapping changes. A safer fix is to route discrete parameters through a single shared descriptor helper that converts an integer choice index to the normalized value the core expects, or add an explicit discrete-index setter at the host boundary.

### Discrete choice labels are hardcoded to SVF mode names instead of descriptor data

Finding-ID: AUDIT-BARRAGE-codex-03
Status:     open
Severity:   medium
Surface:    adapters/plugin/plugin-parameters.cpp:21-29, adapters/plugin/plugin-parameters.cpp:43-50

`PluginParameters::build()` claims to generate host automation from each `ParameterDescriptor`, but every discrete parameter label is produced by `modeName(i)`, which only knows `lowpass`, `highpass`, and `bandpass`. Any future discrete descriptor with a different meaning, or even an SVF descriptor with reordered/renamed modes, will produce a valid-looking but wrong host UI and automation surface.

The blast radius is medium because this does not necessarily break the present SVF slice, but it makes the “descriptor-generated” contract false in a way an unattended downstream adapter would copy. A reasonable fix is to carry discrete display names in `ParameterDescriptor` or provide an effect-owned metadata hook, then have the plugin consume that instead of embedding SVF-specific labels in the generic parameter adapter.
