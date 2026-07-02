# Research — Envelope Followers (Dynamics Level-Detector Primitive)

**Feature**: `specs/envelope-followers` | **Date**: 2026-07-02 | **Phase**: 0 (Outline & Research)

All spec-level NEEDS CLARIFICATION were resolved in the 2026-07-02 `/speckit-clarify` session; this
document consolidates the resulting technical decisions with rationale and the alternatives weighed,
and characterizes the single planning-level open question. Primary reference: J. D. Reiss et al.,
*"Digital Dynamic Range Compressor Design — A Tutorial and Analysis"* (branching vs decoupled,
level vs smooth detector topologies).

## Decision 1 — One-pole smoothing with the 1 − 1/e (~63%) seconds convention

- **Decision**: Attack/release are one-pole (leaky-integrator) smoothers with coefficient
  `a = exp(−1 / (τ · fs))`, where `τ` is the time in seconds to reach `1 − 1/e (~63%)` of a step. The
  update is `y[n] = a·y[n−1] + (1−a)·x[n]`. Coefficients are computed in `setAttack`/`setRelease`
  (and on `init`, which caches `fs`) and never in `process()`.
- **Rationale**: The `exp(−1/(τ·fs))` map is the standard analog-RC → digital one-pole equivalence;
  the 1 − 1/e convention is the conventional analog "time constant" so parameter values match hardware
  intuition and the measurement harness can assert step-response time directly (SC-001/002).
- **Alternatives considered**: (a) the cheaper approximation `a ≈ 1 − 1/(τ·fs)` — rejected as the
  reference form; it diverges for short `τ` (this is exactly the deferred low-fs concern, Decision 7).
  (b) A "time to reach X%" convention other than 1 − 1/e (e.g. time-to-90%) — rejected: 1 − 1/e is the
  analog-standard and keeps the harness tolerance model simple.

## Decision 2 — Detection modes: peak (|x|), RMS (one-pole mean-square → sqrt), peak-hold

- **Decision**: `DetectMode { peak, rms, peakHold }`. peak → `|x|`; rms → square, one-pole
  mean-square accumulate, `sqrt`; peakHold → latch `|x|` peaks with a hold timer before release. All
  three land in the first graduated cut (2026-07-02 clarification).
- **Rationale**: Covers instantaneous-peak (limiter/clip), program-level (compressor/VU), and
  latched-peak (PPM/transient) detection — the full sidechain-detector need of every downstream
  dynamics/metering consumer.
- **Alternatives considered**: peak-only or peak+RMS first (rejected in design/clarify — leaves the
  catalog half-built for the immediate compressor consumer).

## Decision 3 — RMS averaging: one-pole leaky integrator with an independent window

- **Decision**: The moving mean-square is a one-pole leaky integrator whose time constant is set by
  `setRmsWindow(seconds)`, **independent** of attack/release. In the linear domain the envelope is
  `sqrt(meanSquare)` fed into the ballistics smoother; the ballistics attack/release convention
  (Decision 1) is **identical** across modes and is not altered by the mean-square stage.
- **Rationale**: O(1), allocation-free, no ring buffer (Constitution VI, no `Storage`); matches the
  classic analog RMS-detector topology; an independent window decouples "how much averaging" from
  "how fast the envelope moves," which compressors tune separately.
- **Alternatives considered**: (a) release-derived window (fewer knobs, but couples averaging to
  release and removes explicit control); (b) true sliding-window RMS over a fixed buffer (textbook-
  exact but costs memory + a ring buffer, uncommon for real-time detectors). Both rejected in clarify.

## Decision 4 — Ballistics topologies: branching and decoupled, both smooth-capable

