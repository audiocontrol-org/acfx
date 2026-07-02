---
title: Harmonic Analysis — Nonlinear Characterization Tooling — Design Record
date: 2026-07-01
roadmap-item: design:gap/harmonic-analysis
status: approved
---

# Harmonic Analysis — Nonlinear Characterization Tooling

Close the **measurement gap** named (and deferred to) by three sibling Phase-2
designs: waveshapers, saturation, and oversampling each wrote *"deeper
nonlinear-specific harmonic tooling is the `harmonic-analysis` sibling's charter —
boundary noted."* This item is that sibling. It deepens how any acfx effect is
characterized in the harmonic domain, and gives that same characterization a
**live runtime face** in the workbench — without ever putting heavy analysis math
on an audio callback.

## Problem domain

Constitution **Principle X (Measurable Engineering)** requires every effect to be
validated by objective measurement. The shipped **measurement infrastructure**
(`design:feature/measurement-infrastructure`, `tests/support/measurement/`,
namespace `acfx::measure`) delivers exactly one spectral method: **single-bin
Goertzel on known integer-cycle test tones**. That method is leakage-free and
exact for a pre-known pure tone — but it can only read the bins you name in
advance, on a stimulus constructed so every harmonic and every folded image lands
on a DFT bin.

On disk today the consequences are visible:

- **Three labs re-derived the same code.** `core/labs/waveshaping/`,
  `core/labs/saturation/`, and `core/labs/oversampling/` each carry a
  **self-contained Goertzel harmonic readout** in their host harness (the
  waveshaping harness README even says "self-contained Goertzel"). There is no one
  shared harmonic toolkit; each new nonlinear effect will copy the pattern again.
- **Only pre-known bins are observable.** THD, per-shape/per-voicing harmonic
  signatures, and the naive-vs-ADAA inharmonic (aliasing) measure all require a
  carefully integer-cycle-windowed single tone. There is **no broadband spectrum**,
  no **THD+N** (harmonics *and* noise), no **intermodulation** measurement, no
  **alias-vs-frequency sweep**, and no **harmonic phase**.
- **Characterization is offline-only.** The infra is host-side/offline by
  contract. A DSP author cannot *hear-and-see* an effect's harmonic behavior live
  while sweeping a control in the workbench; they must run a test and read a table.

The prospectus lists **Harmonic analysis** as a first-class Phase-2 deliverable
alongside waveshapers/saturation/oversampling. The measurement-infrastructure
design explicitly parked a **general FFT** for Phase 8 (Convolution) — a deferral
this item revisits, because a live broadband display and THD+N/IMD change the
calculus (recorded as an open decision, not resolved here).

### Forces and constraints

- **Real-time safety (Principle VI).** No heap allocation, no locks, no unbounded
  work in any `process()` / audio-callback path. This forces the audio thread to do
  *only* a bounded, lock-free sample copy; all analysis math lives elsewhere.
- **Platform-independent core, thin adapters (Principle IV).** The one unit that
  touches the audio path must be embeddable (Daisy/Teensy) and must not depend on
  the host-only analysis engine. The engine and the live readout are host-only.
- **Strict typing, small modules (Principle VII).** New units stay within the
  ~300–500-line budget; no `any`/unchecked casts (TS side), clean C++ interfaces.
