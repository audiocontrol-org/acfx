### `check-portability.sh` — `grep` exit-code 2 treated as "OK" in checks 2, 3, and 4

Finding-ID: AUDIT-BARRAGE-claude-01
Status:     open
Severity:   high
Surface:    scripts/check-portability.sh:28–45

Checks 2, 3, and 4 all use `if grep -rEn 'pattern' <target-dir>` as the sole condition. `grep` exits 0 when a match is found, 1 when no match is found, and **2 when it encounters an error** (e.g., the target directory doesn't exist). In a bash `if` statement, any non-zero exit code evaluates as false, so both "no match (exit 1)" and "error / directory absent (exit 2)" take the else-branch and emit the "OK" message while leaving `fail` at 0. There is no `set -e` to abort on an unexpected error.

Consequence in CI: if `adapters/daisy`, `adapters/teensy`, or `core/` are missing or misspelled in the checkout (gitignored artifacts, wrong preset, typo), all three checks silently report green. The script is declared a CI quality gate; a false-pass here means a portability regression can merge undetected.

Fix: add explicit existence guards before each `grep` invocation, e.g. `if [ ! -d core/ ]; then note "  FAIL: core/ directory not found"; fail=1; elif grep -rEn '...' core/; then ...`. Alternatively, distinguish grep's exit codes explicitly: `grep ... || { ec=$?; [ "$ec" -ne 1 ] && { note "FAIL: grep error $ec"; fail=1; }; }`.

---

### `check-portability.sh` — `find` with `2>/dev/null` silently passes check 1 when source directories are absent

Finding-ID: AUDIT-BARRAGE-claude-02
Status:     open
Severity:   medium
Surface:    scripts/check-portability.sh:18–25

Check 1 feeds the while-loop via `find core host adapters tests -type f ... 2>/dev/null`. When none of these directories exist, `find` exits non-zero but the `2>/dev/null` suppresses the error message and the process-substitution produces no output — the while-loop body never executes. The trailing guard `[ "$fail" -eq 0 ] && note "  OK: all source files within budget"` then prints "OK" with zero files inspected.

This is the same false-pass class as finding -01 but for check 1. A CI run against an incomplete checkout would report "all source files within budget" after inspecting nothing. The fix is to verify that at least one of the searched directories exists (or at least one `.h`/`.cpp` file was found) before declaring success.

---

### `check-portability.sh` — Teensy-specific platform headers absent from check-2 denylist

Finding-ID: AUDIT-BARRAGE-claude-03
Status:     open
Severity:   low
Surface:    scripts/check-portability.sh:29

Check 2 denies `juce`, `libDaisy`, `daisy_seed`, `<Audio.h>`, and `<Arduino.h>` from `core/`, but the Teensy SDK ships headers that are equally platform-specific: `AudioStream.h` (Teensy Audio Library), `WProgram.h` (Arduino compatibility shim), `imxrt.h` / `kinetis.h` (Cortex-M MCU headers), and `usb_midi.h`. If a developer accidentally includes any of these in `core/`, check 2 will not flag it. The stated goal of the check is "core is platform-independent"; an incomplete denylist leaves the Teensy surface uncovered. Low severity because Teensy adapter code is in its own directory and the accidental-inclusion scenario requires deliberate action, but the gap is real.

---

### `tasks.md` — T027, T031, T035 marked `[X]` (complete) despite documented outstanding manual-acceptance portions

Finding-ID: AUDIT-BARRAGE-claude-04
Status:     open
Severity:   medium
Surface:    specs/svf-vertical-slice/tasks.md:84, 107, 131

Three acceptance tasks are marked `[X]` in the checkbox but their task bodies contain explicit "manual checkpoint … to be confirmed by the operator" language:
- T027: "live sweep, MIDI CC, and dry/processed A/B listening — to be confirmed by the operator on a machine with audio I/O."
- T031: "in-DAW instantiation, automation, and audible parity with the workbench — to be confirmed by the operator in a plugin host."
- T035: "full firmware ELF **link** + flashing is the on-hardware checkpoint (T035), blocked here by a C-only `arm-none-eabi-gcc` (no libstdc++)."

The `[X]` convention in Markdown task lists conventionally means "done." An agent (or operator) doing a quick status sweep reads `[X]` and concludes these are fully verified. The inline caveats are long and easy to miss. Blast radius: any downstream work gated on "US1/US2/US3 acceptance complete" — or any future audit pass asking whether this feature shipped — would start from a false premise. A `[~]` or custom marker (e.g., `[B]` for "blocked") paired with a dedicated "Partial completions" section would make the outstanding state unambiguous.

---

### `no-allocation-test.cpp` — `EffectNode` parameter-setting path not exercised in the RT-safety test

Finding-ID: AUDIT-BARRAGE-claude-05
Status:     open
Severity:   low
Surface:    tests/core/no-allocation-test.cpp:43–56

The first `TEST_CASE` (lines 21–38) explicitly tests both `process()` and `setParameter()` under the sentinel. The second `TEST_CASE` for `EffectNode<SvfEffect>` (lines 42–56) calls only `node.processBlock(block)` inside the guarded region and exercises no parameter-setting path. `EffectNode` is the host-boundary wrapper and presumably owns a thread-safe parameter-passing mechanism (lock-free queue or similar). If that mechanism allocates during the parameter-write path, the current test would not catch it. The RT-safety claim ("≤1 vcall/block", "allocation-free") should extend to every callable method in the audio-callback surface, not just `processBlock`.

---

### `check-portability.sh` — `#elif` platform forks not caught by check-4 regex

Finding-ID: AUDIT-BARRAGE-claude-06
Status:     open
Severity:   low
Surface:    scripts/check-portability.sh:48

The regex `#if(def)?.*(JUCE|DAISY|TEENSY|__arm__|DESKTOP)` matches `#if`, `#ifdef`, and (via prefix matching) `#ifndef` variants. It does NOT match `#elif defined(JUCE_...)` or `#elif JUCE_...` because the pattern requires `#if` at the start of the token. A platform fork written as:

```cpp
#ifdef __clang__
  // ...
#elif defined(JUCE_MODULE_AVAILABLE)
  // platform-specific code that slips through
#endif
```

would not be flagged. In practice this requires a prior `#if` that IS caught, so the likelihood of a completely undetected fork is low. But the invariant the check claims to enforce ("no per-target `#ifdef` forks of the effect source") is stated absolutely; a partial match for `#elif` variants weakens the guarantee. Extending the pattern to `(#if(def|ndef)?|#elif)[[:space:]]`.