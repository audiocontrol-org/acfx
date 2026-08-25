---
description: "Task list for the synchronous, multi-format USB-audio transport (NUCLEO-F446RE)"
---

> ‼ **acfx COMMANDMENTS — non-negotiable** ‼
> **1. COMMIT AND PUSH EARLY AND OFTEN.** **2. NO GIT HOOKS, EVER.**
> **3. DESCRIPTIVE NAMES, NEVER NUMERIC PREFIXES** (datestamps excepted).
> **4. ALL UI/UX WORK GOES THROUGH `/frontend-design`.**
> **5. SCOPE IS THE OPERATOR'S CALL** — never cut/defer on YAGNI; present options and ASK.
> (acfx Constitution I–V.)

# Tasks: Synchronous, multi-format USB-audio transport (NUCLEO-F446RE)

**Input**: [spec.md](./spec.md) · [plan.md](./plan.md) · [research.md](./research.md) ·
[data-model.md](./data-model.md) · [contracts/usb-transport-contract.md](./contracts/usb-transport-contract.md) ·
[quickstart.md](./quickstart.md)

**Tests**: required (host doctest where host-testable + the objective USB packet capture, FR-013).
Host tests are written before their implementation.

## Format: `[ID] [P?] [Story] [tier:<label>] Description with file path`

- **[P]**: parallelizable (different files, no incomplete-task dependency).
- **[US n]**: the user story served (US1–US5). Setup/Foundational/Polish carry no story label.
- **[tier:...]**: model tier — `fast` (haiku: mechanical/RED-test/doc), `balanced` (sonnet:
  standard impl + integration/tests), `powerful` (opus: subtle USB correctness / high blast radius).

⚠ **Hardware-verification tasks** need a board + host and cannot run in CI or unattended; the
operator drives them. During `execute` they use the `- [~]` marker so the govern gate excludes them.

---

## Phase 1: Setup (Shared Infrastructure)

- [ ] T001 [tier:fast] Baseline checkpoint: confirm `cmake --preset test && cmake --build --preset test -j && ctest --preset test`, `cmake --build --preset nucleo`, and `./scripts/check-portability.sh` are all green on the current tree; record the current 16-bit `EP_IN_SZ_MAX` / `kAudioEpSize` / SW-buf / FIFO `static_assert` values from `adapters/nucleo/tusb_config.h` + `usb-audio-service.h` as the pre-change baseline (no file changes)

---

## Phase 2: Foundational (Blocking Prerequisites)

**⚠️ CRITICAL**: the verification instrument every streaming story checks against.

- [ ] T002 [tier:balanced] Build the host-side USB packet-capture harness under `scripts/nucleo-hil/` (macOS): capture the acfx IN endpoint's per-USB-frame packet sizes (usbmon/`ioreg`/`tshark` path) and emit the raw per-frame size series to a file for the evaluator (FR-013 instrument; the full metric evaluator is US5/T019). Excluded from CI (needs a board)

**Checkpoint**: the objective transport gate exists; user stories can be verified at the USB level.

---

## Phase 3: User Story 1 — Pitch-correct, noise-free at 48 kHz/16-bit (Priority: P1) 🎯 MVP

**Goal**: the device presents as SYNCHRONOUS and delivers a steady nominal 48-frames/SOF IN stream at the existing 48 kHz/16-bit, so the round-trip is pitch-correct and noise-free in an aggregate.

**Independent test**: packet capture shows steady 48/frame, zero ZLP/short, accumulated 48000/1000; a tone returns at the same pitch with no noise in a Logic aggregate.

- [ ] T003 [US1] [tier:powerful] Declare both iso endpoints **Synchronous** — set the IN (`0x81`) and OUT (`0x01`) endpoint `bmAttributes` sync bits from Async/Adaptive (`0x05`/`0x09`) to **`0x0D`** in `adapters/nucleo/usb-descriptors.cpp` (research §R2); confirm the nucleo cross-build links and the 16-bit FIFO `static_assert`s still hold
- [ ] T004 [US1] [tier:balanced] Enable `CFG_TUD_AUDIO_EP_IN_FLOW_CONTROL 1` in `adapters/nucleo/tusb_config.h` (research §R5) and confirm the IN FIFO stays adequately sized for 16-bit flow control (≥ 4·Navg = 4·192 = 768 B ≤ current 784 B — verify)
- [ ] T005 [US1] [tier:balanced] Remove the cold-FIFO greedy-drain (FR-007): in `adapters/nucleo/support/usb-in-path.h` / `usb-audio-service.h`, fix the startup path so the SOF-paced reads are served real audio rather than instantly emptying the output ring (research §R10, the R15 seed of the underruns)
- [ ] T006 [P] [US1] [tier:fast] Host test (RED→GREEN) in `tests/core/nucleo-sync-cadence-test.cpp`: model and assert the 48 kHz nominal-per-SOF expectation where host-testable (the 48000/1000 accumulation), registered in `tests/CMakeLists.txt`
- [ ] T007 [US1] [tier:balanced] ⚠ Hardware verification: flash, and via the T002 capture confirm at 48k/16-bit the IN endpoint holds steady 48-frame packets, **zero ZLP/short**, accumulated 48000/1000; and in a Logic aggregate a sustained tone returns **pitch-correct + noise-free** (SC-001/002/003)

