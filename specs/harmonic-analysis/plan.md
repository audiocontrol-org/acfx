> ‼ **acfx COMMANDMENTS — non-negotiable** ‼
> **1. COMMIT AND PUSH EARLY AND OFTEN** — version control is a distributed, journaled
> filesystem that safeguards your work, **NOT a sacred rite reserved for the blessed.**
> **2. NO GIT HOOKS, EVER** — this repo uses zero git hooks; none exist, none get added.
> **3. DESCRIPTIVE NAMES, NEVER NUMERIC PREFIXES** — datestamps excepted.
> (acfx Constitution, Principles I–III — `.specify/memory/constitution.md`.)

# Implementation Plan: Harmonic Analysis — Nonlinear Characterization Tooling

**Branch**: `harmonic-analysis` | **Date**: 2026-07-01 | **Spec**: [spec.md](./spec.md)

**Input**: Feature specification from `specs/harmonic-analysis/spec.md` (approved design `docs/superpowers/specs/2026-07-01-harmonic-analysis-design.md`).

## Summary

Close the Phase-2 "measurement gap": deepen harmonic characterization beyond today's single-bin Goertzel-on-known-tones, and give it a live runtime face. Two faces over **one shared host-side analysis engine**:

1. **Offline** — a deepened analysis engine (windowed radix-2 FFT + retained exact Goertzel) computing full harmonic spectrum with phase, THD+N/SNR, twin-tone IMD (SMPTE + CCIF), alias-vs-frequency sweep, and drive→THD/harmonic series; consolidating the three labs' duplicated Goertzel readouts.
2. **Runtime** — a live spectrum + running-THD readout in the **workbench and the plugin**, fed by an RT-safe lock-free capture probe on the audio thread and computed off-thread by the *same* engine.

**The one load-bearing structure decision (Phase 0):** the shared engine lives in a new host-only library **`host/analysis/`**, not under `tests/support/` — so product adapters reuse it without depending on the test tree. The only audio-path code is the portable RT probe in `core/primitives/analysis/`.

## Technical Context

**Language/Version**: C++ (core compiles at both C++17 floor and C++20 desktop/test — same source, matching the saturation/SVF/oversampling precedent). The RT capture probe is portable core; the analysis engine and live readouts are host/desktop only.

**Primary Dependencies**: for the analysis engine — **none new/external**; the windowed radix-2 FFT is authored in-repo (no FFTW/KissFFT), consistent with the repo's no-new-dependency posture and the design's "small self-contained FFT". Reuses the shipped measurement stimulus/analyzer building blocks (`tests/support/measurement/` `SineGenerator`/`SweepGenerator`/`GoertzelAnalyzer`/`aliasingMeasure`, `tests/support/svf-reference.h` analytic-tolerance helpers, the allocation sentinel). For the live readouts: the existing `adapters/workbench/` and `adapters/plugin/` hosts. Testing: doctest.

**Storage**: N/A. FFT twiddle factors and window tables are computed at engine `init()` (host-side, allocation permitted off the audio thread); the RT probe's ring is a fixed-size value buffer.

**Testing**: doctest host-side unit/measurement suites under `tests/core/`; the three lab harnesses (waveshaping, saturation, oversampling) repointed at the consolidated engine; `scripts/check-portability.sh` extended to cover `core/primitives/analysis/**` as the explicit portability/layering gate (CI, never a hook — Principle II).

**Target Platform**: platform-independent core (Constitution IV) for the RT probe (desktop + DAW plugin + Daisy + Teensy from one source); the analysis engine and the live readouts are **host/desktop only** (workbench + plugin), never embedded.

**Project Type**: DSP library measurement/analysis tooling (single C++ core + host-only analysis lib + thin adapters).

**Performance Goals**: the audio-path RT probe `push(block)` is allocation-free, lock-free, bounded (Constitution VI) — a single-producer/single-consumer ring copy, no analysis math. The analysis engine runs off the audio thread; the live readout refreshes at ~15–30 Hz using overlapping 8192-pt windows (FR-027).

**Constraints**: no platform/vendor headers in `core/` (IV); no heap/locks/unbounded work on the audio path (VI); host-only analysis engine MUST NOT be reachable from portable `core/` (IV); strict typing, no `any`/unchecked casts, files ~300–500 lines (VII); every new metric asserted against an analytic reference within named tolerances, never fabricated exact figures (V, X); unmeasurable → NaN sentinel not 0.0 (V); non-power-of-two FFT length rejected with a descriptive error, never silently zero-padded (V, FR-026); this MEASURES aliasing only — no oversampling (XI); introduces an FFT but builds no convolution (XI).

**Scale/Scope**: one portable RT capture probe (`core/primitives/analysis/`); one host analysis engine (`host/analysis/`: FFT + windows + spectrum + THD+N/SNR + IMD + alias-sweep + drive-series), consolidating the three labs' Goertzel; two live host readouts (workbench + plugin) sharing one implementation; the measurement-suite consolidation; one portability-gate extension; and the FR-019 amendment note back to the measurement-infrastructure design. Constitution Check re-evaluated post-design below.

