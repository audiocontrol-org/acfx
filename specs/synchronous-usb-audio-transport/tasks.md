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

**Tests**: required. **TDD ordering**: every *host-testable* behaviour is a RED test **before** its
implementation. **Descriptor-/config-only** changes (endpoint `bmAttributes`, clock/format
descriptors, `tusb_config` macros) cannot be host-unit-tested — they are explicitly
implementation-first and verified by the nucleo cross-build + the objective USB packet capture +
hardware; each such task is marked *(descriptor/config — impl-first)*.

## Format: `[ID] [P?] [Story] [tier:<label>] Description with file path`

- **[P]** parallelizable · **[US n]** story served · **[tier:...]** model tier — `fast` (haiku:
  mechanical/RED-test/doc), `balanced` (sonnet: standard impl + integration/tests), `powerful`
  (opus: subtle USB correctness / high blast radius).

⚠ **Hardware-verification tasks** need a board + host and cannot run in CI or unattended; the
operator drives them. During `execute` they use the `- [~]` marker so the govern gate excludes them.

---

## Phase 1: Setup

- [ ] T001 [tier:fast] Baseline checkpoint: confirm `cmake --preset test && cmake --build --preset test -j && ctest --preset test`, `cmake --build --preset nucleo`, and `./scripts/check-portability.sh` green on the current tree; record the current 16-bit `EP_IN_SZ_MAX`/`kAudioEpSize`/SW-buf/FIFO `static_assert` values (`adapters/nucleo/tusb_config.h`, `usb-audio-service.h`) as the pre-change baseline (no file changes)

---

## Phase 2: Foundational (Blocking Prerequisites)

- [ ] T002 [tier:powerful] **Capture-backend feasibility gate + harness** (`scripts/nucleo-hil/`): FIRST establish and document the *exact usable* per-isochronous-packet USB capture backend on the operator's **macOS** host — Wireshark/`tshark` over the **XHC20** interface via libpcap (note the possible SIP-disable requirement on recent macOS); if that is not usable without unacceptable system modification, choose a Linux capture host or a hardware USB analyzer and record that decision. THEN implement the harness against the chosen backend, capturing **both** the IN and OUT endpoints' per-USB-frame packet sizes to a raw per-frame series for the evaluator (T024). (`usbmon` is Linux-only — do NOT assume it on macOS.) Excluded from CI (needs a board). *The entire objective-verification strategy depends on this backend being real.*

**Checkpoint**: the objective transport gate has a confirmed, working capture backend.

---

## Phase 3: User Story 1 — Pitch-correct, noise-free at 48 kHz/16-bit (Priority: P1) 🎯 MVP

**Goal**: present the device as SYNCHRONOUS and deliver a steady nominal 48-frames/SOF IN stream at the existing 48 kHz/16-bit → pitch-correct, noise-free round-trip.

**Independent test**: capture shows steady 48/frame, zero ZLP/short, accumulated 48000/1000; a tone returns at the same pitch, no noise, in a Logic aggregate.

- [ ] T003 [US1] [tier:powerful] *(descriptor/config — impl-first)* Declare both iso endpoints **Synchronous** — IN (`0x81`) + OUT (`0x01`) `bmAttributes` sync bits `0x05`/`0x09` → **`0x0D`** in `adapters/nucleo/usb-descriptors.cpp` (research §R2); confirm the nucleo cross-build links
- [ ] T004 [US1] [tier:balanced] *(config — impl-first)* Enable `CFG_TUD_AUDIO_EP_IN_FLOW_CONTROL 1` in `adapters/nucleo/tusb_config.h` (research §R5). Express the IN-FIFO adequacy as a **parametric** `static_assert` (IN SW-buf ≥ 4·Navg where `Navg` derives from the active max subslot size and rate) rather than a magic 16-bit constant — it must survive the 24-bit resize (T015)
- [ ] T005 [P] [US1] [tier:fast] **RED** host test `tests/core/nucleo-inpath-startup-test.cpp` (registered in `tests/CMakeLists.txt`): assert the IN path / output ring does NOT empty at startup under a SOF-paced draw (the FR-007 cold-drain), driving the host-testable logic in `support/usb-in-path.h`/`audio-ring.h`; confirm it FAILS first
- [ ] T006 [US1] [tier:balanced] Implement the FR-007 cold-drain fix in `adapters/nucleo/support/usb-in-path.h`/`usb-audio-service.h` so SOF-paced reads are served real audio (GREEN for T005)
- [~] T007 [US1] [tier:balanced] ⚠ Hardware verification: via the T002 capture confirm 48k/16-bit holds steady 48-frame IN packets, **zero ZLP/short**, accumulated 48000/1000; and in a Logic aggregate a sustained tone returns **pitch-correct + noise-free** (SC-001/002/003). **[analyze fold-in — FR-011]** also confirm the **dry / effect-bypassed** signal round-trips **pitch-correct + noise-free** in the same aggregate (the transport correctness is independent of the effect)