**Checkpoint**: the core defect is fixed at 48 kHz/16-bit — a usable real-time effect (MVP).

---

## Phase 4: User Story 2 — Native 44.1 + 48 kHz (Priority: P1)

**Goal**: advertise + honour both rates natively; the fractional 44 100/1 000 cadence at 44.1 kHz.

**Independent test**: select 44.1 kHz; capture shows accumulated 44100/1000; live 48↔44.1 change without re-plug; pitch-correct.

- [ ] T008 [US2] [tier:powerful] Multi-rate clock in `adapters/nucleo/usb-descriptors.cpp` + `usb-audio-controls.cpp` (research §R3): clock attr `INT_FIX_CLK`→`INT_VAR_CLK`, freq control `AUDIO20_CTRL_R`→`AUDIO20_CTRL_RW`, RANGE returns two subranges {44100},{48000}, CUR returns `g_currentSampleRateHz`
- [ ] T009 [US2] [tier:powerful] Add the STRONG `extern "C"` `tud_audio_set_req_entity_cb` in `adapters/nucleo/usb-audio-controls.cpp` accepting SAM_FREQ ∈ {44100,48000} → store `g_currentSampleRateHz`, reject others (research §R9; MUST be a strong `.cpp` def — TASK-37 weak-callback trap)
- [ ] T010 [US2] [tier:balanced] Make `PrepareEffect()` re-invokable at the selected rate in `adapters/nucleo/nucleo-main.cpp` (today one-shot at 48k, `:440`) and add a poll-loop rate-change service step in `adapters/nucleo/usb-audio-service.h` that re-prepares the effect + resets the rings off EP0 context (FR-006; mirrors `ServiceUsbLifecycle`)
- [ ] T011 [P] [US2] [tier:fast] Host test in `tests/core/nucleo-rate-select-test.cpp`: the set-frequency logic accepts {44100,48000} and rejects others; the rate-change flag is set + consumed correctly
- [ ] T012 [US2] [tier:balanced] ⚠ Hardware verification: select 44.1 kHz — capture shows accumulated **44100/1000** (44/45 scheduled) with zero ZLP/short, pitch-correct + noise-free; a **live 48↔44.1 change** streams through without a re-plug (SC-005)

---

## Phase 5: User Story 3 — 16 + packed-24-bit (Priority: P2)

**Goal**: advertise + honour 16-bit and packed-24-bit; resolve the FR-014 feasibility gate.

**Independent test**: select 24-bit at each rate; capture + Logic show correct, noise-free streaming at 24-bit resolution.

- [ ] T013 [P] [US3] [tier:balanced] Packed-24 (3-byte, signed LE) wire↔float conversion in `adapters/nucleo/support/sample-format.h` alongside the int16 path (research §R6); host test (RED→GREEN) in `tests/core/nucleo-packed24-test.cpp` round-trips within 24-bit resolution
- [ ] T014 [US3] [tier:powerful] Add alt-2 (`bSubslotSize=3`, `bBitResolution=24`) to each AudioStreaming interface in `adapters/nucleo/usb-descriptors.cpp` (research §R4); resize `EP_IN_SZ_MAX`/`kAudioEpSize`/SW-buf for 24-bit (Navg 288 → IN SW-buf ≥ 1152 B) in `adapters/nucleo/tusb_config.h`; extend the strong `tud_audio_set_itf_cb` to record the selected format (pcm16/pcm24)
- [ ] T015 [US3] [tier:powerful] **FR-014 feasibility gate** — re-verify the OTG-FS FIFO-RAM budget against the ACTUAL resized 24-bit constants (research §R7: ≈20/320 words free at 48 kHz) and update the size `static_assert`s in `adapters/nucleo/usb-audio-service.h` + `usb-descriptors.h`. **If it overruns, STOP and surface the fallback table (24-bit-at-44.1-only / 16-bit-only) to the operator — do NOT cut silently** (Constitution V)
- [ ] T016 [US3] [tier:balanced] Make `adapters/nucleo/support/usb-out-path.h` and `usb-in-path.h` format-aware (select the 16/24 converter by the recorded format); host test both depths in `tests/core/`
- [ ] T017 [US3] [tier:balanced] ⚠ Hardware verification: select 24-bit at 44.1 and 48 kHz — capture + Logic aggregate confirm correct, noise-free streaming; a live 16↔24 format change without re-plug (SC-005)

