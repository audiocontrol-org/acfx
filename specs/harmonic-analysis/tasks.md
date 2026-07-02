---
description: "Task list for the harmonic-analysis nonlinear characterization tooling feature"
---

> ‼ **acfx COMMANDMENTS — non-negotiable** ‼
> **1. COMMIT AND PUSH EARLY AND OFTEN** — version control is a distributed, journaled
> filesystem that safeguards your work, **NOT a sacred rite reserved for the blessed.**
> **2. NO GIT HOOKS, EVER** — this repo uses zero git hooks; none exist, none get added.
> **3. DESCRIPTIVE NAMES, NEVER NUMERIC PREFIXES** — datestamps excepted.
> (acfx Constitution, Principles I–III — `.specify/memory/constitution.md`.)

# Tasks: Harmonic Analysis — Nonlinear Characterization Tooling

**Input**: Design documents in `specs/harmonic-analysis/` (plan.md, spec.md, research.md, data-model.md, contracts/{capture-probe-api,analysis-engine-api}.md, quickstart.md)

**Tests**: INCLUDED — the acfx core is validated host-side by measurement (Constitution VIII, X); every metric asserts against an analytic reference within a named tolerance. Test tasks are first-class, RED before GREEN.

**Organization**: Tasks are grouped by user story (spec.md priorities). The RT capture probe is authored under `core/primitives/analysis/` (portable); the shared analysis engine under `host/analysis/` (host-only); the live readouts in `adapters/{workbench,plugin}/`.

## Format: `[ID] [P?] [Story] [tier:label] Description`

- **[P]**: Can run in parallel (different files, no dependency on an incomplete task)
- **[Story]**: US1..US5 (user-story phases only)
- **[tier:label]**: model-sized-dispatch tier — `fast`/`balanced`/`powerful` resolve via `.stack-control/config.yaml` `tier_map` (fast=haiku mechanical, balanced=sonnet standard, powerful=opus subtle DSP/concurrency correctness)
- Every task names exact file path(s)

## Path Conventions

Portable RT probe under `core/primitives/analysis/`; host engine under `host/analysis/`; live readouts under `adapters/workbench/` + `adapters/plugin/`; tests under `tests/core/` + `tests/support/`; gate at `scripts/check-portability.sh`.

---

## Phase 1: Setup (Shared Infrastructure)

**Purpose**: Directory skeletons and the portability gate, ready before code lands.

- [x] T001 [tier:fast] Create the directory skeletons: `core/primitives/analysis/` (placeholder `README.md`) and `host/analysis/` (placeholder `README.md`), per plan.md Project Structure.
- [x] T002 [P] [tier:balanced] Extend `scripts/check-portability.sh` to cover `core/primitives/analysis/**` (platform-free, harness-free) and assert neither `host/analysis/**` nor any adapter is reachable from `core/` (dependency direction), following the existing `core/primitives/oversampling` gate block; must pass vacuously while the trees are empty.

**Checkpoint**: Directories exist; the gate recognizes the new trees.

---

## Phase 2: Foundational (Blocking Prerequisites)

**Purpose**: The FFT + window + shared engine seam every offline story builds on. ⚠️ No user story can proceed until this phase is complete.

