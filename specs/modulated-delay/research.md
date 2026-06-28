# Phase 0 Research: Modulated Delay

**Feature**: [spec.md](./spec.md) | **Plan**: [plan.md](./plan.md) | **Date**: 2026-06-28

This phase resolves the open design decisions implied by the spec and its
clarifications, so Phase 1 contracts and the eventual implementation have no
unresolved unknowns. Each decision records what was chosen, why, and the
alternatives considered. No code is written here.

---

## Decision 1 — Fractional delay interpolation method

**Decision**: Use **linear interpolation** between the two samples bracketing the
fractional read position for both the main delay line and the wow & flutter line.

**Rationale**: Linear interpolation is allocation-free, branch-bounded, trivially
sample-rate independent, and cheap enough for the MCU targets. For modulated/swept
delay times its mild high-frequency loss is musically acceptable (and, for a tape
wow & flutter emulation, even desirable). It is the standard first choice for
modulated delays and is straightforward to test for accuracy (a known fractional
read of a ramp/impulse buffer has a closed-form expected value).

**Alternatives considered**:
- **Allpass interpolation** — flatter magnitude response, but its transient/state
  behavior under fast modulation can ring, and it carries state per delay line
  (more to reset). Deferred; can replace linear later behind the same `DelayLine`
  contract if higher fidelity is wanted.
- **Cubic / Hermite (4-point)** — better HF retention, but more per-sample work and
  more edge handling near buffer ends; not justified for MVP and tighter on MCU.

---

## Decision 2 — Feedback-loop topology and stability bound

**Decision**: Signal flow per sample: read the (fractionally) delayed sample →
filter it through the per-channel `SvfPrimitive` → scale by feedback → sum with the
current input → write to the delay line. The wet output is taken from the delayed
(post-filter or pre-filter — see below) signal and blended with dry via the mix
control. **Feedback is clamped to a stable maximum strictly below 1.0** (e.g.
≤ ~0.98) so author-commanded high feedback produces long, controllable tails
without uncommanded runaway; the in-loop filter (especially low-pass) further
bounds energy growth.

