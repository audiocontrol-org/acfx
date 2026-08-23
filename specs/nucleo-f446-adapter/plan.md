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

# Implementation Plan: NUCLEO-F446RE adapter with USB audio I/O

**Branch**: `nucleo-f446-adapter` | **Date**: 2026-08-23 | **Spec**: [spec.md](./spec.md)

**Input**: Feature specification from `specs/nucleo-f446-adapter/spec.md`, itself derived from
the operator-approved design record
`docs/superpowers/specs/2026-08-22-nucleo-f446-adapter-design.md` (**D1–D26**) plus the
2026-08-23 clarification session.

## Summary

Make a bare NUCLEO-F446RE — a general-purpose development board with **no audio hardware at
all** — into an acfx target by using USB Audio Class as the audio interface, so the host acts as
the codec.

The technical approach follows **D1**: split the adapter into a thin silicon-touching shim
(`nucleo-main.cpp` — clock, GPIO, TinyUSB init, the OTG_FS ISR, the service loop) and a thicker
**platform-independent, host-testable** support library (`acfx_nucleo_support` — sample-format
conversion, the audio ring buffer, the parameter shadow block, the MIDI-CC mapping, the
telemetry record). This applies acfx's own "platform-independent core, thin adapter" principle
*recursively inside the adapter*, and it is the only decomposition that closes the untested-glue
gap the Daisy and Teensy adapters currently carry — neither has a single behavioural test.

The runtime contract is fixed by **D20–D22**: the host's SOF is the only clock, OUT is an
adaptive sink, IN is an asynchronous source, and there is **no feedback endpoint** — not an
omission, but because a device consuming exactly what the host sends has no rate to report.
Packets carry 0–49 frames, and the 2026-08-23 clarification decoupled that from the DSP, which
draws **fixed 48-frame blocks** from the ring.

## Technical Context

**Language/Version**: C++17 for `acfx_nucleo_support` (matching `acfx_core`'s
`cxx_std_17` interface requirement); C++20 available for the shim, as the Daisy factory already
requests. `-fno-exceptions -fno-rtti` on the ARM target (**D11**).

**Primary Dependencies**: TinyUSB pinned **0.21.0** (**D8**, **D9**); `cmsis_device_f4`; CMSIS
core. All `DOWNLOAD_ONLY`, compiled from source into the firmware, mirroring how the Daisy and
Teensy adapters consume their vendor trees. No STM32Cube HAL — deliberately (**D6** rationale).

**Storage**: N/A — no persistence. All state is in SRAM and lives for one power cycle.

**Testing**: Host-side **doctest** via the existing `acfx_core_tests` binary (`test` preset,
`ctest --preset test`), extended with the support library's suites. Plus CI cross-compile+link,
plus a hardware-in-the-loop harness that cannot run in normal CI (**D18** — all three layers).

**Target Platform**: STM32F446RE, Cortex-M4F (single-precision FPU, `fpv4-sp-d16`), 512 KB
flash / 128 KB SRAM, bare metal. Host side: macOS verified; Linux/Windows expected by class
compliance, unverified.

**Project Type**: Embedded firmware adapter over a header-only platform-independent core, plus a
host-testable support library.

**Performance Goals**: One isochronous packet per 1 ms USB frame at 48 kHz / 16-bit / stereo
(**D4**). Each 48-frame DSP block must complete well inside the frame period; the actual
headroom is *measured* via `worstBlockMicros` rather than asserted (FR-034), and which effects
fit is open question 8.

**Constraints**: No heap allocation and no locks in the audio path (**D16**, FR-030) — the
repo's existing allocation-sentinel discipline enforces this host-side. Files within ~300–500
lines. `acfx_core` must never learn about USB (FR-004). Ring capacity, water marks, and startup
fill are **measurement-derived and deliberately unpinned** (**D23**, FR-035).

