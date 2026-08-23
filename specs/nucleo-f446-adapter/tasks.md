---

description: "Task list for the NUCLEO-F446RE adapter with USB audio I/O"
---

> ‼ **acfx COMMANDMENTS — non-negotiable** ‼
> **1. COMMIT AND PUSH EARLY AND OFTEN** — version control is a distributed, journaled
> filesystem that safeguards your work, **NOT a sacred rite reserved for the blessed.**
> Small atomic commits, pushed promptly; never hoard unpushed work.
> **2. NO GIT HOOKS, EVER** — this repo uses zero git hooks; none exist, none get added.
> **3. DESCRIPTIVE NAMES, NEVER NUMERIC PREFIXES** — names carry information; fake sequence
> numbers (`001-`) imply false order and false precision (datestamps excepted).
> **4. ALL UI/UX WORK GOES THROUGH `/frontend-design`** — no exceptions, no offroading; every
> user-facing visual/interaction decision routes through the frontend-design skill.
> **5. SCOPE IS THE OPERATOR'S CALL** — never cut/defer/drop scope on "YAGNI" or "simplicity";
> when scope is open, present options and ASK. The operator decides scope, not the agent.
> (acfx Constitution, Principles I–V — `.specify/memory/constitution.md`.)

# Tasks: NUCLEO-F446RE adapter with USB audio I/O

**Input**: Design documents from `specs/nucleo-f446-adapter/`

**Prerequisites**: [plan.md](./plan.md), [spec.md](./spec.md) (73 FRs, 10 user stories),
[research.md](./research.md), [data-model.md](./data-model.md),
[contracts/nucleo-support.md](./contracts/nucleo-support.md), [quickstart.md](./quickstart.md)

**Tests**: **Required.** The spec mandates three verification layers (**D18**, FR-048–FR-050):
CI cross-compile+link, host doctest over `acfx_nucleo_support`, and a hardware-in-the-loop
harness. Host tests are written **before** their implementation throughout.

**Organization**: Grouped by user story so each is independently implementable and testable.

## Format: `[ID] [P?] [Story] [tier:<label>] Description`

- **[P]**: Can run in parallel (different files, no dependencies on incomplete tasks)
- **[Story]**: Which user story the task serves (US1–US10)
- **[tier:...]**: Model tier — `fast` (mechanical/RED-test/doc), `balanced` (standard
  implementation), `powerful` (cross-cutting, architectural, high blast radius)

## ⚠ Execution-order note that priority alone does not convey

**US8 (fail-loud clock) must be implemented before US4's clock-validation task**, despite its
lower priority. FR-015c requires the LED indicator to exist *before* clock validation runs —
without a locked PLL there is no USB to report a fault over, so the indicator is the only
channel. Phases are listed in priority order below; the Dependencies section carries the real
execution graph.

---

## Phase 1: Setup (Shared Infrastructure)

**Purpose**: The build surface the rest of the work hangs off. Plan Phase A.

- [ ] T001 [tier:fast] Create the adapter directory skeleton `adapters/nucleo/` with `support/` and `startup/` subdirectories per plan.md § Source Code
- [ ] T002 [tier:balanced] Add `cmake/toolchains/nucleo-f446.cmake` targeting Cortex-M4 with `-mfpu=fpv4-sp-d16 -mfloat-abi=hard -fno-exceptions -fno-rtti`, mirroring `cmake/toolchains/daisy.cmake` (FR-006)
- [ ] T003 [tier:balanced] Port the libstdc++ probe from `cmake/toolchains/daisy.cmake` into the Nucleo toolchain so a C-only `arm-none-eabi-gcc` fails configuration with a message naming the missing component (FR-007)
- [ ] T004 [tier:balanced] Add `ACFX_BUILD_NUCLEO` option to `CMakeLists.txt` and a `nucleo` configure/build preset to `CMakePresets.json`, mirroring the `daisy` preset
- [ ] T005 [tier:balanced] Pin TinyUSB `0.21.0`, `cmsis_device_f4`, and CMSIS core as `DOWNLOAD_ONLY` CPM packages in `cmake/dependencies.cmake` under `if(ACFX_BUILD_NUCLEO)`, with **real refs captured from the upstream repositories and verified by an actual fetch** — never a fabricated version number (FR-010, FR-011, research R12)
- [ ] T006 [tier:fast] Record the new pins and their fetch-verification status in the comment block at the top of `cmake/dependencies.cmake`, matching the file's existing discipline

