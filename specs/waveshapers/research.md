# Phase 0 Research — Waveshapers

Consolidated technical decisions for the waveshapers primitive. Each entry records
the decision, the rationale, and the alternatives considered. The Technical Context
in `plan.md` has no unresolved `NEEDS CLARIFICATION`; this document records the
research that justified those resolutions and the analytic facts the test suite will
assert against. Sequencing of the first implemented catalog cut is deferred to
`/speckit-tasks` (an Open Question, not a blocker).

## Decision 1 — Transfer-function catalog and their analytic harmonic facts

**Decision**: Implement the catalog as pure `float→float` functions, each with a
documented closed-form definition and the analytic harmonic fact the suite asserts.

| Shape | Closed form (input `u`) | Symmetry / harmonics | Anchor facts asserted |
|---|---|---|---|
| tanh | `tanh(u)` | odd | monotone, range (−1,1), `f(0)=0`, odd `f(−u)=−f(u)` |
| arctan | `(2/π)·atan(u)` | odd | monotone, range (−1,1), odd |
| cubic soft-clip | `u−u³/3` clamped, hard limit beyond `|u|≥... ` | odd | `f(0)=0`, slope 1 at 0, odd |
| algebraic | `u/√(1+u²)` | odd | monotone, range (−1,1), odd |
| hard-clip | `clamp(u,−1,1)` | odd | exact ±1 saturation, odd |
| polynomial soft-knee | piecewise (linear core, smooth knee) | odd | continuity + C¹ at knee |
| Chebyshev-N | `T_N(u)` on `[−1,1]` | targets Nth harmonic | a unit sine maps to (predominantly) the Nth harmonic |
| biased/asymmetric | symmetric base + asymmetry (via wrapper bias and/or dual-curve) | even+odd | even harmonics present; DC handled by wrapper |
| diode-style | asymmetric exp/`tanh`-approx, e.g. `sign·(1−e^{−|·|})`-type | even+odd | monotone, asymmetric; documented as a *curve* (see Decision 6) |
| sine-fold | `sin(k·u)` style folding | rich, fold-dependent | bounded; harmonic count grows with fold depth |
| triangle-fold | reflective folding into `[−1,1]` | rich, fold-dependent | bounded; piecewise-linear folds |

**Rationale**: Pure functions are independently unit-testable against closed-form
truths (the `svf-reference` analytic-bound philosophy) and teachable in the lab.
Anchor facts (range, symmetry, monotonicity, key points) are exact analytic truths —
not fabricated numbers — satisfying Constitution V/X.

**Alternatives considered**: a single parametric "super-shape" (rejected — obscures
the per-shape harmonic pedagogy and analytic assertions); table-only definitions
(rejected — see Decision 4, closed-form is the reference).

## Decision 2 — Wrapper signal chain and bias-vs-drive order

**Decision**: `process(x): u = drive·x + bias; y = shape(u); y = dcBlock(y); y =
gainComp·y`. Bias is a **fixed offset applied after drive** (constant asymmetry point
independent of drive). DC-blocker is a wrapper member, never part of the shape
contract.

**Rationale**: Matches the analog grid-bias intuition (a fixed operating-point
offset); keeps the memoryless shape contract pure; the DC the bias introduces is
removed by the wrapper's blocker so downstream stages stay DC-free (FR-007/008).
Resolves the external-review ambiguity.

**Alternatives considered**: bias-before-drive `shape(drive·(x+bias))` (rejected —
asymmetry would scale with drive, a less predictable operating point); DC-block as a
free function (rejected — it is stateful, violating the memoryless contract).

## Decision 3 — DC-blocker

**Decision**: One-pole high-pass `y[n] = x[n] − x[n−1] + R·y[n−1]` with `R` near 1
(low cutoff). Cutoff fixed-by-default; "fixed vs parameter" remains an Open Question.

**Rationale**: Standard, cheap, RT-safe, one state variable; removes the bias-induced
DC without materially coloring the audio band. State lives only in the wrapper.

**Alternatives considered**: leaky integrator subtraction (equivalent); per-block mean
subtraction (rejected — block-rate, not sample-accurate, and not streaming-friendly).

## Decision 4 — Evaluation backends: closed-form reference + LUT