- **Decision**: `Ballistics { branching, decoupled }` with a `setSmooth(bool)` flag.
  - **Branching**: single state `env`; `a = (level > env) ? aAtk : aRel`; `env = a·env + (1−a)·level`.
  - **Decoupled** (base, `smooth_==false`): two states; a release stage `y1 = max(level, aRel·y1)`
    (hard max-with-decay) feeds an attack smoother `env = aAtk·env + (1−aAtk)·y1` — removing the
    branching detector's release-then-attack tracking artifact.
  - **Smooth decoupled** (`smooth_==true`): the release stage becomes a one-pole smooth blend at the
    **release** rate, `y1 = max(level, aRel·y1 + (1−aRel)·level)`, feeding the same attack smoother —
    the Reiss "smooth decoupled peak detector". (This corrects the design record's loose phrasing
    "attack coeff in both stages": the release stage MUST stay at the release rate, else release would
    run at the attack rate. The attack stage always uses `aAtk`; only the release stage's smoothness
    changes.) For the branching topology the flag is a no-op (single stage).
- **Rationale**: Branching is the cheapest single-state option (MCU targets, simple gates); decoupled-
  smooth is the modern compressor reference. Capturing both, enum-selected, matches the `SvfMode`
  idiom and serves both consumer classes without a second primitive.
- **Alternatives considered**: branching-only (artifact) or decoupled-smooth-only (drops the cheap
  MCU path) — both rejected in design/clarify.

## Decision 5 — Detection domain: linear base contract + decibel peer with a −120 dBFS floor

- **Decision**: `DetectDomain { linear, decibel }`. Linear returns a linear-amplitude envelope
  (base contract). Decibel clamps the detected level to a fixed **−120 dBFS** floor, converts
  `20·log10(level)` **before** the ballistics smoother, and returns the smoothed dB value; a level at
  or below the floor returns −120 dB (never −∞).
- **Rationale**: dB-domain smoothing gives amplitude-independent attack/release time constants (the
  compressor-feel property, SC-006); linear stays the cheap default for limiters/gates/meters and for
  MCU targets (the `log10` is opt-in). −120 dBFS sits below 16/24-bit noise floors, so the clamp never
  discards musically relevant level yet guarantees finiteness.
- **Alternatives considered**: linear-only (drops level-independence), log-only (forces per-sample
  `log10` on every consumer), and higher floors (−100/−60 dBFS — truncate legitimate low-level detail).
  All rejected in design/clarify.

## Decision 6 — Peak-hold at the detector stage (topology-independent)

- **Decision**: The hold is applied at the detector/latch stage, upstream of the ballistics smoother:
  a sample counter (derived once in `setHold`/`init`) freezes the latched peak; a new higher `|x|`
  updates the held value and restarts the counter; when the counter expires the release ballistics
  resume. Because it sits upstream of smoothing, it composes with both branching and decoupled.
- **Rationale**: Keeps hold (a detector concern) orthogonal to smoothing (a ballistics concern) — one
  implementation works for every topology, avoiding a combinatorial matrix of special cases.
- **Alternatives considered**: branching-only peak-hold in the first cut (rejected in clarify —
  unnecessary given the orthogonal placement).

## Decision 7 — (Deferred to implementation) Low-sample-rate coefficient accuracy

- **Open (planning/impl characterization, not scope)**: whether `a = exp(−1/(τ·fs))` needs a
  higher-order correction for very short `τ` at MCU sample rates (≤ 32 kHz), where `τ·fs` approaches a
  single sample and the one-pole's effective time deviates from the nominal 1 − 1/e.
- **Guardrail already fixed**: FR-018 bounds every coefficient to `[0, 1)` and forbids NaN/Inf, so the
  primitive is always *stable*; the only question is timing *accuracy* at the extreme, which the
  ballistics test can characterize and, if needed, correct during implementation. Not a spec ambiguity.

## Validation approach (reused infrastructure)

- **Decision**: Reuse the shipped measurement stimulus/response tooling (`tests/core/measurement-*`,
  `measurement-support.h`, `tests/support/svf-reference.h`) for step-response timing, sine-envelope
  levels, ripple, and dwell assertions, following the `svf-reference` named-tolerance pattern. A
  no-allocation test (allocation sentinel) covers the RT-safety invariant across every config.
- **Rationale**: The infrastructure already provides step/impulse/sine stimuli and analytic-bound
  assertion helpers; envelope tracking is a time-domain property it directly measures — no new
  measurement machinery is needed (that would overlap the `harmonic-analysis`/measurement charters).
- **Alternatives considered**: bespoke per-test stimulus generation (rejected — duplicates shipped
  infrastructure and diverges the tolerance model).