---

## Phase 6: User Story 4 — Low, measured latency (Priority: P2)

**Goal**: shrink the rings to the measured minimum and pin the latency numbers.

**Independent test**: measured round-trip is a small bounded value (not ~0.5 s); pinned values recorded.

- [ ] T018 [US4] [tier:balanced] Add ring **occupancy instrumentation** (min/max) to `adapters/nucleo/support/audio-ring.h` / `usb-audio-service.h` — the missing R15 measurement gap (FR-008)
- [ ] T019 [US4] [tier:powerful] ⚠ Measure across all 4 rate×depth combos + a live rate/format change via the T059 HIL harness; **derive and PIN** ring capacity / startup-fill / water-range and round-trip latency (frames + ms) — reduce the rings from the 1024/98 placeholder to the measured minimum in `adapters/nucleo/usb-audio-service.h`, recording the evidence in `specs/synchronous-usb-audio-transport/research.md` (FR-008/SC-004; high blast radius on ring sizing)
- [ ] T020 [US4] [tier:fast] Investigate + document the UAC2 latency mechanism (research §R8): if a host-consumed mechanism is confirmed, expose the device latency (NOT `bLockDelay`); otherwise document the measured round-trip latency and that host PDC is unverified (FR-009). Record in research.md / `adapters/nucleo/README.md`

---

## Phase 7: User Story 5 — Objective verification guard (Priority: P3)

**Goal**: the packet capture is a full-metric, durable regression guard.

**Independent test**: the evaluator reports the full metric set and PASS/FAILs on zero-ZLP + accumulated-rate tracking.

- [ ] T021 [US5] [tier:balanced] Complete the packet-capture **evaluator** (`scripts/nucleo-hil/`): report selected rate, subslot size, USB/audio frame totals, size histogram, ZLP + non-nominal counts, effective frames/second; PASS = zero ZLP/short in steady state AND accumulated frames tracking the exact SOF-derived schedule (FR-013). Add fixtures + a host self-test of the pure evaluator where possible
- [ ] T022 [US5] [tier:fast] Confirm the capture harness is excluded from CI and documented (no `.github` / ctest reference), mirroring the base HIL harness (US9 pattern)

---

## Phase 8: Polish & Cross-Cutting

- [ ] T023 [tier:fast] Objdump-verify every new/changed TinyUSB callback (`tud_audio_set_req_entity_cb`, `tud_audio_set_itf_cb`) is a STRONG symbol, not the weak default (TASK-37 / [[tinyusb-weak-callback-linkage-trap]]) — a clean link is not a boot check
- [ ] T024 [tier:fast] Confirm `./scripts/check-portability.sh` passes (core acquired no adapter/USB dep; every changed file ≤ 500 lines) and the nucleo cross-build is clean
- [ ] T025 [tier:fast] Update `adapters/nucleo/README.md` (multi-rate/multi-format + the two-cable/latency notes) and record the D4/D20 supersession (FR-015) so the base spec is not read as authoritative
- [ ] T026 [tier:balanced] ⚠ Final acceptance: full host suite + nucleo cross-build + portability gate green; operator sign-off across all four rate×depth combinations in Logic (SC-006)

---

## Dependencies & order

- **Setup (T001)** → **Foundational (T002)** → user stories.
- **US1 (T003–T007)** is the MVP and the base every later story builds on (the synchronous
  declaration + flow control). **US2** (multi-rate) precedes **US3** (24-bit) by priority; US3's
  descriptor/FIFO changes (T014/T015) depend on US1's endpoint config. **US4** (rings/latency)
  depends on the transport being synchronous (US1–US3 in place to measure across combos). **US5**
  (evaluator) uses the T002 harness and can proceed once any story streams. **Polish** last.
- The **FR-014 gate (T015)** is a hard decision point: a budget overrun routes to the operator
  (Constitution V), not a silent cut.

## Parallel opportunities

- Host tests are `[P]` against their implementation (T006, T011; T013's test half).
- T002 (capture harness) can be built in parallel with US1's firmware tasks.
- Within US3, the packed-24 converter (T013) is independent of the descriptor/FIFO work (T014) until
  the format-aware paths (T016) join them.

## Implementation strategy (MVP-first)

**MVP = US1**: sync declaration + flow control + cold-drain fix at the existing 48 kHz/16-bit — this
alone fixes the pitch/noise defect and is independently shippable. Layer US2 (44.1 kHz), then US3
(24-bit, with the feasibility gate), then US4 (latency/rings) and US5 (the durable guard).
