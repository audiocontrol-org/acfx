### Checked-off tasks still leave required acceptance unchecked

Finding-ID: AUDIT-BARRAGE-codex-01  
Status:     open  
Severity:   high  
Surface:    specs/svf-vertical-slice/tasks.md:88,107,127,141,146-163

The task list marks T027, T031, T035, and T038 as complete even though the same file says their interactive/hardware acceptance remains unchecked in “Manual acceptance.” This is not just wording drift: the original task text for T027/T031/T035 was the independent acceptance run, but the completed entries now substitute build-only or host-only verification and move the actual acceptance criteria to unchecked bullets at lines 155-163.

The blast radius is high because a downstream unattended consumer reading `[X]` on the story acceptance tasks and Phase 6 invariant will conclude the feature is done, including DAW-host loading and MCU build/link/flash behavior. The artifact itself does not encode those as incomplete tasks in the main dependency/completion flow; it places them in a separate unchecked section after all story tasks are marked complete. A reasonable fix would keep the automated build checks as completed subtasks, but leave the acceptance tasks themselves unchecked until the Scenario B/C/D manual runs actually pass, or split each original task into explicit automated and operator-run task IDs so completion state cannot be misread.