- [x] T003 [tier:balanced] RED: `tests/core/analysis-fft-test.cpp` — windowed radix-2 FFT reconstructs analytic tones within tolerance; **a non-power-of-two length is rejected with a descriptive error** (FR-009/026). (Links RED until T004.)
- [x] T004 [tier:powerful] `host/analysis/fft.h` — self-contained iterative radix-2 FFT (namespace `acfx::analysis`), twiddles precomputed at `init()`, applies the configured window, **power-of-two-only with a descriptive error on non-pow2**; GREEN T003 (research Decision 2).
- [x] T005 [P] [tier:balanced] RED: `tests/core/analysis-window-test.cpp` — default window is 4-term Blackman-Harris; Hann and flat-top selectable; sidelobe/main-lobe sanity (FR-025). (Links RED until T006.)
- [x] T006 [tier:balanced] `host/analysis/window.h` — selectable `WindowKind{BlackmanHarris(default),Hann,FlatTop}`, coeffs at `init()`; GREEN T005 (research Decision 3).
- [x] T007 [tier:balanced] **Relocate** the reusable building blocks from `tests/support/measurement/` into `host/analysis/` — `stimulus.h` (Sine/Sweep/Impulse/Step/WhiteNoise), `goertzel.h` (exact single-bin `GoertzelAnalyzer`), `aliasing.h` (integer-cycle inharmonic measure), and the `captureCallable` seam — make `tests/support/measurement/` **re-export** them (no duplicate impl; dependency `tests/support → host/analysis`, never the reverse — analyze F1); then author `host/analysis/analysis-engine.h`, the single entry surface (namespace `acfx::analysis`) composing Window + Fft + the relocated capture seam, establishing the one-engine seam tests and adapters share (FR-006/014/015 foundation). Existing call sites keep compiling.

**Checkpoint**: engine core builds; FFT + window suites green.

---

## Phase 3: User Story 1 — Deep offline harmonic characterization (Priority: P1) 🎯 MVP

**Goal**: Any effect/callable → full harmonic spectrum (magnitude + phase, arbitrary N), THD+N/SNR, with the exact Goertzel retained for known-bin checks.

**Independent test**: Run against analytic nonlinearities (symmetric→odd-only; biased→even+odd+DC); spectrum/THD+N/noise-floor match analytic references within tolerance; retained Goertzel reproduces exact known-bin values.

- [x] T008 [tier:balanced] RED: `tests/core/analysis-spectrum-test.cpp` — full spectrum magnitude+phase vs analytic harmonic signatures; out-of-band harmonics not-measured; sub-floor phase → NaN (FR-001/008, US1).
- [x] T009 [US1] [tier:balanced] `host/analysis/spectrum.h` — `harmonicSpectrum(in, fundamentalHz, numHarmonics)`: per-harmonic magnitude AND phase, arbitrary N, 1-based `at(k)`; GREEN T008.
- [x] T010 [tier:powerful] RED: `tests/core/analysis-thdn-test.cpp` — THD+N residual method + noise-floor/SNR vs analytic; no-fundamental → NaN (FR-002/008, US1).
- [x] T011 [US1] [tier:powerful] `host/analysis/thdn.h` — `thdPlusN(in, fundamentalHz)` residual (notch-fundamental) method, `noiseFloor`, `snr` referenced to fundamental, NaN sentinel; GREEN T010 (research Decision 4).
- [x] T012 [tier:balanced] RED: `tests/core/analysis-goertzel-parity-test.cpp` — retained exact integer-cycle Goertzel reproduces the current known-bin amplitudes (FR-007, US1).
- [x] T013 [US1] [tier:balanced] Wire the retained exact `goertzelBin(...)` known-bin path into `host/analysis/analysis-engine.h` (unwindowed, leakage-free), reusing the relocated `host/analysis/goertzel.h`; GREEN T012 (FR-007/010).

**Checkpoint**: US1 independently testable — the MVP offline characterization ships.

---

## Phase 4: User Story 2 — Intermodulation & aliasing characterization (Priority: P2)

**Goal**: Twin-tone IMD (SMPTE + CCIF) and an alias-vs-frequency sweep.

**Independent test**: Known nonlinearity + each twin-tone pair → difference/sum products within tolerance; swept tone through a naive nonlinearity → inharmonic curve rises past Nyquist, lower for a band-limited arm.

