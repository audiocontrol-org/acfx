> ‼ **acfx COMMANDMENTS — non-negotiable** ‼
> **1. COMMIT AND PUSH EARLY AND OFTEN** — small atomic commits, pushed promptly.
> **2. NO GIT HOOKS, EVER.**
> **3. DESCRIPTIVE NAMES, NEVER NUMERIC PREFIXES** (datestamps excepted).
> **4. ALL UI/UX WORK GOES THROUGH `/frontend-design`.**
> **5. SCOPE IS THE OPERATOR'S CALL** — never cut/defer/drop scope on "YAGNI"; present options and ASK.
> (acfx Constitution, Principles I–V — `.specify/memory/constitution.md`.)

# Implementation Plan: Synchronous, multi-format USB-audio transport (NUCLEO-F446RE)

**Branch**: `nucleo-f446-adapter` | **Date**: 2026-08-25 | **Spec**: [spec.md](./spec.md)

**Input**: [spec.md](./spec.md) (17 FRs, 6 SCs, 5 user stories) · **Research**: [research.md](./research.md)

## Summary

Turn the Nucleo USB-audio device from a free-running, no-feedback **asynchronous** source into a
proper **synchronous** (USB-SOF-locked) UAC2 device that natively supports **44.1 + 48 kHz × 16 +
packed-24-bit** stereo, fixing the pitch-down / digital-noise / ~0.5 s-latency defect seen in a
CoreAudio aggregate. Research (R1–R10) established that this is **one coupled change** driven
through TinyUSB's rate/format mechanism, and — critically — that **no SOF-pipeline rewrite is
needed**: enabling `CFG_TUD_AUDIO_EP_IN_FLOW_CONTROL` makes TinyUSB pace the IN endpoint to the
exact nominal per-SOF cadence (including the fractional 44 100/1 000 schedule for 44.1 kHz) off
iso-transfer completion, while the existing free-poll service loop just keeps the FIFO fed. 24-bit
**fits** the OTG-FS FIFO budget at both rates but **tightly** (≈6.25 % headroom at 48 kHz), an
operator-owned gate (FR-014) with a documented fallback table. UAC2 latency reporting is best-effort
(unconsumed in practice), so FR-009 reduces to minimise + measure + document + verify PDC
empirically. Verification is an objective host-side USB packet capture (nominal-cadence /
accumulated-rate) plus operator acceptance in Logic across all four rate×depth combinations.

## Technical Context

**Language/Version**: C++20 (adapter + core), `extern "C"` for TinyUSB callbacks.

**Primary Dependencies**: TinyUSB 0.21.0 (pinned, UAC2 device class) · CMSIS `cmsis_device_f4` ·
DaisySP (effect). Host tooling: bash + a host-side USB packet-capture facility (macOS).

**Storage**: N/A (firmware).

**Testing**: host doctest suite (`test` preset) for the platform-independent pieces (packed-24
conversion, format/rate selection logic, packet-cadence/accumulator logic where host-testable) ·
`nucleo` cross-build gate · host-side USB packet capture (objective transport gate, FR-013) ·
operator acceptance in Logic Pro through a CoreAudio aggregate (SC-006). Host tests are written
before implementation.

**Target Platform**: STM32F446RE, Cortex-M4F, OTG-FS **full-speed** USB device; 320-word (1.25 KB)
endpoint FIFO RAM shared across audio IN/OUT + MIDI + CDC.

**Project Type**: embedded firmware — platform-independent DSP core + thin Nucleo adapter (the
transport change is entirely adapter-side).

**Performance Goals**: steady nominal frames/SOF with zero ZLP/short packets in steady state
(SC-002); round-trip latency a small bounded value (SC-004), not the prior ~0.5 s; DSP worst-block
budget unchanged (transport-only change).

**Constraints**: OTG-FS FIFO RAM ceiling 320 words — **24-bit leaves ≈20 words (6.25 %) free at
48 kHz**; RT-safety on the audio path (no heap/locks); the rate/format-change reaction (re-prepare
+ ring reset) runs **off** the RT path, on the poll loop.

**Scale/Scope**: one transport, one adapter; 4 rate×depth combinations; ~7 adapter files + host
tests + one packet-capture tool.

## Constitution Check

*GATE: must pass before Phase 0 (passed) and re-checked after Phase 1 design (passed).*

- **VI — Platform-Independent Core, Thin Adapters**: PASS. The change is adapter-side
  (`adapters/nucleo/…`); the DSP core is untouched except that the effect's existing
  `prepare(sampleRate)` seam is re-invoked on rate change. The packed-24 conversion lives in the
  adapter's `support/` (already the home of int16 conversion), not core.