**Decision**: `closedForm` computes the shape directly; `lut` reads a precomputed,
fixed-size table (built in `init()`) with linear interpolation by default. **LUT error
is asserted against closed-form as ground truth** within a named bound for the chosen
resolution. Out-of-domain inputs clamp to the table edge (matching closed-form edge
behavior within tolerance) — a defined bounded policy, not a silent fallback.

**Rationale**: Closed-form is exact and portable (the reference); LUT gives uniform,
cheap per-sample cost on MCU targets where `tanh`/`exp` are expensive. Building the
table in `init()` keeps `process()` allocation-free (FR-011).

**Alternatives considered**: closed-form only (rejected by operator — MCU cost is a
present constraint); LUT only (rejected — loses the exact reference and bakes
interpolation error into every assertion); higher-order interpolation now (deferred —
table scheme per shape is an Open Question).

## Decision 5 — ADAA variant (anti-aliasing), strictly layered

**Decision**: A separate `ADAAWaveshaper` implementing first-order antiderivative
anti-aliasing `y[n] = (F(u[n]) − F(u[n−1])) / (u[n] − u[n−1])` (with the standard
small-`|u[n]−u[n−1]|` fallback to the direct `shape` midpoint to avoid the
0/0 singularity), where `F` is a shape's antiderivative. Second-order ADAA is captured
as an Open Question. The base memoryless contract and `Waveshaper` are unchanged; ADAA
wraps them. Shapes without an analytic antiderivative are documented **naive-only** and
the variant refuses them with a descriptive error rather than silently mis-shaping
(Constitution V).

**Rationale**: ADAA reduces aliasing without a separate oversampler and belongs to the
waveshaping concept; keeping it a separate type preserves the hard memoryless/stateful
split the design and review require.

**Alternatives considered**: ADAA folded into the base shaper (rejected — contaminates
the memoryless contract); rely solely on the oversampling sibling (rejected — discards
a distinct in-primitive technique; out of this item's hands anyway).

## Decision 6 — Diode-style curve boundary

**Decision**: Provide a memoryless diode-style transfer *curve*; document it explicitly
as distinct from the circuit-solved diode clipper (numerically solved I-V, stateful)
that `phase-circuit-modeling`'s `diode-clippers` owns.

**Rationale**: The curve is a standard, cheap, teachable member of the catalog; the
altitude distinction prevents the later circuit item from reading as a duplicate
(one-concept-at-a-time, FR-004).

**Alternatives considered**: omit any diode-named shape (rejected — omits a standard
catalog member); model the real diode here (rejected — that is the circuit phase).

## Decision 7 — Validation via the shipped measurement infrastructure

**Decision**: Reuse the Goertzel/THD analyzer + sine stimulus + allocation sentinel +
analytic-bound assertion pattern. Per-shape harmonic signatures and the naive-vs-ADAA
aliasing comparison are asserted against analytic truths + named tolerances. The
oversampled comparison arm is **contingent** (Decision 5 / FR-018) — not a promised
deliverable; if exercised at all it uses a throwaway, non-reusable, non-graduated
in-harness resampler.

**Rationale**: No new spectral engine is needed; the measurement-infrastructure design
explicitly anticipated nonlinear/harmonic reuse. A full FFT remains a later-phase
(convolution) concern.

**Alternatives considered**: a new FFT analyzer now (rejected — premature; Goertzel
single-bin harmonic readout suffices for known test tones).

## Decision 8 — Lab → primitive graduation mechanics

**Decision**: Author `core/labs/waveshaping/` (README theory + RT-safe kernel headers +
host-only harness). Graduate by `git mv`-ing the kernel headers into
`core/primitives/nonlinear/`, updating `#include` paths in tests and harness, and
keeping the lab as README + harness now driving the graduated primitive. Extend
`scripts/check-portability.sh` to cover both new locations (harness-isolation,
dependency-direction, platform-independence, file-size).

**Rationale**: This is exactly the three-layer-structure precedent (which proved the
retroactive migration with SVF); waveshaping proves the forward/greenfield path.
`git mv` preserves history and honors "evolve, don't re-derive" (Principle IX).

**Alternatives considered**: build directly in `primitives/nonlinear/` (rejected —
skips the lab, breaking the just-established graduation discipline); copy-then-delete
instead of `git mv` (rejected — loses provenance and risks divergence).
