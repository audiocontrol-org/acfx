### T035 is marked complete while the required MCU link is explicitly unverified

Finding-ID: AUDIT-BARRAGE-codex-01
Status:     open
Severity:   high
Surface:    specs/svf-vertical-slice/tasks.md:116-132

Phase 5 still defines the user-story goal and independent test as “compiles and links” for Daisy/Teensy, and says Scenario D should “build & link” the same core. But T035 is checked off while its new text narrows the completed work to compile-only verification and explicitly says the “full firmware ELF link + flashing” is an unchecked operator/on-hardware checkpoint blocked by the current toolchain.

The blast radius is high because a downstream consumer or unattended agent reading task completion mechanically will conclude US3’s build-and-link acceptance is done, even though the file itself says the link has not been performed. A reasonable fix is to split T035 into an automated compile/portability task that can be checked and a separate unchecked link/flash acceptance task, or leave T035 unchecked until the stated Scenario D link criterion is satisfied.

### The no-allocation test only exercises cutoff parameter updates

Finding-ID: AUDIT-BARRAGE-codex-02
Status:     open
Severity:   medium
Surface:    tests/core/no-allocation-test.cpp:29-35

The first test claims audio-thread parameter changes are allocation-free, but inside the measured region it only alternates `SvfEffect::kCutoff`. That means the resonance and mode update paths are never driven under the allocation sentinel, even though `process()` applies each dirty parameter through separate branches.

The blast radius is medium: the current implementation may still be allocation-free, but the test does not enforce the full real-time contract it advertises. A future change could add allocation in the resonance or mode path and this test would stay green. A reasonable fix is to iterate all parameter IDs from `SvfEffect::parameters()` or explicitly cover cutoff, resonance, and mode in the measured region.