**Checkpoint**: the core defect is fixed at 48 kHz/16-bit — a usable real-time effect (MVP).

---

## Phase 4: User Story 2 — Native 44.1 + 48 kHz (Priority: P1)

**Goal**: advertise + honour both rates; the exact fractional 44 100/1 000 cadence at 44.1 kHz.

**Independent test**: select 44.1 kHz; capture shows accumulated 44100/1000; live 48↔44.1 change without re-plug; pitch-correct.

- [ ] T008 [P] [US2] [tier:fast] **RED** host test `tests/core/nucleo-rate-select-test.cpp`: the rate-selection logic accepts {44100,48000} and rejects other values; confirm it FAILS first
- [ ] T009 [US2] [tier:powerful] *(descriptor part impl-first)* Multi-rate clock in `adapters/nucleo/usb-descriptors.cpp` + `usb-audio-controls.cpp` (research §R3): `INT_FIX_CLK`→`INT_VAR_CLK`, freq control `AUDIO20_CTRL_R`→`AUDIO20_CTRL_RW`, RANGE two subranges {44100},{48000}, CUR = `g_currentSampleRateHz`; add the STRONG `extern "C"` `tud_audio_set_req_entity_cb` accepting SAM_FREQ ∈ {44100,48000} (research §R9; STRONG `.cpp` def — TASK-37 weak-callback trap) — GREEN for T008
- [ ] T010 [P] [US2] [tier:fast] **RED** host test `tests/core/nucleo-rate-change-lifecycle-test.cpp`: the rate-change flag is set on a frequency change and consumed exactly once by the service step; confirm it FAILS first
- [ ] T011 [US2] [tier:balanced] Make `PrepareEffect()` re-invokable at the selected rate in `adapters/nucleo/nucleo-main.cpp` (today one-shot at 48k, `:440`) and add a poll-loop **rate-change service step** in `adapters/nucleo/usb-audio-service.h` that re-prepares the effect + resets the rings off EP0 context (FR-006; mirrors `ServiceUsbLifecycle`) — GREEN for T010
- [~] T012 [US2] [tier:balanced] ⚠ Hardware verification: select 44.1 kHz — capture shows accumulated **44100/1000** (44/45 scheduled), zero ZLP/short, pitch-correct + noise-free; a **live 48↔44.1 change** streams through without a re-plug (SC-005)

---

## Phase 5: User Story 3 — 16 + packed-24-bit (Priority: P2)

**Goal**: advertise + honour 16-bit and packed-24-bit, with a defined format-transition; resolve the FR-014 feasibility gate.

**Independent test**: select 24-bit at each rate; a live 16↔24 change resets/re-primes cleanly; capture + Logic show correct, noise-free 24-bit streaming.