**Open sub-choice deferred to implementation (not blocking)**: whether the *wet tap*
is taken before or after the in-loop filter. Taking it post-filter means the first
echo is already filtered; taking it pre-filter means the dry-ish first repeat
brightens the early reflections. Default: **post-filter wet tap** (each audible
echo reflects the feedback filter, matching the spec's "each successive echo is
re-filtered" language). This is a wiring detail, fully covered by the effect
contract.

**Rationale**: Filter-in-the-feedback-path is exactly the requested architecture
(FR-003). Clamping feedback below unity is the standard guard against runaway and
satisfies FR-010 without a fallback (it is a defined bound, not silent mock
behavior). Reusing `SvfPrimitive` keeps the per-sample math identical to the proven
SVF effect (FR-005).

**Alternatives considered**:
- **Filter outside the loop (on the wet output only)** — simpler, but then echoes
  do *not* progressively re-filter; rejected because it contradicts the core intent.
- **Soft saturation in the loop instead of a hard feedback clamp** — musically nice
  (tape-style), but adds a nonlinearity to model/test; deferred as a possible later
  enhancement, not MVP.

---

## Decision 3 — LFO shapes, including "random"

**Decision**: The `Lfo` primitive generates a normalized output (range chosen in the
contract, e.g. bipolar [-1, 1]) from a phase accumulator advanced by `rate/sampleRate`
each tick, with a selectable shape: **sine, triangle, saw**, and **random**.
"Random" is a **smoothed/interpolated sample-and-hold** (a new random target each LFO
period, linearly interpolated toward it) so it is organic but click-free — not raw
white noise.

**Rationale**: A phase-accumulator LFO is allocation-free, sample-rate independent
(advance by `rate/sr`), and cheap. The smoothed random source gives the organic
warble/flutter character the author asked for without introducing discontinuities.
Determinism for tests is achieved with a seedable, RT-safe integer PRNG (e.g. a
small xorshift) — no `std::random` heap/locks in the audio path.

**Alternatives considered**:
- **Wavetable LFO** — needs a table (storage) and adds little for four simple shapes.
- **Raw sample-and-hold (no smoothing)** — clicks on each step; rejected for the
  audio path (smoothing is cheap and necessary).
- **`std::mt19937`** — not guaranteed allocation/lock-free and heavier; rejected for
  the audio path in favor of a tiny xorshift.

---

## Decision 4 — Modulation application granularity (avoiding zipper)

**Decision**: Tick each LFO and apply modulation **per sample** for the values that
feed the fractional delay read (delay-time modulation must be smooth at audio-visible
depths). Filter coefficient updates (cutoff/resonance) from their LFOs may be applied
**per small sub-block or per sample**; per-sample is the default for smoothness, with
a sub-block option available if MCU cost demands it. The chosen granularity must pass
the "no audible stepping across the supported rate/depth range" test (FR-016).

**Rationale**: Delay-time zipper is the most audible artifact, so its modulation is
per-sample. SVF coefficient recomputation is the heavier op; per-sample is fine on
desktop and likely on MCU, but the contract allows a documented sub-block cadence as
a tuning knob without changing observable behavior beyond smoothness.

**Alternatives considered**:
- **Block-rate modulation only** — cheapest, but steps audibly at higher rates;
  rejected as the default.

---

## Decision 5 — Delay-time smoothing on stepped (non-modulated) control edits

**Decision**: When the author changes the **delay time control** (a discrete edit,
not LFO motion), glide the effective base delay time toward the target with a
one-pole smoother before it feeds the fractional read, so a knob jump does not click
or zipper (FR-008). LFO modulation is summed on top of the smoothed base.

**Rationale**: Fractional reads already make small continuous changes click-free; a
one-pole smoother turns a large instantaneous control jump into a fast continuous
sweep, eliminating the discontinuity at negligible cost. This is the standard
delay-time-change treatment.

**Alternatives considered**:
- **Crossfade between two read taps on a jump** — higher fidelity for very large
  jumps but more state/logic; deferred. One-pole smoothing is sufficient for MVP.

---

## Decision 6 — Wow & flutter modeling

**Decision**: The wow & flutter stage owns its **own small interpolated delay line**
(tens of milliseconds nominal) on the input path and modulates its read position
with **two independent LFOs**: a slow **wow** (default range ~0.1–2 Hz) and a faster
**flutter** (default range ~5–12 Hz), each with its own author-controlled rate and
depth (FR-018). Their summed modulation displaces the read position, producing pitch
instability; with both depths at zero the read sits at the nominal tap and the input
passes through unmodulated (FR-019).

**Rationale**: Pitch modulation via a modulated delay (vibrato/chorus mechanism) is
the correct, allocation-free way to emulate tape wow & flutter and directly reuses
the new `DelayLine` and `Lfo` primitives. Placing it ahead of the main delay makes
the instability flow into the delayed/feedback signal too (FR-020). The default
rate ranges are conventional tape values; exposing rate+depth per the clarification
lets the author dial the character.

**Alternatives considered**:
- **Pitch-shifter-based wow/flutter** — far heavier and unnecessary; rejected.
- **Single combined LFO** — cannot represent the distinct slow-wow + fast-flutter
  texture the author asked for; rejected per the clarification.

---

## Decision 7 — Parameter time unit (`ParamUnit` has no ms/seconds today)

**Decision**: Add a **`seconds`** (or `milliseconds`) member to the existing
`ParamUnit` enum in `core/dsp/param-id.h` and use it for delay time, with a
**logarithmic** skew over the [min, 2.0 s] range so the control resolves musically
across short and long delays. LFO/wow/flutter rates use the existing `ParamUnit::hz`.
Depths use `ParamUnit::percent` or `ParamUnit::none` (normalized 0..1). Mix and
feedback use `ParamUnit::percent`/`none`.

**Rationale**: `ParamUnit` already enumerates `hz`, `decibels`, `percent`, `ratio`,
`none`; a time unit is a small, additive, backward-compatible extension that keeps
delay time self-describing for host metadata (adapters render the label). Logarithmic
skew matches how delay time is perceived. This is the only change to existing
`core/dsp` code and it is purely additive (no behavior change for existing effects).

**Alternatives considered**:
- **Reuse `ParamUnit::none` for delay time** — works numerically but loses the unit
  label in host metadata; rejected since the additive enum change is trivial and
  improves the single-source-of-truth table.

---

## Decision 8 — Per-channel memory budget for the 2 s buffer (embedded check)

**Decision**: Size the main delay buffer for **2.0 s at the prepared sample rate**,
allocated once in `prepare()`. At 48 kHz that is ~96k floats/channel (~384 KB);
at 96 kHz ~768 KB/channel. The wow & flutter line is a few thousand samples. On the
**embedded targets**, verify at implementation time that the chosen channel count ×
buffer size fits the device RAM; if a target cannot hold 2 s at its native rate, the
adapter/prepare bounds the buffer to the device-feasible maximum (a defined bound,
surfaced as a descriptive limit — not a silent fallback).

**Rationale**: The 2 s maximum is the clarified value (good desktop/plugin coverage).
Desktop/plugin RAM is ample. The embedded budget is the only real constraint and is
verified against the actual toolchain rather than assumed here (no fabricated
numbers, Constitution V).

**Alternatives considered**:
- **Fixed sample-count buffer independent of sample rate** — simpler allocation but
  the max delay *time* would shrink at higher sample rates, breaking sample-rate
  independence of the control (FR-015 spirit); rejected.

---

## Summary of resolved unknowns

| # | Topic | Decision |
|---|-------|----------|
| 1 | Interpolation | Linear (fractional) for both delay lines |
| 2 | Feedback topology | Read→filter→feedback-scale→sum→write; feedback clamped < 1.0; post-filter wet tap |
| 3 | LFO shapes | sine/triangle/saw + smoothed sample-and-hold "random"; xorshift PRNG |
| 4 | Mod granularity | Per-sample for delay time; per-sample (sub-block optional) for filter coeffs |
| 5 | Delay-time edits | One-pole smoothing of the base delay time on stepped control changes |
| 6 | Wow & flutter | Own short delay line + two LFOs (slow wow + fast flutter), each rate+depth |
| 7 | Time unit | Additively extend `ParamUnit` with a time unit; delay time log-skewed to 2 s |
| 8 | Memory | 2 s/ch sized at prepared SR; embedded fit verified at implementation, bounded if needed |

No `NEEDS CLARIFICATION` items remain. Ready for Phase 1.
