### Missing-input-device fallback is not surfaced

Finding-ID: AUDIT-BARRAGE-codex-01
Status:     open
Severity:   high
Surface:    adapters/workbench/workbench-app.cpp:104-108, adapters/workbench/workbench-app.cpp:281-287

The startup warning path only remembers and compares `audioOutputDeviceName`. If the previously selected input device disappears but the output device remains the same, `setAudioChannels(2, 2, loaded.deviceState.get())` can fall back to another available input while `surfaceStartupIssues()` emits no message. That violates the US3 acceptance scenario for “a previously selected device” becoming unavailable, and it is especially visible for live input: the workbench may process the wrong physical input with no explanation.

Blast radius is high because this is a shipped correctness defect in the persistence/restore path a user will hit when USB or loopback inputs change. A reasonable fix is to capture the saved input device name as well, compare it against the active input device after restore, and include it in the same startup warning path instead of checking output only.