---

## Phase 2: Foundational (Blocking Prerequisites)

**Purpose**: The build-graph decisions every later phase depends on.

**⚠️ CRITICAL**: No user story work can begin until this phase is complete.

- [ ] T007 [tier:powerful] Declare `acfx_nucleo_support` as an **unconditional** top-level `INTERFACE` target in `CMakeLists.txt`, shaped like `acfx_core`/`acfx_analysis` — **NOT** gated behind `ACFX_BUILD_NUCLEO`. Research R8: the host suite builds under the `test` preset with no toolchain, so a gated target would be invisible to it and would silently reproduce the untested-glue blind spot **D1** exists to close (FR-002, FR-004a)
- [ ] T008 [tier:balanced] Add `acfx_add_effect_nucleo` to `cmake/acfx-effect-targets.cmake`, mirroring `acfx_add_effect_daisy`: one executable per effect, `_acfx_inject_effect` for `ACFX_EFFECT_TYPE`/`ACFX_EFFECT_HEADER`, linker script, `--gc-sections`, `.elf` suffix (FR-008, FR-009)
- [ ] T009 [tier:fast] Wire the four `tests/core/nucleo-*-test.cpp` sources into the `acfx_core_tests` target in `tests/CMakeLists.txt` and link `acfx_nucleo_support`

**Checkpoint**: The build graph is correct — host tests can see the support library, and firmware targets can be declared.

---

## Phase 3: User Story 1 — Cross-compile an effect to a firmware image (Priority: P1) 🎯 MVP

**Goal**: Any acfx effect builds into a standalone NUCLEO-F446RE firmware binary, and a toolchain that cannot produce one fails loud.

**Independent test**: Configure with the Nucleo toolchain and build; every declared firmware target links. A C-only toolchain fails configuration with a descriptive message.

- [ ] T010 [P] [US1] [tier:balanced] Write the linker script `adapters/nucleo/startup/nucleo-f446.ld` for STM32F446RE (512 KB flash at 0x8000000, 128 KB SRAM)
- [ ] T011 [US1] [tier:balanced] Generate the interrupt vector table in `adapters/nucleo/startup/vector-table.cpp` **from the CMSIS `IRQn_Type` enum**, so its length is structural rather than maintained by hand and covers `OTG_FS_IRQn` (= 67). A core-exceptions-only table sends the NVIC past the end of the array on the first USB interrupt (FR-012, **D13**)
- [ ] T012 [US1] [tier:balanced] Define `SystemCoreClock` in `adapters/nucleo/nucleo-main.cpp` with the true configured frequency; ST's `system_stm32f4xx.c` is not compiled, and TinyUSB derives PHY turnaround timing from this value — a wrong one degrades timing *silently* (FR-013, **D14**)
- [ ] T013 [US1] [tier:balanced] Create `adapters/nucleo/CMakeLists.txt` declaring firmware targets via `acfx_add_effect_nucleo` for the same two effects the Daisy adapter builds (`acfx::SvfEffect`, `acfx::ModulatedDelayEffect`)
- [ ] T014 [US1] [tier:fast] Add the `nucleo` preset to the CI build matrix so cross-compile+link is enforced on every change (FR-048, verification layer 1)
- [ ] T015 [US1] [tier:fast] Verify US1 end to end: `cmake --preset nucleo && cmake --build --preset nucleo` links one `.elf` per effect; a C-only toolchain on `PATH` fails configuration with the libstdc++ message (quickstart § 2)

**Checkpoint**: Firmware images build and link. CI gate active.

---

## Phase 4: User Story 2 — Convert between USB and effect sample formats (Priority: P1)

**Goal**: Interleaved 16-bit USB payloads convert to non-interleaved float channel buffers and back, for any payload the transport can deliver, without allocating.

**Independent test**: Host doctest round-trips known int16 buffers and asserts exact recovery, clamping, exact frame counts across 0–49, and no allocation.