- [x] T014 [tier:powerful] RED: `tests/core/analysis-imd-test.cpp` — SMPTE (60+7000 Hz) and CCIF (19+20 kHz) difference/sum products vs analytic; product-bin coinciding with a harmonic attributed unambiguously (FR-003, US2).
- [x] T015 [US2] [tier:powerful] `host/analysis/imd.h` — `imd(fx, method)` twin-tone SMPTE/CCIF, difference AND sum products, unambiguous product attribution; GREEN T014 (research Decision 5).
- [x] T016 [tier:balanced] RED: `tests/core/analysis-alias-sweep-test.cpp` — inharmonic energy rises as harmonics fold past Nyquist; a band-limited arm is lower (FR-004, US2).
- [x] T017 [US2] [tier:balanced] `host/analysis/alias-sweep.h` — `aliasSweep(fx, sweep)` inharmonic-vs-frequency reusing the relocated `host/analysis/aliasing.h` integer-cycle measure; GREEN T016 (research Decision 6).

**Checkpoint**: US2 independently testable atop US1's engine.

---

## Phase 5: User Story 3 — Drive-dependent harmonic series (Priority: P2)

**Goal**: drive→THD and drive→per-harmonic curves as a first-class reduction (replaces ad-hoc `driveThdSeries`).

**Independent test**: Sweep drive on a known nonlinearity → drive→THD monotonic where the model predicts; per-harmonic curves match analytic references at sampled points.

- [x] T018 [tier:balanced] RED: `tests/core/analysis-drive-series-test.cpp` — drive→THD monotonicity + per-harmonic curves vs analytic (FR-005, US3).
- [x] T019 [US3] [tier:balanced] `host/analysis/drive-series.h` — `driveSeries(fxFactory, driveRange, numHarmonics)` returning drive→THD and drive→harmonic curves; GREEN T018.

**Checkpoint**: US3 independently testable.

---

## Phase 6: User Story 4 — Consolidate duplicated harmonic tooling (Priority: P2)

**Goal**: Fold the three labs' self-contained Goertzel + `meastest::` helpers onto `host/analysis/`; zero regression.

**Independent test**: Repointed harnesses produce identical harmonic tables / aliasing figures; no self-contained Goertzel remains; all prior suites green.

- [x] T020 [US4] [tier:balanced] Repoint `tests/core/measurement-support.h` (`meastest::`) onto the relocated `host/analysis/` building blocks (T007), and confirm the `tests/support/measurement/` re-exports carry every existing call site with no duplicate impl remaining (FR-007 preserved).
- [x] T021 [P] [US4] [tier:fast] Repoint `core/labs/waveshaping/harness/waveshaping-harness.cpp` at `host/analysis/`; delete its self-contained Goertzel; confirm the per-shape harmonic table is unchanged.
- [x] T022 [P] [US4] [tier:fast] Repoint `core/labs/saturation/harness/saturation-harness.cpp` at `host/analysis/`; delete its self-contained Goertzel and open-coded `driveThdSeries` (use `drive-series.h`); confirm harmonic + aliasing figures unchanged.
- [x] T023 [P] [US4] [tier:fast] Repoint `core/labs/oversampling/harness/oversampling-harness.cpp` at `host/analysis/`; delete its self-contained Goertzel; confirm the naive-vs-ADAA aliasing figures unchanged.
- [x] T024 [US4] [tier:balanced] Zero-regression gate: run all prior harmonic/aliasing suites (waveshaper/saturation/oversampler) and assert green; `grep -rn Goertzel core/labs/*/harness/` returns nothing (SC-003).

**Checkpoint**: one shared toolkit; no duplication; prior evidence intact.

---

## Phase 7: User Story 5 — Live harmonic readout (workbench + plugin) (Priority: P3)

**Goal**: RT-safe capture probe + a shared live readout (spectrum + running THD) in both host adapters, agreeing with the offline engine.

**Independent test**: In workbench and plugin, a known nonlinearity's live readout reflects its harmonic signature; the audio callback does only a bounded lock-free push; a live metric matches the offline figure within tolerance.