## Constitution Check

*GATE: Must pass before Phase 0 research. Re-check after Phase 1 design.*

| Principle | Status | How this plan satisfies it |
|---|---|---|
| I. Commit & push early/often | PASS | Atomic commits per artifact/task, pushed promptly; already practiced through design/spec/clarify. |
| II. No git hooks | PASS | Enforcement is `scripts/check-portability.sh` (explicit CI step), never a hook. |
| III. Descriptive names | PASS | `specs/harmonic-analysis`, `core/primitives/analysis/`, `host/analysis/` — no numeric prefixes. |
| IV. Platform-independent core | PASS | RT probe uses only in-repo + stdlib headers (embeddable); the host-only analysis engine and readouts live in `host/`+`adapters/`, gate-asserted unreachable from `core/`. |
| V. No fallbacks/mock data | PASS | Unmeasurable → NaN sentinel (FR-008); non-pow2 FFT → descriptive error (FR-026); every metric validated against real measured audio + analytic references, no mock data. |
| VI. Real-time safety | PASS | Audio path is a bounded lock-free ring `push` only — no alloc, no locks, no math (FR-011); asserted by the allocation sentinel (SC-004). All FFT/analysis is off-thread. |
| VII. Strict typing & small modules | PASS | No `any`/casts; engine split into per-concern headers (fft, windows, spectrum, thdn, imd, alias-sweep, drive-series) within ~300–500 lines each (FR-021). |
| VIII. Test core host-side | PASS | doctest suites validate every metric host-side against analytic references; the retained Goertzel path reproduces exact known-bin values (no regression). |
| IX. Progressive layered architecture | PASS | Measurement tooling layer: portable RT primitive (`core/primitives/analysis/`) + host analysis lib + host adapters; dependencies point inward (adapters→host/analysis; nothing→core from host). |
| X. Measurable engineering | PASS | Full spectrum+phase, THD+N/SNR, IMD, alias-sweep, drive-series, and live/offline agreement are the acceptance evidence (SC-001..006). |
| XI. One concept at a time | PASS | Harmonic *characterization* is the single concept; it measures aliasing (not oversampling) and introduces an FFT (not convolution) — boundaries FR-018. CSV extension (design OQ §6) deferred to a later pass, not cut. |

**Result: PASS — no violations.** Complexity Tracking is empty.

> **Watch item (not a violation):** the workbench **+ plugin** reach (clarify Q4, operator-chosen over the workbench-only recommendation) adds a second adapter surface. Both share ONE readout implementation and ONE engine (FR-014), so the added surface is a thin second host binding, not a second engine — Principle XI holds.

## Project Structure

### Documentation (this feature)

```text
specs/harmonic-analysis/
├── plan.md              # This file (/speckit-plan output)
├── research.md          # Phase 0 — engine home decision, FFT/window method, THD+N/IMD/alias-sweep methods, RT-ring design, consolidation strategy
├── data-model.md        # Phase 1 — entities: capture-probe ring, analysis engine, spectrum, THD+N/SNR, IMD result, alias curve, drive series
├── quickstart.md        # Phase 1 — how to build/run the suites + harnesses + live readouts
├── contracts/           # Phase 1 — public C++ API contracts
│   ├── capture-probe-api.md      # core/primitives/analysis RT-safe SPSC ring
│   └── analysis-engine-api.md    # host/analysis engine surface (FFT, windows, spectrum, THD+N, IMD, alias-sweep, drive-series)
├── checklists/
│   └── requirements.md  # spec quality checklist (already created; markers resolved)
└── tasks.md             # Phase 2 output (/speckit-tasks — NOT created here)
```

### Source Code (repository root)