- [ ] T016 [P] [US2] [tier:fast] Write RED tests in `tests/core/nucleo-sample-format-test.cpp` for contract SF1 (round-trip exactness over the representable range), SF2 (**clamping, not wrapping**, for floats outside [-1.0, 1.0)), SF2a (ties away from zero), SF3 (exact frame counts for every size 0–49 incl. zero-length), SF3a (torn payload truncates to whole frames and reports the remainder), SF4 (no allocation)
- [ ] T017 [US2] [tier:balanced] Implement `adapters/nucleo/support/sample-format.h` — `deinterleaveToFloat` and `interleaveToInt16`, scaling by 32768 both ways, round-to-nearest ties-away-from-zero, clamping to [-32768, 32767], `noexcept`, no allocation (FR-038, FR-038a, FR-028a)
- [ ] T018 [US2] [tier:fast] Confirm the allocation sentinel covers the conversion path and the tests from T016 pass green (SC-009, SC-010)

**Checkpoint**: Format conversion is correct and host-verified with no board attached.

---

## Phase 5: User Story 3 — Buffer audio with observable behaviour (Priority: P1)

**Goal**: A statically sized ring sits between the USB packet cadence and the DSP's independent fixed 48-frame block cadence, with every mismatch response defined, bounded, and counted.

**Independent test**: Host doctest drives scripted producer/consumer patterns — starvation, saturation, balanced flow, startup — and asserts both the emitted samples and the counter deltas.

- [ ] T019 [P] [US3] [tier:fast] Write RED tests in `tests/core/nucleo-audio-ring-test.cpp` for contract AR1 (bounded occupancy), AR2 (underflow → silence, reported), AR3 (overflow → drop **oldest**, reported), AR5 (no allocation, no locks), AR7 (**startup wait** — no block drawn until the fill target is reached), AR8 (**no re-centring**), AR9 (`reset()` clears and re-arms the wait without touching counters)
- [ ] T020 [P] [US3] [tier:fast] Write RED tests in `tests/core/nucleo-audio-ring-test.cpp` for `AudioTransportStats` contract TS1 (monotonic **modulo 2^32**, wrap not saturate), TS1a (**mutual exclusivity** — `inputStarved` vs `outputUnderruns` never both fire for the same silence), TS2 (`blocksProcessed` once per block), TS4 (zero blocks → rate 0, not a division by zero)
- [ ] T021 [US3] [tier:balanced] Implement `adapters/nucleo/support/transport-stats.h` — the eight-field `AudioTransportStats` (`truncatedFrames` included) and `errorRate()` (FR-033, FR-034, FR-034a)
- [ ] T022 [US3] [tier:powerful] Implement `adapters/nucleo/support/audio-ring.h` — statically sized SPSC ring, `CapacityFrames` a **template parameter with no default** (a default would be an invented number wearing the costume of a decision), defined underflow/overflow substitutions reported by return value, startup-fill wait, no re-centring, `noexcept`, no allocation, no locks (FR-030, FR-030a, FR-030b, FR-030c, FR-031, FR-032)
- [ ] T023 [US3] [tier:fast] Confirm the allocation sentinel covers the ring and that T019/T020 pass green (SC-009, SC-010)

**Checkpoint**: Buffering semantics are correct and host-verified. The numbers that make it *tuned* come later, in T060.

---

## Phase 6: User Story 4 — Enumerate driverless and stream duplex (Priority: P1)

**Goal**: The board appears as a stereo-in/stereo-out 48 kHz audio device, a MIDI port, and a serial port on one connection, with no driver installation.

**Independent test**: Attach the board and open it duplex at 48 kHz from a class-compliant client; confirm all three functions appear with no driver install.

> **Depends on US8's T047/T048** (LED indicator) — FR-015c requires the indicator to exist before clock validation runs.