- [x] T025 [tier:powerful] RED: `tests/core/capture-probe-test.cpp` — SPSC ring correctness + deterministic overrun (counted, non-blocking) / underrun (hold, skip) (FR-011/013, US5).
- [x] T026 [US5] [tier:powerful] `core/primitives/analysis/capture-probe.h` — `CaptureProbeRing<Capacity>` lock-free SPSC; audio-path `push(block)` bounded copy + release store, no alloc/lock/math; acquire-side `drain`/`available`/`overrunCount`; GREEN T025 (research Decision 7; contract capture-probe-api.md).
- [x] T027 [P] [US5] [tier:balanced] Extend `tests/core/no-allocation-test.cpp` — assert `CaptureProbeRing::push()` allocates nothing on the audio path (SC-004).
- [x] T028 [tier:balanced] RED: `tests/core/analysis-live-offline-parity-test.cpp` — a metric captured through the probe → engine equals the direct offline engine result within a named tolerance (FR-015, US5).
- [x] T029 [US5] [tier:balanced] Author the shared live-readout implementation (drains the ring on a UI/timer thread at ~15–30 Hz overlapping windows, calls `host/analysis` for spectrum + running THD); GREEN T028's offline-parity assertion.
- [~] T030 [US5] [tier:balanced] Wire the shared readout into `adapters/workbench/` (spectrum + running-THD surface; audio thread owns a `CaptureProbeRing` and only `push`es).
- [~] T031 [P] [US5] [tier:balanced] Wire the same shared readout + capture probe into `adapters/plugin/` (one implementation, second host; desktop-only, never embedded) (FR-014/016).

**Checkpoint**: live readout in both hosts; one-engine parity proven.

---

## Phase 8: Polish & Cross-Cutting Concerns

- [ ] T032 [P] [tier:fast] Write `core/primitives/analysis/README.md` (RT-safety contract + overrun/underrun semantics) and `host/analysis/README.md` (host-only boundary, one-engine guarantee, hybrid FFT+Goertzel rationale).
- [ ] T033 [P] [tier:fast] FR-019: append the amendment note to `docs/superpowers/specs/2026-06-29-measurement-infrastructure-design.md` — Phase-2 off-thread FFT amends its Decision A; Phase 8 reuses/supersedes.
- [ ] T034 [tier:balanced] Final gate: run `scripts/check-portability.sh` + the full suite; confirm `host/analysis` and adapters are unreachable from portable `core/` (SC-006) and walk quickstart.md end-to-end.

---

## Dependencies & Story Completion Order

- **Setup (T001–T002)** → **Foundational (T003–T007)** → user stories.
- **US1 (T008–T013)** depends only on Foundational — the MVP; nothing else depends on US1 except by reuse of the engine.
- **US2 (T014–T017)** and **US3 (T018–T019)** depend on Foundational (+ US1's engine surface); independent of each other.
- **US4 (T020–T024)** depends on US1–US3 existing (it consolidates onto the shipped metrics, incl. `drive-series` for T022).
- **US5 (T025–T031)** depends on Foundational + US1 (engine) for the parity metric; the RT probe (T025–T027) is independent of the offline metrics and can start as soon as Setup is done.
- **Polish (T032–T034)** last.

## Parallel Opportunities

- T002 ∥ T001-follow-up; within Foundational, T005/T006 (window) ∥ T003/T004 (FFT).
- US2 and US3 phases can run in parallel once Foundational + US1 land.
- US4 repointing T021 ∥ T022 ∥ T023 (different harness files).
- US5: T027 ∥ the readout wiring; T031 (plugin) ∥ T030 (workbench) once T029 exists.
- Polish T032 ∥ T033.

## Implementation Strategy

- **MVP = Foundational + US1** (T001–T013): every nonlinear effect immediately gains full spectrum+phase, THD+N/SNR, and the exact retained Goertzel — objective characterization far beyond today's fixed-6-bin readout.
- **Increment 2**: US2 + US3 (IMD, alias-sweep, drive-series) — the measures that expose nonlinear misbehavior a single-tone THD misses.
- **Increment 3**: US4 consolidation — remove the duplication the gap exists to close.
- **Increment 4**: US5 live readout in workbench + plugin — the runtime face (broadest surface; last, depends on the engine).

## Task count

34 tasks — Setup 2, Foundational 5 (T007 = relocate + engine seam), US1 6, US2 4, US3 2, US4 5, US5 7, Polish 3.
