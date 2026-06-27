### Completed tasks still contain pending or blocked acceptance

Finding-ID: AUDIT-BARRAGE-codex-01  
Status:     open  
Severity:   high  
Surface:    specs/svf-vertical-slice/tasks.md:88, specs/svf-vertical-slice/tasks.md:107, specs/svf-vertical-slice/tasks.md:127

`T027`, `T031`, and `T035` are marked `[X]`, but their own text says parts of the acceptance remain pending or blocked: US1 still needs live sweep/MIDI/A-B listening, US2 still needs DAW instantiation/automation/parity, and US3 says the firmware ELF link is blocked by a C-only `arm-none-eabi-gcc`. This contradicts the task-list completion signal: an unattended downstream agent or release gate will read `[X]` as complete and close the feature despite acceptance gaps.

The blast radius is high because these are the user-story acceptance tasks, not cosmetic notes. A reasonable correction is to split each item into completed automated/build verification versus explicit unchecked manual/hardware acceptance tasks, or leave the parent acceptance task unchecked until the stated independent test is actually satisfied.

### Portability gate can pass dependency regressions by grepping text instead of the build graph

Finding-ID: AUDIT-BARRAGE-codex-02  
Status:     open  
Severity:   medium  
Surface:    scripts/check-portability.sh:36-56

The script claims to enforce “No JUCE / ProcessorNode in the MCU dependency surface” and “every adapter links the same `acfx_core`,” but it only greps source trees and CMake files. Line 37 searches `adapters/daisy` and `adapters/teensy` text for `juce|processor-node`, which misses transitive CMake dependencies or generated link interfaces. Lines 51-56 accept any occurrence of `acfx_core` in an adapter `CMakeLists.txt`, including comments or dead conditional branches, rather than querying target link libraries or building the presets.

The blast radius is medium: this is a quality gate, so a false green compounds over time and can let the exact portability invariant regress while CI still reports success. A stronger fix would make the gate configure the relevant presets and inspect CMake target/link metadata or generated dependency graphs, with text grep retained only as an additional source-level lint.