- [ ] T024 [US4] [tier:powerful] Implement register-level clock bring-up in `adapters/nucleo/nucleo-main.cpp`: HSE **bypass** on the ST-Link 8 MHz MCO, PLL M=4 N=168 P=2 Q=7 → 168 MHz SYSCLK and **exactly** 48 MHz on PLLQ (FR-014, **D6**). Hardware-verified in the spike — cross-check `RCC_PLLCFGR`, `HSERDY`, `PLLRDY`, and `RCC_CFGR.SWS` against the recorded values
- [ ] T025 [US4] [tier:balanced] Configure PA11/PA12 alternate-function GPIO for OTG_FS in `adapters/nucleo/nucleo-main.cpp`
- [ ] T026 [US4] [tier:balanced] Write `adapters/nucleo/tusb_config.h` with `CFG_TUD_VBUS_DETECT_HW 0` (VBUS deliberately unwired — the board is ST-Link powered and feeding breakout VBUS into the 5 V rail puts two supplies in contention), plus audio, MIDI, and CDC class enables (FR-015 [VBUS], FR-022, **D17**)
- [ ] T027 [US4] [tier:powerful] Author `adapters/nucleo/usb-descriptors.h` and `usb-descriptors.cpp` locally from the `TUD_AUDIO20_DESC_*` primitives: a composite device grouping UAC2 audio (**stereo-in with stereo-out**, which no shipped template provides), USB MIDI, and CDC serial via IADs, advertising exactly one 48 kHz/16-bit/stereo format per direction (FR-018, FR-018a, FR-018b, FR-020, FR-021, **D5**, **D10**)
- [ ] T028 [US4] [tier:powerful] **Before writing any data-path code**, read the pinned TinyUSB 0.21.0 tree and record the actual alt-setting/close callback names and signatures, and confirm the polled `tud_audio_read()`/`tud_audio_write()` entry points. 0.21.0 **removed** `rx_done`/`tx_done`: code written against them links *silently* and leaves the audio path dead with no diagnostic (FR-023, research R1 — highest-cost trap in this feature)
- [ ] T029 [US4] [tier:powerful] Verify the OTG_FS **endpoint budget** closes across three functions (audio IN + OUT iso, MIDI bulk pair, CDC notify + bulk pair). If it does not, **surface it as a finding for the operator** — do not silently drop a function (research R2, spec § Assumptions)
- [ ] T030 [US4] [tier:balanced] Implement TinyUSB init, the `OTG_FS_IRQHandler`, and the `tud_task()` service loop in `adapters/nucleo/nucleo-main.cpp` (FR-046 — the ISR only enqueues)
- [ ] T031 [US4] [tier:fast] Verify US4 on hardware: the device enumerates with audio, MIDI, and serial functions, no driver installation, and opens duplex 2-in/2-out at 48 kHz (quickstart § 5, SC-001)

**Checkpoint**: The board is a driverless class-compliant audio device.

---

## Phase 7: User Story 5 — Process live host audio through an effect (Priority: P1)

**Goal**: Audio from the host passes through `process()` and returns transformed.

**Independent test**: Stream a known signal through a firmware whose effect has a deterministic transfer characteristic; the output matches the expected transformation.

- [ ] T032 [US5] [tier:powerful] Wire the polled OUT path in `adapters/nucleo/nucleo-main.cpp`: read the packet, truncate to whole frames and count the remainder, convert and de-interleave, write to the input ring — accepting **0–49 frames** with no code path assuming 48 (FR-028, FR-028a)
- [ ] T033 [US5] [tier:powerful] Assemble **fixed 48-frame** blocks from the ring, run `AppEffect::process()` in place on non-interleaved `float*`, and write results to the output ring; packet size must not propagate into block size (FR-030a, FR-036a, FR-037)
- [ ] T034 [US5] [tier:balanced] Prepare the effect with `acfx::ProcessContext{48000.0, 49, 2}` at startup — 49 is headroom for the largest deliverable payload, not the working block size (FR-036, **D15**)
- [ ] T035 [US5] [tier:balanced] Wire the polled IN path: read the output ring, convert and interleave with clamping, write via `tud_audio_write()` (FR-026, FR-038a)
- [ ] T036 [US5] [tier:balanced] Implement the DWT `CYCCNT` block timer (enable `TRCENA` then `CYCCNTENA`), convert cycles to microseconds against 168 MHz, and maintain `blocksProcessed` and `worstBlockMicros` (FR-034, research R6)
- [ ] T037 [US5] [tier:balanced] Make a dead timing source **fail loud**: if `CYCCNT` reads stuck-at-zero after enabling, surface it rather than reporting `worstBlockMicros = 0` — a zero meaning "not measured" is indistinguishable from one meaning "instantaneous" (FR-034b, I-TS4)
- [ ] T038 [US5] [tier:fast] Verify US5 on hardware: a known signal returns audibly transformed as the compiled-in effect predicts (quickstart § 6, SC-002)

**Checkpoint**: The board is an acfx target, not a USB passthrough.

---

## Phase 8: User Story 6 — Change parameters live over USB MIDI (Priority: P2)

**Goal**: A MIDI CC moves the intended effect parameter without audio interruption.

**Independent test**: Host doctest the parameter seam directly (write slots, walk dirty flags, assert exactly the changed parameters applied), plus a hardware confirmation that an incoming CC moves a parameter.