**Scale/Scope**: One new adapter directory, one new support library, one new toolchain file, one
new CMake factory, one new preset, one new host test suite, one HIL harness. **73 functional
requirements across 10 user stories** (grown from 56/9 by the real-time/transport requirements
review — see [`checklists/realtime-transport.md`](./checklists/realtime-transport.md) §
Resolution).

## Constitution Check

*GATE: evaluated before Phase 0 and re-evaluated after Phase 1 design.*

| Principle | Verdict | Note |
|---|---|---|
| **I. Commit and push early and often** | ✅ Pass | Spec and clarifications already landed as two atomic commits, both pushed. Implementation tasks are sliced per user story to keep commits small. |
| **II. No git hooks, ever** | ✅ Pass | This feature adds none. Its gates are CI, the host suite, and the HIL harness — explicit and visible. |
| **III. Descriptive names, never numeric prefixes** | ✅ Pass | `specs/nucleo-f446-adapter/`, `adapters/nucleo/`, `cmake/toolchains/nucleo-f446.cmake`. No ordinal prefixes anywhere. |
| **IV. All UI/UX work goes through `/frontend-design`** | ✅ N/A | No user-facing visual surface. The LED fault pattern (FR-015a) is a diagnostic signal on a one-LED board, not a designed interface. |
| **V. Scope is the operator's call** | ✅ Pass | Nothing cut. All 11 open questions carried forward as open; the CDC function was an *operator* decision in the clarify pass; R9 recommends a MIDI mapping without applying it. |
| **VI. Platform-independent core, thin adapters** | ✅ Pass | Core gains nothing. **D1** applies the same principle recursively — see the Complexity Tracking note below, which is where this earns scrutiny rather than a rubber stamp. |
| **VII. No fallbacks, no mock data outside tests** | ⚠️ Justified | See below. |
| **VIII. Real-time safety in the audio path** | ✅ Pass | No heap, no locks (FR-030). Single execution context (**D26**, FR-046) means no lock-free discipline is needed *today*; FR-047 records the explicit trigger to revisit. |
| **IX. Strict typing & small modules** | ✅ Pass | ~300–500 lines per file (FR-005). Structure below splits the support library into five files precisely to hold that. |
| **X. Test the core host-side** | ✅ Pass | This is the principle **D1** exists to satisfy: the support library is host-compilable so doctest can reach it (FR-002, FR-049). |

### Principle VII — the one that needs justifying, not waving through

**No Fallbacks** says: raise a descriptive error for missing functionality rather than
substituting a plausible value. An audio callback **cannot** do that. It must emit *something*
within a bounded time and it cannot throw. FR-031/FR-032 resolve this the only honest way
available: the substitution is **defined** (underflow → silence, overflow → drop oldest) and
**observable** (every substitution increments a counter, and `blocksProcessed` gives those
counters a denominator so they read as a rate).

This is not an exemption from the principle; it is the principle applied to a domain where
throwing is not an option. The failure mode the principle protects against is *silent*
substitution, and nothing here is silent. Recorded in Complexity Tracking below.

The same reasoning covers R7's telemetry drop when nothing has the CDC port open — with the
difference that dropped telemetry needs no counter, because the counters *are* the payload.

## Project Structure

### Documentation (this feature)

```text
specs/nucleo-f446-adapter/
├── spec.md              # Feature specification (56 FRs, 9 user stories, 11 open questions)
├── plan.md              # This file
├── research.md          # Phase 0 — R1..R12, incl. what remains unverified
├── data-model.md        # Phase 1 — entities and their invariants
├── quickstart.md        # Phase 1 — how to build, flash, and validate
├── contracts/
│   └── nucleo-support.md   # Phase 1 — the support library's public surface
├── checklists/
│   └── requirements.md  # Spec quality checklist
└── tasks.md             # Phase 2 — produced by /speckit-tasks, not by this command
```

### Source Code (repository root)

