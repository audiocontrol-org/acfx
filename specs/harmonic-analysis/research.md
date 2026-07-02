# Phase 0 Research — Harmonic Analysis

All spec-level `[NEEDS CLARIFICATION]` were resolved in `/speckit-clarify` (spec § Clarifications, Session 2026-07-01). This file records the **technical method** decisions the plan rests on.

## Decision 1 — Shared engine home: `host/analysis/` (not `tests/support/`)

- **Decision**: The deep analysis engine lives in a new host-only library `host/analysis/`. The reusable building blocks (stimulus generators, `GoertzelAnalyzer`, `aliasingMeasure`, the capture seam) **relocate into `host/analysis/`**; `tests/support/measurement/` becomes thin **test-only re-exports** + assertion helpers. **Dependency direction: `tests/support → host/analysis` and `adapters → host/analysis`, never the reverse** — no product/adapter code reaches the test tree (closes analyze finding F1: reusing test-tree building blocks *from* host/analysis would have reintroduced the very product→test dependency this decision removes).
- **Rationale**: FR-014/FR-015 require the workbench and plugin adapters to run the **same** engine the offline tests use. Adapters are product code; depending on the `tests/` tree from product code is an architecture smell and would let test-only assumptions leak into shipped hosts. `host/` is the established desktop/host tree (`host/processor-node/` precedent), reachable by both `tests/` and `adapters/`, and — critically — **never reachable from portable `core/`** (Constitution IV). This preserves the one-engine guarantee cleanly.
- **Deviation flagged**: the approved design's architecture table named `tests/support/measurement/` as the engine home. This plan refines that placement to serve the design's own "one engine shared by tests + adapters" intent; the design's *intent* is honored, its literal path is not. Called out in plan § Structure Decision.
- **Alternatives considered**: (a) keep the engine in `tests/support/` and `#include` it from adapters — rejected (product→test dependency). (b) duplicate the engine into adapters — rejected (violates one-engine guarantee; two engines drift). (c) put the engine in `core/` — rejected (it allocates and is host-only; would violate Constitution IV/VI if reachable from portable core).

## Decision 2 — FFT: self-contained iterative radix-2, in-repo, power-of-two only

- **Decision**: Author a small iterative (Cooley-Tukey) radix-2 FFT in `host/analysis/fft.h`. No external dependency (no FFTW/KissFFT/PFFFT). Twiddle factors precomputed at engine `init()` (host-side alloc permitted off the audio thread). **Non-power-of-two transform length is rejected with a descriptive error** (FR-026) — never silently zero-padded.
- **Rationale**: matches the design's "small self-contained windowed radix-2 FFT" and the repo's no-new-dependency posture (cf. oversampling authored its halfband FIR in-repo rather than pulling DaisySP). Power-of-two-only keeps the transform simple and correct; rejecting non-pow2 (rather than zero-padding) honors Constitution V (no silent behavior that shifts bin frequencies / adds leakage the caller did not ask for).
- **Alternatives considered**: mixed-radix / Bluestein for arbitrary lengths — rejected as scope the concept doesn't need (callers choose a pow2 size deliberately). External FFT libraries — rejected (new dependency; Phase 8 convolution will reuse this in-repo FFT anyway, a forward seam per FR-018).

## Decision 3 — Window: selectable, default 4-term Blackman-Harris

- **Decision**: `host/analysis/window.h` provides a selectable window with **default 4-term Blackman-Harris** (~-92 dB sidelobes); **Hann** and **flat-top** also selectable. The retained integer-cycle Goertzel path stays **rectangular/unwindowed** (leakage-free by construction).
- **Rationale**: a distortion/noise-floor tool needs dynamic range to separate low-order harmonics and the noise floor from spectral leakage — Blackman-Harris delivers it. Hann (narrower main lobe) and flat-top (amplitude accuracy) are cheap to offer and genuinely useful for different measurements (clarify Q1).
- **Alternatives considered**: Hann-only (rejected — leakage limits noise-floor dynamic range); fixed Blackman-Harris (rejected — flat-top matters for amplitude-accurate readings).

## Decision 4 — THD+N: residual (notch-the-fundamental) method

- **Decision**: `host/analysis/thdn.h` computes THD+N = RMS(signal with the fundamental notched out) / RMS(fundamental). "Noise" = **all energy except the fundamental** (harmonics + broadband noise together). **SNR referenced to the fundamental level.** Unmeasurable (no fundamental) → NaN sentinel (FR-008), never 0.0.
- **Rationale**: the AES/standard THD+N definition; needs no separate noise-floor model to validate, and composes with the FFT spectrum (notch = zero the fundamental bin(s), sum the rest). Matches clarify Q3.
- **Alternatives considered**: modeled noise floor with THD and N reported separately — rejected (requires a defined noise-estimation method + more tests for marginal added value at this stage; can be layered later without breaking this contract).

## Decision 5 — IMD: twin-tone SMPTE + CCIF, difference/sum products