> **Open question 7 blocks completion.** The mapping *mechanism* is fully specified; the concrete CC convention is the operator's call and is needed before this story is done.

- [ ] T039 [P] [US6] [tier:fast] Write RED tests in `tests/core/nucleo-parameter-shadow-test.cpp` for contract PS1 (bounded at `N`), PS2 (**last-write-wins** — a burst leaves the final value, never an intermediate one), PS3 (**no cross-parameter eviction**), PS4 (exactly-once per dirty parameter, then flags cleared), PS5 (`N == 0` is a valid no-op), PS6 (no allocation)
- [ ] T040 [P] [US6] [tier:fast] Write RED tests in `tests/core/nucleo-midi-cc-map-test.cpp` for contract MC1 (unmapped CC ignored, no slot disturbed), MC2 (index beyond `paramCount` never returned), MC3 (pure/stateless)
- [ ] T041 [US6] [tier:balanced] Implement `adapters/nucleo/support/parameter-shadow.h` — per-`ParamId` value + dirty flag, bounded by construction at the effect's parameter count, `flush()` applying each dirty parameter exactly once then clearing (FR-041, FR-042, FR-043, **D25**)
- [ ] T042 [US6] [tier:balanced] Implement `adapters/nucleo/support/midi-cc-map.h` — table-driven CC → parameter index, bounded by `paramCount` so an out-of-range index can never reach `setParameter`. Structure the table so settling open question 7 is a **table edit, not a rewrite** (FR-045, research R9)
- [ ] T043 [US6] [tier:balanced] Wire USB MIDI reception into the map and shadow block, and call `shadow.flush(...)` once per audio block boundary in `adapters/nucleo/nucleo-main.cpp` (FR-039, FR-042)
- [ ] T044 [US6] [tier:fast] **Put open question 7 to the operator**: fixed CC numbers per parameter index? a learn mode? which channel? should it match the workbench's existing MIDI CC consumption so one mapping serves both? Record the answer and apply it to T042's table (spec § Open Questions 7)

**Checkpoint**: Parameters are controllable; the CC convention awaits an operator decision.

---

## Phase 9: User Story 7 — Capture-only operation (Priority: P2)

**Goal**: A host opening the mic interface alone gets a well-defined stream, not a hang or stale audio.

**Independent test**: Open the capture stream alone; the received stream is silence with `inputStarved` incrementing, and duplex resumes when playback is later opened.

- [ ] T045 [US7] [tier:balanced] Detect the capture-only alt-setting state (mic streaming, speaker zero-bandwidth) and emit silence on the IN endpoint, incrementing **`inputStarved`** — not `outputUnderruns`, which covers the different condition of an open playback stream with a momentarily empty ring (FR-029, FR-029a, **D22**)
- [ ] T046 [US7] [tier:fast] Verify US7 on hardware: capture-only yields counted silence, and opening playback afterwards resumes duplex with no restart (quickstart § 7, SC-005)

**Checkpoint**: A legal host configuration whose failure mode is a mystery hang is now defined.

---

## Phase 10: User Story 8 — Fail loudly rather than run on an inadequate clock (Priority: P2)

**Goal**: A clock that cannot be brought up halts the firmware observably instead of continuing on the internal oscillator.

**Independent test**: Force the lock check to fail; the firmware blinks LD2 and halts, never proceeding to USB init.

> **Implement T047/T048 before T024** — FR-015c requires the indicator to exist before clock validation runs.

- [ ] T047 [US8] [tier:balanced] Initialize the LD2 (PA5) GPIO in `adapters/nucleo/nucleo-main.cpp` **before** clock validation, accepting that it runs on the reset-default HSI and its cadence is therefore approximate — the pattern's shape, not its timing, carries the signal (FR-015c)
- [ ] T048 [US8] [tier:balanced] Implement the fatal-fault path: **three short pulses, long gap, repeating indefinitely**, then halt — distinguishable both from a dark board and from any normal-operation indication (FR-015a, FR-015b)
- [ ] T049 [US8] [tier:balanced] Make PLL-lock failure fatal: no fallback to the internal oscillator, no progression to USB initialization, under any configuration (FR-015, **D7**)
- [ ] T050 [US8] [tier:fast] Verify US8 on hardware: with the ST-Link cable disconnected the board blinks the fault pattern and does not enumerate; a dark LED is a **failure**, not an inconclusive result (quickstart § 4, SC-007)