- **One concept at a time (Principle XI).** This item is *harmonic characterization
  tooling*. It measures aliasing; it does not reduce it (oversampling's charter). It
  may introduce an FFT; it does not build convolution (Phase 8's charter).
- **Reuse, don't fork.** The runtime analysis thread and the offline tests must run
  the **same** analysis engine, so a measured result is identical whether it came
  from a test or a live readout.

## Solution space

The design settled two structural forks in the brainstorm (nature; probe
threading). A third — the spectral engine — was captured unresolved in the
brainstorm per capture-over-YAGNI and then **resolved on review** as a hybrid; the
false either/or framing that motivated deferring it is documented in Fork 3.

### Fork 1 — Nature / where the tooling lives and runs

- **Chosen — Offline deepening + RT-safe runtime probe.** Extend the offline
  measurement harness *and* add a live analysis probe usable in the workbench. Two
  faces, one shared engine. This matches the "measurement gap" charter while
  answering the author's real need to see harmonic behavior live.
- **Rejected — Offline measurement only.** Extend `tests/support/measurement/`
  and stop there. Smaller and strictly within the existing host-side/offline
  contract, but leaves the author reading tables after the fact and never shows
  harmonic behavior while a control is swept. Deferring the runtime face to a later
  item would re-open this exact design. Rejected: the runtime face is core to the
  gap, and capture-over-YAGNI says capture it now.
- **Rejected — Runtime probe only.** Ship just the live readout and lean on the
  existing single-bin Goertzel for tests. Rejected: it strands the three labs'
  duplicated Goertzel code un-consolidated and leaves offline characterization as
  shallow as it is today — the larger half of the gap.

### Fork 2 — Runtime probe threading (relative to the audio callback)

- **Chosen — Audio thread copies; analysis runs off-thread.** `process()` performs
  only a bounded, lock-free `ring.push(block)` into a single-producer/single-consumer
  ring buffer (RT-safe: no alloc, no locks, no math). A separate UI/analysis (timer)
  thread drains the ring, windows the samples, and runs the full engine
  (FFT/Goertzel-bank → THD/spectrum/IMD). This is the standard live-analyzer
  pattern and keeps *all* heavy math off the audio thread, which in turn lets the
  engine allocate and run the FFT resolved in Fork 3.
- **Rejected — Analysis inside `process()`.** Compute running THD/spectrum estimates
  incrementally in the audio callback. No cross-thread FIFO, but every metric must
  be incrementally computable under hard RT constraints — which rules out
  block-windowed FFT and most of the richer measures (IMD, alias sweep, harmonic
  phase). Rejected: it caps the achievable analysis depth precisely where this item
  is trying to add depth.

### Fork 3 — Spectral engine (RESOLVED — hybrid: FFT for breadth, Goertzel for exactness)

This fork was captured unresolved in the brainstorm and **resolved on review**. The
framing "FFT-forward **vs** Goertzel-bank" was a false either/or: the two methods
are complementary, not competing, and the resolution keeps each where it is best.

- **Chosen — Hybrid.** Introduce a small, self-contained **windowed radix-2 FFT**
  in the host-side/off-thread analysis layer for everything that needs breadth: the
  broadband magnitude **and phase** spectrum, THD+N, twin-tone IMD, and the live
  workbench display — none of which can Goertzel a bin they do not know in advance.
  **Retain the exact single-bin Goertzel** for known-bin regression tests, where an
  integer-cycle window makes it leakage-free and exact against analytic tolerances
  (the shipped `svf-reference` pattern) — the FFT does not replace it there. The FFT
  lives only off the audio thread and offline; the RT boundary (Fork 2) is
  untouched. **This amends** the measurement-infrastructure design's Decision A,
  which deferred a general FFT to Phase 8 (Convolution) — see the amendment note in
  Decisions §4 and Open questions §7. Phase 8 (partitioned convolution) later
  reuses or supersedes this FFT; introducing it here is a forward seam, not a
  commitment to build convolution.
- **Rejected — Goertzel-bank only, no general FFT.** Honor the Phase-8 deferral
  fully: a bank of Goertzel filters for arbitrary harmonic sets, targeted IMD
  product bins, and THD+N via harmonic-sum-vs-total-power. Smaller and consistent
  with the prior cross-phase decision, but yields **no broadband sweep display** —
  only the bins you name — which guts the live-analyzer face this item exists to
  add. Rejected: the breadth cases (live spectrum, harmonic phase, THD+N over
  unknown content) are core to the gap, and a Goertzel-bank cannot serve them.
- **Rejected — FFT-only, drop the exact Goertzel.** Route *all* analysis, including
  known-bin regression tests, through the FFT. Rejected: it trades away the
  leakage-free exactness the existing regression suites depend on (integer-cycle
  Goertzel is exact where the FFT carries window leakage), for no benefit — the two
  coexist cheaply.

### Architecture (consequence of Forks 1–2)

Three homes, one RT boundary:

| Unit | Home | Constraint |
|---|---|---|
| **RT capture probe** — lock-free ring-buffer sample copy | `core/primitives/analysis/` (new primitive category) | Audio path: bounded, no alloc, no locks. Platform-independent, embeddable. |
| **Analysis engine** — spectrum, THD+N, IMD, aliasing-sweep, harmonic phase | `tests/support/measurement/` (extend `acfx::measure`) | Host-side/off-thread; may allocate. |
| **Live readout** — drains ring on a timer thread, runs the engine | `adapters/workbench/` | Desktop host only; never in the embedded audio path. |

The audio thread's only analysis-related work is `ring.push(block)`. The workbench
readout and the offline tests both call the *same* engine, so a live number and a
test number agree.

## Decisions

1. **Two faces, one engine.** Deepen the offline harness **and** add a live runtime
   probe; both consume a single shared analysis engine (Fork 1 → Chosen).
2. **RT boundary = a lock-free ring copy.** The only audio-path unit is an RT-safe
   SPSC ring-buffer probe in a **new `core/primitives/analysis/` category**; all
   heavy math is off the audio thread (Fork 2 → Chosen).
3. **Engine is host-side/off-thread.** The analysis engine extends `acfx::measure`
   in `tests/support/measurement/`, may allocate, and is reused verbatim by the
   workbench live-readout thread. It is never pulled into the embedded audio path.
4. **Spectral engine = hybrid (Fork 3, resolved).** A host-side/off-thread windowed
   radix-2 **FFT** serves broadband spectrum + harmonic phase, THD+N, IMD, and the
   live display; the **exact single-bin Goertzel is retained** for known-bin
   regression tests. Both live off the audio thread. **Amendment note:** this
   reverses the measurement-infrastructure design's Decision A (general FFT deferred
   to Phase 8) for the harmonic-analysis scope only. Rationale: an off-thread
   analysis lane removes the RT objection that motivated the deferral, and the live
   broadband/phase/THD+N faces cannot be served without it. Phase 8 (partitioned
   convolution) reuses or supersedes this FFT; the amendment is recorded back to the
   measurement-infra design during `/define` (Open questions §7).
5. **Consolidation is in scope.** Fold the three labs' self-contained Goertzel
   readouts and the `meastest::` harmonic helpers into the one shared toolkit, so
   future nonlinear effects reuse rather than re-derive.
6. **Captured capability set (scope, not YAGNI-cut).** THD+N and noise floor / SNR;
   twin-tone IMD (SMPTE 60+7000 Hz, CCIF 19+20 kHz, difference/sum products); full
   harmonic spectrum at arbitrary N including harmonic **phase** and rolloff;
   drive→THD / drive→harmonic curves as a first-class series (today ad-hoc
   `driveThdSeries`); alias-vs-frequency sweep generalizing today's single-tone
   inharmonic measure; the live workbench spectrum + running-THD readout. Sequencing
   is a planning concern, not a scope cut.
7. **Sibling boundaries drawn.** Oversampling (shipped) *reduces* aliasing; this
   item *measures* it. Phase 8 (Convolution) owns partitioned FFT convolution; any
   FFT introduced here is a forward seam Phase 8 reuses/supersedes, not a promise to
   build convolution.
8. **Portability gate extended.** `scripts/check-portability.sh` (CI, never a hook)
   learns `core/primitives/analysis/**` so its harness-isolation,
   dependency-direction, platform-independence, and file-size checks cover the new
   category. The host-only engine and adapter must not leak into portable core.

## Open questions

Captured per the capture-over-YAGNI house rule — parked for an explicit later
scoping pass (`/speckit-clarify` / planning), **not** discarded:

1. ~~**Spectral engine (Fork 3).**~~ **Resolved on review:** hybrid — off-thread FFT
   for breadth (broadband spectrum, phase, THD+N, IMD, live display) + exact Goertzel
   retained for known-bin regression tests (see Fork 3 / Decision 4).
2. **Window function(s) and default transform sizes** for the offline engine and the
   live readout (Hann vs Blackman-Harris; size vs latency/resolution trade-off).
3. **Does the live readout ship in the `plugin` adapter too, or workbench-only in
   this item?** Workbench-only is the smaller first cut.
4. **Ring-buffer sizing and the drain cadence** — how much audio the SPSC ring
   holds and how often the analysis thread wakes (resolution vs update-rate).
5. **How THD+N defines "noise"** — everything not at a harmonic bin vs a modeled
   noise floor; and the reference for SNR.
6. **CSV/report surface** — extend the existing opt-in `report.h` CSV for the new
   metrics, and whether the live readout can dump a snapshot.
7. **Reconciliation of the Phase-8 FFT deferral (Fork 3 resolved FFT-forward).**
   Write the explicit amendment note back to the measurement-infrastructure design's
   Decision A during `/define` — recording that harmonic-analysis introduces an
   off-thread FFT in Phase 2 and that Phase 8's partitioned convolution reuses or
   supersedes it. This is now an action item, not an open choice.

## Provenance

- **Roadmap:** `design:gap/harmonic-analysis` — `status: planned` at design start
  (this record advances it to `designing`), `depends-on:
  multi:feature/phase-digital-fundamentals`, `part-of:
  multi:feature/phase-nonlinear-dsp`. Design pointer set via `stackctl workflow
  link-design` to this file. Compass verdict `on-course` (planned → designing).
- **Constitution:** Principle X (Measurable Engineering) as the driver, with
  supporting Principles IV (Platform-Independent Core), VI (Real-Time Safety), VII
  (Strict Typing & Small Modules), IX (Progressive Layered Architecture), XI (One
  Concept at a Time). `.specify/memory/constitution.md`.
- **The gap, as named by its siblings:**
  `docs/superpowers/specs/2026-06-30-waveshapers-design.md` (Decision 8 —
  "Deeper, nonlinear-specific harmonic tooling is the `harmonic-analysis` sibling's
  charter — boundary noted") and
  `docs/superpowers/specs/2026-06-30-saturation-design.md` (Decision 8 — same
  boundary note).
- **Reused infrastructure:**
  `docs/superpowers/specs/2026-06-29-measurement-infrastructure-design.md`
  (Decision A — "Goertzel + sine-sweep, no FFT yet; a general FFT is deferred to
  Phase 8"; Decision B — assertions + opt-in CSV) and the shipped
  `tests/support/measurement/` headers (`stimulus.h`, `analyzers.h`, `metrics.h`,
  `aliasing.h`, `report.h`) plus `tests/core/measurement-support.h` (`meastest::`).
- **Program vision:**
  `docs/superpowers/specs/2026-06-29-acfx-progressive-dsp-prospectus.md` (Phase 2 —
  Nonlinear DSP: Waveshapers / Saturation / Oversampling / **Harmonic analysis**).
- **Pattern precedent:**
  `docs/superpowers/specs/2026-06-29-three-layer-structure-design.md` (lab kernel +
  host-only harness; portability-gate extension as the enforcement seam).
- **Design method:** `superpowers:brainstorming` driven in-session via the
  `/stack-control:design` frontend (house rules: capture-over-YAGNI, anchored
  record, handoff to `/stack-control:define`). Forks 1 and 2 resolved interactively;
  Fork 3 captured unresolved by operator direction, then **resolved on a
  third-party review pass** — the FFT-vs-Goertzel framing was corrected to a hybrid
  (off-thread FFT for breadth + exact Goertzel retained for known-bin tests), which
  amends the measurement-infra Phase-8 FFT deferral for this scope.