- **Decision**: `host/analysis/imd.h` drives SMPTE (60 Hz + 7000 Hz, 4:1) and CCIF (19 kHz + 20 kHz, 1:1) twin-tone stimuli and reports **difference and sum** intermodulation products (SMPTE: sidebands around 7 kHz, i.e. 7000±60·k; CCIF: the 1 kHz difference and higher-order 2f₁−f₂ / 2f₂−f₁ products). Product bins are read off the FFT spectrum; a product bin coinciding with a harmonic of either tone is attributed unambiguously (spec edge case).
- **Rationale**: SMPTE and CCIF are the two industry-standard IMD methods; both are directly measurable from the windowed spectrum. Matches FR-003 / clarify.
- **Alternatives considered**: DIN/other IMD standards — not requested; can be added on the same engine later.

## Decision 6 — Alias-vs-frequency sweep generalizes the integer-cycle inharmonic measure

- **Decision**: `host/analysis/alias-sweep.h` sweeps a tone across frequency; at each step it measures inharmonic (folded) energy reusing the shipped `aliasingMeasure` integer-cycle-window method, and returns an inharmonic-energy-vs-frequency curve.
- **Rationale**: reuses the exact, leakage-free integer-cycle inharmonic measure already validated for the naive-vs-ADAA comparison, sampled across a frequency sweep — the classic alias-sweep plot. No new spectral machinery required for the measure itself; the FFT provides the broadband view.
- **Alternatives considered**: a single broadband swept-sine with continuous aliasing extraction — rejected as harder to validate against analytic truth than per-step integer-cycle measures.

## Decision 7 — RT capture probe: fixed-capacity SPSC ring, deterministic overrun/underrun

- **Decision**: `core/primitives/analysis/capture-probe.h` is a single-producer/single-consumer ring buffer with a **fixed compile-time-sized** value buffer (capacity ≥ one FFT window + margin). Audio thread calls `push(block)` — a bounded copy advancing an atomic write index (release); the analysis thread reads via an atomic read index (acquire). **No heap, no locks, no math** on the audio path. **Overrun** (consumer behind): the producer never blocks/allocates — it advances and the dropped span is counted in an observable overrun counter. **Underrun** (fewer than a window available): the consumer holds and skips the update. Both conditions are observable to the readout (FR-013).
- **Rationale**: the standard lock-free live-analyzer bridge; honors Constitution VI absolutely (the audio path does one bounded copy). Fixed size keeps embedded RAM statically known.
- **Alternatives considered**: analysis inside `process()` (rejected in design Fork 2); a locking queue (rejected — locks on the audio path violate VI); a resizable ring (rejected — allocation).

## Decision 8 — Consolidation strategy (US4)

- **Decision**: The exact integer-cycle Goertzel, stimulus generators, and aliasing measure **relocate into `host/analysis/`** (Decision 1). The three lab harnesses (waveshaping, saturation, oversampling), `tests/core/measurement-support.h` (`meastest::`), and `tests/support/measurement/` are repointed at `host/analysis/`; ad-hoc self-contained Goertzel readouts are deleted and `tests/support/measurement/` re-exports the relocated building blocks so there is exactly one implementation of each metric. All previously green harmonic/aliasing suites must stay green (SC-003, zero-regression gate).
- **Rationale**: consolidation is the larger half of the gap (design). Repointing rather than rewriting keeps existing evidence intact and proves the shared engine reproduces prior numbers.
- **Alternatives considered**: leave the labs' Goertzel in place and only add new metrics — rejected (leaves the duplication the gap exists to remove).

## Decision 9 — Live/offline parity + tolerance

- **Decision**: The live readout and the offline tests call the identical `host/analysis` entry surface; a parity test (`analysis-live-offline-parity-test.cpp`) drives a known stimulus through the capture probe → engine and asserts the result equals the direct offline engine result within a named tolerance (windowing/overlap differences bounded). 
- **Rationale**: this is the operational proof of the one-engine guarantee (SC-005, FR-015).

## Decision 10 — FR-019 amendment note

- **Decision**: During implementation, append an amendment note to `docs/superpowers/specs/2026-06-29-measurement-infrastructure-design.md` recording that harmonic-analysis introduces an off-thread FFT in Phase 2, amending that design's Decision A (general FFT deferred to Phase 8) for this scope, and that Phase 8 partitioned convolution reuses/supersedes this FFT.
- **Rationale**: keeps the cross-phase decision trail coherent (FR-019); the off-thread lane removes the RT objection that motivated the original deferral.

## Defaults / parameters (from clarify)

- Default FFT size **8192** (~5.9 Hz/bin @ 48 kHz, ~170 ms window); live refresh **~15–30 Hz** with overlapping windows; ring capacity ≥ window + margin (FR-027).
- CSV report extension to the new metrics + a live snapshot dump (design OQ §6) is **deferred to a later pass** — FR-017 keeps `report.h` extendable and non-gating; not implemented in this feature unless a task explicitly adds it.