**Checkpoint**: A failed board is distinguishable from an unpowered one by eye, with no debug probe.

---

## Phase 11: User Story 10 — Survive host-initiated USB lifecycle events (Priority: P2)

**Goal**: Suspend, resume, and bus reset leave the device streaming normally afterwards.

**Independent test**: Sleep and wake the host with the device streaming; separately force a bus reset. Audio resumes and the counters tell a coherent story across the event.

- [ ] T051 [US10] [tier:balanced] Handle bus **suspend** in `adapters/nucleo/nucleo-main.cpp`: stop producing and consuming audio, and do not spin waiting for packets that will not arrive (FR-051)
- [ ] T052 [US10] [tier:balanced] Handle **resume**: restart streaming with no power cycle and **without replaying** audio buffered before the suspend (FR-052)
- [ ] T053 [US10] [tier:balanced] Handle **bus reset / re-enumeration**: clear the rings via `reset()` and re-arm the startup-fill wait so the device restarts from a defined state rather than draining a stale partial ring — while leaving the counters untouched (FR-053, FR-054, contract AR9)
- [ ] T054 [US10] [tier:balanced] Ensure every alt-setting combination (both closed, playback only, capture only, both open) is handled and that moving between any two requires no power cycle (FR-055)
- [ ] T055 [US10] [tier:fast] Verify US10 on hardware: a host sleep/wake cycle and a forced bus reset each leave the device streaming, with counters readable across the event (quickstart § 7a, SC-013)

**Checkpoint**: An entire lifecycle class that was absent from the original spec is now covered.

---

## Phase 12: User Story 9 — Verify transport quality against a HIL harness (Priority: P3)

**Goal**: A developer with a board attached asserts against the transport's own counters rather than inferring glitches from signal correlation.

**Independent test**: Run the harness against an attached board; it reports the full counter set and passes or fails against the configured bar.

- [ ] T056 [US9] [tier:balanced] Emit `AudioTransportStats` over the CDC serial function as line-oriented `key=value` text, newline-terminated, one snapshot per line — non-blocking and non-allocating, and harmless when nothing has the port open (FR-033a, research R7)
- [ ] T057 [US9] [tier:balanced] Build the HIL harness under `tools/nucleo-hil/`, starting from the spike's `tools/loopback_test.py`: stream a known signal, read the counter set over CDC, and express error counts as a **rate against `blocksProcessed`** using deltas between snapshots rather than lifetime totals (FR-050, FR-034a, US9 AS2)
- [ ] T058 [US9] [tier:fast] Ensure the harness is **not** wired into the normal CI job — it requires a physical board (FR-050, US9 AS3)
- [ ] T059 [US9] [tier:fast] **Put open questions 2 and 6 to the operator**: what counter rate constitutes a failing build, which layer enforces it, where the harness lives, and how it is invoked (spec § Open Questions 2, 6)

**Checkpoint**: Transport quality is measurable rather than anecdotal.

---

## Phase 13: Measurement Pass & Polish

**Purpose**: Plan Phase H — the numbers that could not honestly exist before hardware — plus cross-cutting cleanup.

- [ ] T060 [tier:powerful] **Measure, do not pick**: derive ring capacity, water marks, and startup fill using research R5's procedure — instrument with a generous capacity, stream a sustained run, record occupancy min/max/distribution over `blocksProcessed` plus every counter, derive fill from the lower excursion and capacity from the upper, each with headroom justified by the measured spread, then re-run to confirm. The spike's ~0.2% figure came from a naive single buffer and **predicts nothing** about the tuned design (FR-035, **D23**)
- [ ] T061 [tier:balanced] Pin the measured values as `AudioRing` template arguments in `adapters/nucleo/nucleo-main.cpp` and record the measurement evidence that justifies each in `specs/nucleo-f446-adapter/research.md`
- [ ] T062 [tier:fast] Record `worstBlockMicros` for every shipped Nucleo firmware alongside its harness results (SC-011)
- [ ] T063 [tier:fast] Confirm every shipped source file is within the ~300–500 line budget and that `nucleo-main.cpp` holds no logic that could have lived in the host-testable support library (FR-005, FR-003, SC-012)
- [ ] T064 [tier:fast] Write `adapters/nucleo/README.md` documenting the two-cable requirement, the PA11/PA12 breakout wiring, and the trap that the Arduino-labelled `D11`/`D12` pins are **PA7/PA6** (FR-016, FR-017)
- [ ] T065 [tier:fast] File a backlog item for the repo-wide `cpm-package-lock.cmake` gap — it holds only its header comment while the real pinning mechanism is `GIT_TAG` in `dependencies.cmake`; pre-existing and not this feature's to fix (research R12)
- [ ] T066 [tier:fast] Confirm all 11 open questions remain recorded and none was silently resolved during implementation (spec § Open Questions)