- **VII — No Fallbacks, No Mock Data Outside Tests**: PASS. The 24-bit feasibility outcome is an
  **operator-owned decision** (FR-014) with a surfaced fallback table, not a silent cut; missing
  capability raises a described gate, not a mock.
- **VIII — Real-Time Safety in the Audio Path**: PASS. No heap/locks added to the audio callbacks;
  the rate/format-change reaction (effect `prepare()`, ring reset) is deferred to a poll-loop
  service step off the RT path (R9), exactly as the base US10 lifecycle handling does.
- **IX — Strict Typing & Small Modules**: PASS. No `any`/unchecked casts; new converter + paths
  stay within the ~300–500 line budget (the packed-24 path is small; descriptor/control edits are
  additive).
- **V — Scope Is the Operator's Call**: PASS. The one open scope item (24-bit vs the fallback) is
  gated to the operator (FR-014) with the byte budget on the table; nothing is narrowed unilaterally.

No violations → no Complexity-Tracking justification required (the tight FIFO margin is a *risk*,
tracked below, not a constitution violation).

## Project Structure

### Documentation (this feature)

```text
specs/synchronous-usb-audio-transport/
├── plan.md              # this file
├── research.md          # Phase 0 (R1–R10)
├── data-model.md        # Phase 1 — transport entities & state
├── quickstart.md        # Phase 1 — verification/run guide
├── contracts/
│   └── usb-transport-contract.md   # the device's USB-observable contract
└── tasks.md             # Phase 2 (/speckit-tasks)
```

### Source Code (repository root) — files this feature touches

```text
adapters/nucleo/
├── usb-descriptors.cpp / .h     # sync bmAttributes (0x0D), INT_VAR_CLK + RW freq control,
│                                #   add alt-2 packed-24; size static_asserts
├── tusb_config.h                # CFG_TUD_AUDIO_EP_IN_FLOW_CONTROL 1; resize EP/SW-buf for 24-bit
├── usb-audio-controls.cpp       # 2-subrange RANGE, RW CUR + STRONG set-freq cb; alt→format record
├── support/sample-format.h      # add packed-24 (3-byte, signed LE) wire<->float conversion
├── support/usb-out-path.h       # format-aware consume (16/24)
├── support/usb-in-path.h        # format-aware produce (16/24); occupancy instrumentation
├── support/usb-audio-service.h  # rate/format-change service step; ring occupancy counters
└── nucleo-main.cpp              # re-invokable PrepareEffect(rate); wire the rate-change step

tests/core/                      # host doctest: packed-24 conversion, cadence/accumulator logic,
│                                #   format/rate-selection state, no-alloc guards
scripts/nucleo-hil/              # host-side USB packet-capture tool + evaluator (FR-013);
                                 #   extend the existing HIL rig
```

**Structure Decision**: adapter-side transport rework; the platform-independent core is untouched
(Principle VI). New host-testable logic (packed-24 conversion, the packet-cadence/accumulator
reasoning where it can be modelled host-side) goes under `tests/core/`; the objective transport gate
is a host-side USB packet capture under `scripts/nucleo-hil/`.

## Complexity Tracking / Risks

No constitution violations. One implementation **risk** to carry into tasks:

| Risk | Detail | Mitigation |
|------|--------|------------|
| Tight 24-bit FIFO margin | Packed-24 at 48 kHz leaves ≈20/320 words (6.25 %) free; the resized IN SW-buf (≥4·Navg = 1152 B) and EP size (196→294 B) consume most of the slack | A task MUST re-verify the R14 FIFO budget against the **actual resized constants** (not the estimate) before committing 24-bit; if it overruns, fall back per FR-014's operator-owned table (24-bit-at-44.1-only, or 16-bit-only). Surface to the operator. |
| TinyUSB weak-callback trap | The new set-frequency and alt-setting callbacks silently no-op if not strong `.cpp` defs (TASK-37) | Objdump-verify the callbacks are strong, as the base feature learned to do; host tests can't catch this. |
| Verification can't run in CI / degrades locally | Bug only shows in an aggregate; host CoreAudio degrades with reflashing | Objective USB packet capture is the primary gate (board + host, not CI); operator Logic A/B is acceptance. |

## Phase 1 outputs

- [data-model.md](./data-model.md) — clock source, format, streams, rings, rate/format-change event.
- [contracts/usb-transport-contract.md](./contracts/usb-transport-contract.md) — the USB-observable
  device contract (sync endpoints, clock RANGE/CUR, alt settings, packet cadence, callbacks).
- [quickstart.md](./quickstart.md) — the verification/run guide (packet capture + Logic acceptance).