- [ ] T013 [P] [US3] [tier:fast] **RED** host test `tests/core/nucleo-packed24-test.cpp`: packed-24 (3-byte signed LE) wire↔float round-trips within 24-bit resolution; confirm it FAILS first
- [ ] T014 [US3] [tier:balanced] Implement packed-24 (3-byte, signed LE) wire↔float conversion in `adapters/nucleo/support/sample-format.h` alongside the int16 path (research §R6) — GREEN for T013
- [ ] T015 [US3] [tier:powerful] *(descriptor/config part impl-first)* Add alt-2 (`bSubslotSize=3`, `bBitResolution=24`) to each AudioStreaming interface in `adapters/nucleo/usb-descriptors.cpp` (research §R4); resize `EP_IN_SZ_MAX`/`kAudioEpSize`/SW-buf for 24-bit (Navg 288 → IN SW-buf ≥ 1152 B) in `tusb_config.h`; extend the strong `tud_audio_set_itf_cb` to record the selected format (pcm16/pcm24)
- [ ] T016 [US3] [tier:powerful] **FR-014 feasibility gate** — re-verify the OTG-FS FIFO-RAM budget against the ACTUAL resized 24-bit constants (research §R7: ≈20/320 words free at 48 kHz) and update the size `static_assert`s in `usb-audio-service.h` + `usb-descriptors.h`. **If it overruns, STOP and surface the fallback table (24-bit-at-44.1-only / 16-bit-only) to the operator — do NOT cut silently** (Constitution V)
- [ ] T017 [P] [US3] [tier:fast] **RED** host test `tests/core/nucleo-format-transition-test.cpp`: a format change records the pending format and the service step performs exactly one transport reset/re-prime; confirm it FAILS first
- [ ] T018 [US3] [tier:balanced] **Format-transition service** (the missing lifecycle op): on a SET_INTERFACE alt change, defer to the poll loop a defined **transport reset / re-prime** — reuse the rate-change / stream-open-edge machinery to reset the rings + flush in-flight FIFO across the packet-size change and switch the converter depth cleanly, then resume (FR-006). NOTE: bit depth does NOT re-run the effect `prepare()` (the DSP stays float); this resets *transport* state, not the effect. GREEN for T017
- [ ] T019 [US3] [tier:balanced] Make `adapters/nucleo/support/usb-out-path.h` and `usb-in-path.h` format-aware (select the 16/24 converter by the recorded format); host test both depths in `tests/core/`
- [~] T020 [US3] [tier:balanced] ⚠ Hardware verification: select 24-bit at 44.1 and 48 kHz — capture + Logic confirm correct, noise-free streaming; a **live 16↔24 format change** resets/re-primes without a re-plug (SC-005)

---

## Phase 6: User Story 4 — Low, measured latency (Priority: P2)

**Goal**: shrink the rings to the measured minimum and pin the latency numbers; satisfy FR-009.

**Independent test**: measured round-trip is a small bounded value (not ~0.5 s); pinned values recorded.

- [ ] T021 [US4] [tier:balanced] Add ring **occupancy instrumentation** (min/max) to `adapters/nucleo/support/audio-ring.h`/`usb-audio-service.h` — the missing R15 measurement gap (FR-008)
- [~] T022 [US4] [tier:powerful] ⚠ Measure across all 4 rate×depth combos + a live rate/format change via the base feature's HIL harness (`scripts/nucleo-hil/run-hil.sh`, the tool the base spec built); **derive and PIN** ring capacity/startup-fill/water-range + round-trip latency (frames + ms) — reduce the rings from the 1024/98 placeholder to the measured minimum in `usb-audio-service.h`, recording the evidence in `specs/synchronous-usb-audio-transport/research.md` (FR-008/SC-004)
- [ ] T023 [US4] [tier:balanced] **Latency (FR-009, three obligations)**: (a) **MUST** — record the measured round-trip latency (frames + ms) from T022 in research.md/`adapters/nucleo/README.md`; (b) SHOULD — expose device latency via a UAC2 mechanism ONLY IF research/testing confirms a host actually consumes one (NOT `bLockDelay`); (c) **MUST** — verify empirically whether Logic/CoreAudio applies it for PDC and record the result. Do NOT block (a)/(c) on (b)

---

## Phase 7: User Story 5 — Objective verification guard (Priority: P3)

**Goal**: the packet capture is a full-metric, dual-direction, durable regression guard.

**Independent test**: the evaluator reports the full metric set for IN and OUT and PASS/FAILs on zero-ZLP + accumulated-rate tracking + no input-ring drift.

