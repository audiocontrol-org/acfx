### CI comment contains a prohibited deferred-work marker

Finding-ID: AUDIT-BARRAGE-codex-01
Status:     open
Severity:   low
Surface:    .github/workflows/ci.yml:3-7

The CI header says the hardware preset gate exists only when ARM toolchains are available, then points to later work instead of encoding an invariant the workflow enforces. This is specifically the kind of operator-discipline trap the audit prompt asks reviewers to surface: a reader can treat the missing Daisy/Teensy CI coverage as intentionally handled, while the workflow only runs host tests, portability checks, and desktop builds.

Blast radius is low because the actual CI jobs are visible below and the README separately describes hardware compilation as a manual/toolchain-dependent step, so this does not directly break the shipped slice. A reasonable fix would replace the later-work marker with a concrete current-state statement, or add an explicit skipped/manual hardware gate entry that names the invariant and required toolchain condition.

### Plugin discrete parameter labels are hardcoded to SVF mode names

Finding-ID: AUDIT-BARRAGE-codex-02
Status:     open
Severity:   medium
Surface:    adapters/plugin/plugin-parameters.cpp:24-33, adapters/plugin/plugin-parameters.cpp:49-55

`PluginParameters::build()` claims to generate host automation from `ParameterDescriptor`, but discrete choices are named by the local `modeName()` switch. That works for the current SVF `mode` parameter only because its indices happen to be lowpass/highpass/bandpass; any other discrete descriptor added to the same generated path would silently expose filter-mode labels in the DAW UI and automation list.

Blast radius is medium: the current vertical slice is not broken, but the adapter violates the declared “one parameter declaration drives every adapter” boundary and will compound as soon as another discrete control is introduced. A reasonable fix would move discrete choice labels into the descriptor model, or use neutral index labels consistently until the descriptor can carry names.