```text
core/
├── primitives/
│   └── analysis/                         # NEW portable category (RT-safe, embeddable)
│       ├── capture-probe.h               #   SPSC lock-free ring; audio-path push(block) only — no alloc/lock/math
│       └── README.md                     #   the RT-safety contract + overrun/underrun semantics
│   (existing: nonlinear/, filters/, oversampling/ — unchanged)

host/
├── processor-node/                       # existing (unchanged)
└── analysis/                             # NEW host-only analysis engine (may allocate; off any audio thread)
    ├── fft.h                             #   windowed radix-2 FFT; rejects non-pow2 length (FR-026)
    ├── window.h                          #   selectable window; default Blackman-Harris (+Hann, flat-top) (FR-025)
    ├── spectrum.h                        #   full harmonic spectrum: per-harmonic magnitude AND phase, arbitrary N (FR-001)
    ├── thdn.h                            #   THD+N residual method + noise floor/SNR vs fundamental (FR-002)
    ├── imd.h                             #   twin-tone IMD: SMPTE + CCIF, difference/sum products (FR-003)
    ├── alias-sweep.h                     #   alias-vs-frequency sweep (generalizes integer-cycle inharmonic) (FR-004)
    ├── drive-series.h                    #   drive→THD / drive→harmonic first-class series (FR-005)
    ├── analysis-engine.h                 #   the single entry surface tests + adapters share (FR-006/014/015)
    └── README.md                         #   host-only boundary; one-engine guarantee; hybrid FFT+Goertzel rationale

adapters/
├── workbench/                            # MODIFIED — live readout host (spectrum + running THD)
│   └── (live-readout wiring: owns capture-probe, drains ring on UI/timer thread, calls host/analysis)
└── plugin/                               # MODIFIED — same live readout (shared impl), same capture-probe (FR-014)

tests/
├── support/
│   ├── measurement/                      # CONSOLIDATED — exact Goertzel/stimulus/aliasing retained for known-bin
│   │   │                                 #   tests; the deep engine graduates to host/analysis and is re-used here
│   │   └── (stimulus.h, analyzers.h, metrics.h, aliasing.h, report.h — repointed onto host/analysis where superseded)
│   └── svf-reference.h                   # existing analytic-tolerance helpers (reused)
└── core/
    ├── measurement-support.h             # CONSOLIDATED — meastest:: harmonic helpers fold onto host/analysis
    ├── analysis-fft-test.cpp             # NEW — FFT correctness (analytic tones), pow2 rejection (FR-009/026)
    ├── analysis-window-test.cpp          # NEW — window shapes / sidelobe behavior; default = Blackman-Harris (FR-025)
    ├── analysis-spectrum-test.cpp        # NEW — full spectrum magnitude+phase vs analytic signatures (FR-001, US1)
    ├── analysis-thdn-test.cpp            # NEW — THD+N residual + noise-floor/SNR vs analytic (FR-002, US1)
    ├── analysis-imd-test.cpp             # NEW — SMPTE + CCIF difference/sum products vs analytic (FR-003, US2)
    ├── analysis-alias-sweep-test.cpp     # NEW — alias-vs-frequency rises past Nyquist; band-limited arm lower (FR-004, US2)
    ├── analysis-drive-series-test.cpp    # NEW — drive→THD monotonicity + per-harmonic curves (FR-005, US3)
    ├── analysis-goertzel-parity-test.cpp # NEW — retained Goertzel reproduces exact known-bin values (FR-007, US1)
    ├── capture-probe-test.cpp            # NEW — SPSC ring correctness + overrun/underrun determinism (FR-011/013)
    ├── analysis-live-offline-parity-test.cpp # NEW — live-captured metric == offline metric within tol (FR-015, US5)
    └── no-allocation-test.cpp            # EXTEND — capture-probe.push() no-alloc on the audio path (SC-004)

scripts/check-portability.sh             # EXTEND — cover core/primitives/analysis/** (platform-free, harness-free,
                                         #   no host/analysis or adapter reach back into core)

docs/superpowers/specs/2026-06-29-measurement-infrastructure-design.md  # MODIFIED — FR-019 amendment note
                                         #   (Phase-2 off-thread FFT amends its Decision A; Phase 8 reuses/supersedes)
```

**Structure Decision**: Single C++ core with the established taxonomy, plus a new **host-only analysis library**. Three homes, one RT boundary (from the approved design, refined by Phase-0 research):

- **`core/primitives/analysis/`** (NEW portable category) — the RT-safe SPSC capture probe. The *only* audio-path unit; `push(block)` is bounded, lock-free, allocation-free, math-free; embeddable on Daisy/Teensy. Gate-asserted platform-free.
- **`host/analysis/`** (NEW host-only library) — the single deep analysis engine (self-contained windowed radix-2 FFT + selectable window + retained exact Goertzel + spectrum/THD+N/IMD/alias-sweep/drive-series), split across per-concern headers to honor the ~300–500-line budget (VII). May allocate; runs only off the audio thread. **This is the deviation from the design's literal "tests/support/measurement/" placement** — chosen so `adapters/{workbench,plugin}` reuse the engine without a product→test-tree dependency (Phase-0 Decision 1). The existing `tests/support/measurement/` stays as the test-facing stimulus/known-bin layer and is consolidated onto `host/analysis/`.
- **`adapters/workbench/` + `adapters/plugin/`** (MODIFIED) — each owns a capture probe, drains its ring on a UI/timer thread, and calls the shared `host/analysis` engine; one readout implementation shared by both (FR-014). Host/desktop only; the engine is never reachable from `core/`.

Dependencies point inward: `adapters → host/analysis → (nothing in core)`; `core/primitives/analysis` depends on nothing host-side. This preserves the one-engine guarantee (a live number and a test number agree because both call `host/analysis`) while keeping the portable core clean.

## Complexity Tracking

> No Constitution Check violations — this section is intentionally empty.