```text
adapters/nucleo/
├── CMakeLists.txt           # Declares firmware targets via acfx_add_effect_nucleo
├── nucleo-main.cpp          # THE SHIM: clock, GPIO/LED, TinyUSB init, OTG_FS ISR, service loop
├── usb-descriptors.h        # Locally authored composite descriptor (UAC2 + MIDI + CDC)
├── usb-descriptors.cpp      # Descriptor tables + TinyUSB descriptor callbacks
├── tusb_config.h            # TinyUSB configuration (incl. CFG_TUD_VBUS_DETECT_HW 0)
├── startup/
│   ├── vector-table.cpp     # Generated from CMSIS IRQn_Type (D13)
│   └── nucleo-f446.ld       # Linker script (512K flash / 128K SRAM)
└── support/                 # acfx_nucleo_support — PLATFORM-INDEPENDENT, host-compilable
    ├── sample-format.h      # int16 interleaved <-> float non-interleaved (FR-038, FR-038a)
    ├── audio-ring.h         # Statically sized SPSC ring + defined substitutions (FR-030..032)
    ├── transport-stats.h    # AudioTransportStats + rate helpers (FR-033, FR-034)
    ├── parameter-shadow.h   # Per-ParamId shadow block + dirty flags (FR-041..FR-044)
    └── midi-cc-map.h        # CC -> ParamId table-driven mapping (FR-045)

cmake/
├── toolchains/nucleo-f446.cmake   # Cortex-M4, fpv4-sp-d16, hard float, libstdc++ probe
├── acfx-effect-targets.cmake      # + acfx_add_effect_nucleo factory
└── dependencies.cmake             # + TinyUSB 0.21.0, cmsis_device_f4, CMSIS core

tests/core/
├── nucleo-sample-format-test.cpp
├── nucleo-audio-ring-test.cpp
├── nucleo-parameter-shadow-test.cpp
└── nucleo-midi-cc-map-test.cpp

tools/nucleo-hil/               # HIL harness (FR-050) — NOT wired into normal CI
└── (home and invocation are open question 6)
```

**Structure Decision**: The adapter follows the existing `adapters/<target>/` convention
(`adapters/daisy/`, `adapters/teensy/`), and the toolchain, factory, and dependency changes
extend the three files that already carry the equivalents for Daisy and Teensy. The one genuine
departure is `adapters/nucleo/support/` — a second CMake target inside an adapter directory,
which neither existing MCU adapter has. That is **D1**, and it is what makes FR-049's host
coverage possible.

### The build-graph constraint that shapes everything (research R8)

`acfx_nucleo_support` **must not** be declared only under `if(ACFX_BUILD_NUCLEO)`. The host test
suite builds under the `test` preset, which sets **no toolchain** and enables only
`ACFX_BUILD_TESTS`; a support library gated behind the Nucleo option would be invisible to it,
silently reproducing the exact untested-glue blind spot **D1** exists to close.

It is therefore declared as an unconditional `INTERFACE` target at the top level — the same
shape as `acfx_core` and `acfx_analysis` — and consumed by whichever targets are enabled. This
in turn is what forces the support library to be **strictly platform-independent**: no TinyUSB
headers, no CMSIS, no board headers. Anything needing them belongs in the shim (FR-003).

## Implementation Phases

Sequenced so each phase leaves the tree in a state that builds and tests, per Commandment 1.

**Phase A — Build surface (US1).** Toolchain file with the libstdc++ probe reused from
`daisy.cmake`, the `acfx_add_effect_nucleo` factory, the `nucleo` preset, the three CPM pins with
real fetch-verified refs, the linker script, and the CMSIS-derived vector table. Exit: the
`nucleo` preset configures and an empty-`main` firmware links.

**Phase B — Support library + host tests (US2, US3, US6).** The five headers under
`support/`, declared as an unconditional target, with their doctest suites joining
`acfx_core_tests`. Nothing here touches silicon, so all of it is verifiable on the host before
any board is involved. Exit: `ctest --preset test` green, allocation sentinel clean.