- [ ] T024 [US5] [tier:balanced] Complete the packet-capture **evaluator** (`scripts/nucleo-hil/`) covering **BOTH the IN and OUT endpoints**: report per direction the selected rate, subslot size, USB/audio frame totals, size histogram, ZLP + non-nominal counts, effective frames/second; and assert **OUT health** (FR-003) — expected OUT cadence and **no systematic input-ring drift / accumulated over-underrun** in healthy streaming. PASS = zero ZLP/short in steady state AND accumulated frames tracking the exact SOF-derived schedule (FR-013). Add fixtures + a host self-test of the pure evaluator
- [ ] T025 [US5] [tier:fast] Confirm the capture harness is excluded from CI and documented (no `.github`/ctest reference), mirroring the base HIL harness

---

## Phase 8: Polish & Cross-Cutting

- [ ] T026 [tier:fast] Objdump-verify every new/changed TinyUSB callback (`tud_audio_set_req_entity_cb`, `tud_audio_set_itf_cb`) is a STRONG symbol, not the weak default (TASK-37 / [[tinyusb-weak-callback-linkage-trap]]) — a clean link is not a boot check
- [ ] T027 [tier:fast] Confirm `./scripts/check-portability.sh` passes (core acquired no adapter/USB dep; every changed file ≤ 500 lines) and the nucleo cross-build is clean
- [ ] T028 [tier:fast] Update `adapters/nucleo/README.md` (multi-rate/multi-format + latency notes) and record the D4/D20 supersession (FR-015) + the pinned latency/ring values
- [~] T029 [tier:balanced] ⚠ **Lifecycle regression** (FR-012 — descriptor/FIFO/cadence/ring changes can regress it): re-run on hardware **capture-only, playback-only, duplex, both-closed, suspend→resume, bus-reset/re-enumeration, rate-change-while-streaming, format-change-while-streaming** and confirm each still behaves per the base US10 contract (counters untouched across events, AR9). **[analyze fold-in — FR-016]** confirm the host sees a **single SOF-derived clock domain** (both AudioStreaming interfaces cite the same Clock Source Entity). **[analyze fold-in — FR-017]** inject a torn/short/ZLP OUT payload and confirm it is **counted** (substitution counter), does **not** misalign the stereo channels, and does **not** perturb the IN cadence
- [~] T030 [tier:balanced] ⚠ Final acceptance: full host suite + nucleo cross-build + portability gate green; operator sign-off across all four rate×depth combinations in Logic (SC-006)

---

## Dependencies & order

- **Setup (T001)** → **Foundational (T002 capture backend)** → user stories.
- **US1 (T003–T007)** is the MVP and the base every later story builds on. **US2** (multi-rate)
  precedes **US3** (24-bit) by priority. **US4** (rings/latency) needs the transport synchronous
  across combos (US1–US3). **US5** (evaluator) uses the T002 backend. **Polish** last, incl. the
  **lifecycle regression (T029)** and objdump/portability guards.
- Within each story, **RED test → implementation → integration/hardware** (T005→T006, T008→T009,
  T010→T011, T013→T014, T017→T018). Descriptor/config tasks (T003, T004, T009-descriptor,
  T015-descriptor) are impl-first, verified by cross-build + capture + hardware.
- The **FR-014 gate (T016)** is a hard operator decision point (Constitution V).

## Parallel opportunities

- RED tests are `[P]` against their implementation (T005, T008, T010, T013, T017).
- T002 (capture backend) proceeds in parallel with US1 firmware once its backend is confirmed.
- Within US3, the packed-24 converter (T013/T014) is independent of the descriptor/FIFO work
  (T015) until the format-aware paths (T019) join them.

## Implementation strategy (MVP-first)

**MVP = US1**: sync declaration + flow control + cold-drain fix at the existing 48 kHz/16-bit — this
alone answers "did correcting the clock model fix the observed defect?" and is independently
shippable. Then US2 (44.1 kHz), US3 (24-bit + the feasibility gate + the format-transition), US4
(latency/rings), US5 (the dual-direction durable guard), and the lifecycle-regression polish.
