### Hardware acceptance is marked complete while the required build/link remains unchecked

Finding-ID: AUDIT-BARRAGE-codex-01
Status:     open
Severity:   high
Surface:    specs/svf-vertical-slice/tasks.md:124-133, specs/svf-vertical-slice/tasks.md:160-163

T035 is checked off as complete on lines 126-127, but the task’s original acceptance surface is “build `daisy` + `teensy` presets” and “confirm linked artifacts”; the edited text explicitly says the actual `arm-none-eabi` Cortex-M7 compile and firmware ELF link were **not verified**. Lines 160-163 then restate that the actual on-target compile, build, link, flash, and listen run is still an unchecked manual checkpoint.

The blast radius is high because a downstream unattended consumer reading `[X] T035` will treat US3’s hardware build/link acceptance as done, even though the same artifact says that core part of Scenario D has not happened. A reasonable fix would keep the host dual-standard/no-JUCE verification as a completed subtask, but leave the hardware compile/link acceptance unchecked or split T035 into completed automated proof and unchecked hardware acceptance without reusing the completed T035 identity for both.