**Phase C — Clock and fault signalling (US8).** LED GPIO first, then HSE-bypass + PLL bring-up,
then the fatal-on-lock-failure path. Exit: a board runs at 168 MHz with 48 MHz on PLLQ, and a
forced lock failure blinks LD2 and halts.

**Phase D — USB enumeration (US4).** `tusb_config.h`, the composite descriptor, TinyUSB init,
the OTG_FS ISR, and the `tud_task()` loop. Exit: the device enumerates driverless with audio,
MIDI, and serial functions visible on a host.

**Phase E — Audio path and USB lifecycle (US5, US7, US10).** Wire the polled data path to the
ring and the ring to the effect's fixed 48-frame block; handle the capture-only case; handle
suspend, resume, and bus reset, including clearing the rings and re-arming the startup-fill wait
while leaving the counters intact. Exit: audio passes through an effect and is audibly
transformed, and survives a host sleep/wake cycle.

**Phase F — Parameters (US6 hardware half).** MIDI CC in, through the mapping, into the shadow
block, applied once per block. Exit: a controller moves a parameter live.

**Phase G — Telemetry and HIL (US9).** CDC telemetry emission, then the harness. Exit: the
harness reports the counter set from an attached board.

**Phase H — Measurement pass (FR-035).** Only now can ring capacity, water marks, and startup
fill be derived, using R5's procedure. This phase is *why* those numbers are not in this plan.

Phases A and B are independent of hardware and of each other's runtime; B is where most of the
testable substance lives.

## Complexity Tracking

| Violation | Why Needed | Simpler Alternative Rejected Because |
|---|---|---|
| Two CMake targets inside one adapter directory (**D1**) | The adapter has strictly more glue than Daisy or Teensy — conversion, buffering, parameter marshalling — and a single-file adapter offers no seam a host test can reach | A single `nucleo-main.cpp` mirroring `daisy-main.cpp` is the conventional shape, and was rejected in the design record: matching that precedent means inheriting its untested-glue problem, in the adapter that can least afford it. The operator chose all three verification layers |
| Bounded substitution in the audio path instead of a raised error (Principle VII) | An audio callback must emit within a bounded time and cannot throw | Raising is not available. The principle's real target is *silent* substitution; FR-031/FR-032 make every substitution defined and counted, with `blocksProcessed` as a denominator so it reads as a rate |
| A third USB function (CDC) beyond **D5**'s UAC2 + MIDI | The HIL harness must read `AudioTransportStats` off the board, and **D5** provided no channel for it | MIDI SysEx mixes telemetry into the control path with fiddly framing; a vendor request risks a Windows driver prompt; debugger-only readback puts a probe in every HIL run. **Operator decision, 2026-08-23** — flagged because it extends an approved design record |
| Ring capacity/water marks/startup fill left unpinned (**D23**) | Pre-measurement values would be invented; the spike's 0.2% figure came from a naive single buffer and predicts nothing | Picking plausible numbers now is exactly the false precision **D23** was written to prevent. R5 supplies the measurement procedure instead, and Phase H is where the numbers land |

## Risks

- **TinyUSB 0.21.0 API drift (R1).** The removed `rx_done`/`tx_done` callbacks link *silently*;
  the alt-setting callback names must be read off the pinned tree, never recalled. Highest-cost
  trap in the feature: it produces a board that enumerates perfectly and passes no audio.
- **OTG_FS endpoint budget (R2).** Three functions across iso + bulk + interrupt endpoints may
  not fit. If it does not close, that is a finding for the operator, not an agent-side cut.
- **DWT `CYCCNT` availability (R6).** A stuck-at-zero counter would report
  `worstBlockMicros = 0`, indistinguishable from "instantaneous" — must fail loud.
- **Open question 7 blocks the tail of US6.** The mapping *mechanism* is planned (R9); the
  concrete CC convention is the operator's and is needed before US6 can be called complete.