---

## Dependencies

**Phase order**: Phase 1 → Phase 2 → user story phases → Phase 13.

**Real execution graph** (priority order alone does not convey this):

```text
Phase 1 (T001-T006)  →  Phase 2 (T007-T009)
                              │
        ┌─────────────────────┼──────────────────────┐
        │                     │                      │
   US1 (T010-T015)      US2 (T016-T018)        US3 (T019-T023)
   build surface        host-only              host-only
        │                     └──────────┬───────────┘
        │                                │
   US8 (T047-T049) ◄── LED before clock validation (FR-015c)
        │
   US4 (T024-T031)  ── enumeration
        │
   US5 (T032-T038)  ── audio path (needs US2 + US3 + US4)
        │
        ├── US6 (T039-T044)   parameters
        ├── US7 (T045-T046)   capture-only
        └── US10 (T051-T055)  lifecycle
                │
           US9 (T056-T059)    telemetry + harness
                │
           Phase 13 (T060-T066)  measurement — requires the harness
```

**Key dependency facts**

- **US2 and US3 need no hardware** and can proceed fully in parallel with US1. They hold most of the feature's testable substance.
- **US8's T047/T048 precede US4's T024** despite the lower priority (FR-015c).
- **T060 cannot start before US9** — the measurement needs the harness that reads the counters.
- **T028 gates all data-path work** in US4/US5: the TinyUSB API must be read off the pinned tree, never recalled.

## Parallel Execution Examples

**After Phase 2, three tracks run concurrently:**

```text
Track A (build):  T010 → T011 → T012 → T013 → T014 → T015
Track B (host):   T016 → T017 → T018
Track C (host):   T019 ∥ T020 → T021 → T022 → T023
```

**Within US6, the two RED-test tasks are independent:**

```text
T039 ∥ T040   (different test files, no shared state)
```

## Implementation Strategy

**MVP = US1.** A firmware image that builds and links is the smallest increment that delivers
value on its own: it proves the toolchain, the factory, the vector table, and the linker script,
and it turns on CI's cross-compile gate for everything that follows.

**Then the host-only pair, US2 + US3.** These need no board, carry the bulk of the correctness
burden, and are where **D1**'s decomposition actually pays: they close the untested-glue gap the
Daisy and Teensy adapters carry. Reaching a green `ctest --preset test` here means the risky
hardware work starts on a verified foundation.

**Then the hardware chain**, US8 → US4 → US5, which is where the board first becomes an audio
device. **Then the P2 stories** (US6, US7, US10), each independently demonstrable.

**Finally US9 and the measurement pass**, in that order — the harness has to exist before the
numbers it produces can be pinned.

## Task Summary

| Phase | Story | Tasks | Count |
|---|---|---|---|
| 1 | Setup | T001–T006 | 6 |
| 2 | Foundational | T007–T009 | 3 |
| 3 | US1 (P1) 🎯 MVP | T010–T015 | 6 |
| 4 | US2 (P1) | T016–T018 | 3 |
| 5 | US3 (P1) | T019–T023 | 5 |
| 6 | US4 (P1) | T024–T031 | 8 |
| 7 | US5 (P1) | T032–T038 | 7 |
| 8 | US6 (P2) | T039–T044 | 6 |
| 9 | US7 (P2) | T045–T046 | 2 |
| 10 | US8 (P2) | T047–T050 | 4 |
| 11 | US10 (P2) | T051–T055 | 5 |
| 12 | US9 (P3) | T056–T059 | 4 |
| 13 | Polish/measurement | T060–T066 | 7 |
| **Total** | | | **66** |

**Tier distribution**: `fast` 25, `balanced` 32, `powerful` 9. The `powerful` set is exactly the
work that is cross-cutting or silently-failing: the build-graph decision (T007), the locally
authored composite descriptor (T027), register-level clock bring-up (T024), the TinyUSB API
verification (T028), the endpoint budget (T029), the ring itself (T022), the two data-path
wiring tasks (T032, T033), and the measurement pass (T060).
