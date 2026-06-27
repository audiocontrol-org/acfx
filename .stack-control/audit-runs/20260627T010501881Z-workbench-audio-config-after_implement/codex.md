### Completed checklist masks unexecuted manual acceptance

Finding-ID: AUDIT-BARRAGE-codex-01  
Status:     open  
Severity:   high  
Surface:    specs/workbench-audio-config/tasks.md:69, specs/workbench-audio-config/tasks.md:84, specs/workbench-audio-config/tasks.md:99, specs/workbench-audio-config/tasks.md:113, specs/workbench-audio-config/tasks.md:123

The task list marks US1/US2/US3/US4 and Scenario F acceptance items as `[X]` while the line text says those interactive checks are operator-owned and not actually completed in this implementation pass. This is especially load-bearing because `contracts/source-config.md` says most of the feature is validated through manual JUCE acceptance rather than unit tests, so marking these tasks complete changes the apparent verification state of the feature.

Blast radius is high: a downstream operator or unattended agent reading this artifact as written will reasonably conclude all acceptance criteria passed, including device routing, source switching, persistence restore, MIDI filtering, and rapid switch RT-safety. The reasonable fix is to keep the mechanical implementation tasks checked, but represent unexecuted manual acceptance as incomplete or as a separate clearly non-complete operator acceptance record, without marking the acceptance tasks done.
