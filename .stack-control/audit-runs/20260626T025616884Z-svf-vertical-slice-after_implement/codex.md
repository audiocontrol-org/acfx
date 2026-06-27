### Manual acceptance is split out after marking story acceptance tasks complete

Finding-ID: AUDIT-BARRAGE-codex-01
Status:     open
Severity:   medium
Surface:    specs/svf-vertical-slice/tasks.md:82-151

The task ledger marks T027, T031, and T035 as `[X]`, but each rewritten task now excludes the actual independent acceptance scenario it originally represented and moves that work into unchecked “Manual acceptance” items. For example, T027 is marked complete while the live sweep / MIDI / A-B listening run is explicitly “pending” in the US1 checkpoint and again unchecked in Manual acceptance. The same pattern appears for plugin host validation in T031 and on-target MCU build/link/flash in T035.

This matters because `tasks.md` is a governance artifact an unattended consumer may use to decide whether the feature is complete. The intended reading is recoverable because the manual section is explicit, so this is not blocking, but the `[X]` story tasks create a plausible wrong completion signal. A safer shape would keep the original acceptance tasks unchecked or split them into separate automated `[X]` and manual `[ ]` task IDs so completion state is unambiguous.
