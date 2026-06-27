### Completion ledger marks acceptance tasks done while their own acceptance remains unverified

Finding-ID: AUDIT-BARRAGE-codex-01
Status:     open
Severity:   high
Surface:    specs/svf-vertical-slice/tasks.md:88, specs/svf-vertical-slice/tasks.md:104

T027 and T031 are checked `[X]` as “Run quickstart Scenario B/C end-to-end and confirm all acceptance scenarios,” but both lines then state that the manual acceptance portions still need confirmation: US1 needs live sweep, MIDI CC, and A/B listening; US2 needs DAW instantiation, cutoff automation, and audible parity. Those are not optional extras; they are the independent tests and acceptance scenarios in `spec.md` and `quickstart.md`.

The blast radius is high because this task ledger is an input to unattended downstream agents and release/governance checks. A consumer can reasonably read `[X]` plus the “independently shippable” checkpoints as completion, even though the same line says the actual end-to-end acceptance is still unconfirmed. A reasonable fix is to leave implementation/build tasks checked, but split the manual acceptance tasks into explicit unchecked/manual-verification items or mark the parent story checkpoint as partially verified rather than complete.

### Hardware story claims build-and-link completion despite the link being blocked

Finding-ID: AUDIT-BARRAGE-codex-02
Status:     open
Severity:   blocking
Surface:    specs/svf-vertical-slice/tasks.md:115-123

US3’s independent test is explicitly “`daisy` and `teensy` presets build & link” and T035 is checked as confirming linked artifacts, but the same T035 text says only cross-compilation was verified and that “full firmware ELF link” is blocked because the installed `arm-none-eabi-gcc` has no libstdc++. The following checkpoint then says “US3 done” and “build + link on both MCUs,” directly contradicting the verified evidence.

This is blocking by the provided rubric because acting on the ledger as written breaks the feature’s stated hardware goal: the cross-platform claim depends on clean MCU build and link, and the artifact says that link did not happen. The fix is to mark T035 and the US3 checkpoint incomplete or split them into checked “compile/no-JUCE graph gate” and unchecked “firmware link artifacts produced with proper toolchain” tasks, with the checkpoint wording matching the verified state.
