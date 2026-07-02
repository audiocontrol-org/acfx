> ============================================================================
> ‼ acfx COMMANDMENTS (non-negotiable) ‼
> 1. COMMIT AND PUSH EARLY AND OFTEN
> 2. NO GIT HOOKS, EVER
> 3. DESCRIPTIVE NAMES, NEVER NUMERIC PREFIXES
> (acfx Constitution, Principles I-III -- `.specify/memory/constitution.md`)
> ============================================================================

# Envelope Follower — Lab

An envelope follower (level detector) tracks the amplitude envelope of an
input signal: it turns an audio-rate waveform into a slower, smoothed
control-rate signal representing "how loud is this right now." It is the
foundational sidechain building block underneath every dynamics processor —
compressors, limiters, and gates all decide how much gain to apply based on
an envelope follower's output — and it is also the whole of a level meter
(VU, PPM) by itself, with no gain stage attached at all.

**Scope of this lab is the detector only.** `EnvelopeFollower` produces a
tracked level (linear amplitude or dB); it does not compute gain reduction,
apply a VCA, or do sidechain EQ/lookahead. Those live one layer up, in the
gain computer of a compressor/limiter/gate (`design:feature/compressors`,
out of scope here — see `specs/envelope-followers/contracts/envelope-follower-api.md`,
"Out of scope").

## Theory

### Detection modes

Three ways to reduce a bipolar audio signal to a non-negative level, each
matching a different class of downstream consumer:

- **peak** — instantaneous rectified level, `|x|`. The response is as fast
  as the smoother allows; nothing is averaged before smoothing. This is
  what a limiter or a peak meter (PPM-adjacent) wants: react to the true
  instantaneous excursion, not a smoothed average.
- **rms** — moving root-mean-square: square the input, run it through a
  one-pole leaky-integrator mean-square accumulator, then `sqrt` the
  result. This approximates the ear's/meter-ballistics' perceived loudness
  better than raw peak because it averages energy over a short window
  before the ballistics stage ever sees it. This is the VU-meter model.
- **peakHold** — latch the rectified peak `|x|` and hold it for a
  configured `hold` duration before the release ballistics are allowed to
  resume decaying it. A new, higher peak during the hold window updates
  the latch and restarts the hold timer. This is the classic PPM
  (peak-programme meter) behavior: fast attack, a dwell at the peak so the
  eye/ear can register it, then release.

As a rule of thumb: **VU ≈ RMS**, **PPM ≈ peak-hold**. Plain peak (no hold)
sits underneath both as the fastest, cheapest primitive detector.

### Ballistics topologies

Once a level is detected, it is smoothed by an attack/release ballistics
stage. The canonical reference for the topologies below is J. D. Reiss et
al., *"Digital Dynamic Range Compressor Design — A Tutorial and Analysis"*
(Journal of the AES), which distinguishes a **branching** (level) detector
from a **decoupled** (smooth) detector:

- **Branching** — a single state `env`. Every sample compares the newly
  detected level against the current envelope and picks one of two
  coefficients depending on direction:

      a   = (level > env) ? aAtk : aRel
      env = a·env + (1 − a)·level

  This is the cheapest possible ballistics stage (one state, one branch),
  but it has a well-known artifact: while the envelope is releasing
  (decaying) and a new transient arrives, the detector must first "catch
  up" through the release trajectory before the attack coefficient can
  engage, producing audibly wrong timing on fast, dense transient material.

- **Decoupled** — two states. A release-only smoother runs first and feeds
  an attack smoother:

      y1  = max(level, aRel·y1 + (1 − aRel)·level)
      env = aAtk·env + (1 − aAtk)·y1

  The `max()` in the release stage means `y1` snaps instantly to any level
  above its own decaying trajectory, so the attack stage downstream always
  sees a rising input in time to react with the attack coefficient — the
  release-then-attack artifact of the branching detector disappears.

- **Smooth variant** — for the decoupled topology, `setSmooth(true)` turns
  the *release* (first) stage into a one-pole smooth blend **at the
  release rate** (`aRel`) instead of the base decoupled's hard
  max-with-decay:

      base   (smooth_==false): y1 = max(level, aRel·y1)
      smooth (smooth_==true):  y1 = max(level, aRel·y1 + (1 − aRel)·level)
      env    (both cases):     env = aAtk·env + (1 − aAtk)·y1

  This is the Reiss "smooth decoupled peak detector." The attack stage
  always uses `aAtk`; only the release stage's smoothness changes — the
  release stage MUST stay governed by the release coefficient, never the
  attack coefficient (an earlier loose phrasing of this design, "attack
  coefficient in both stages," would incorrectly force release to run at
  the attack rate; corrected during implementation, see
  `specs/envelope-followers/spec.md` FR-005 and `research.md` Decision 4).
  The smooth blend removes the remaining discontinuity at the
  attack/release seam, trading a small amount of extra release-stage
  smoothing for a click-free transition. It is the modern default for
  musical compressors; plain (non-smooth) decoupled and branching remain
  available for the cheap, MCU-friendly cases (simple gates, minimal
  footprint).

  For the **branching** topology (a single stage) `setSmooth` has no
  additional stage to smooth and is a **no-op**.

Both topologies are first-class, enum-selected (`Ballistics::branching` /
`Ballistics::decoupled`), matching the existing `SvfMode`-style idiom in
this codebase rather than shipping two separate primitives.

### Time-constant math

Attack and release are each specified as a **time in seconds to reach
1 − 1/e (~63%) of a step** — the standard analog-RC time-constant
convention, chosen so parameter values match hardware/plugin intuition and
a measurement harness can assert step-response timing directly.

Given a time constant `τ` (seconds) and sample rate `fs` (Hz), the
equivalent discrete one-pole coefficient is:

    a = exp(−1 / (τ · fs))

and the per-sample smoother update is the standard leaky-integrator form:

    y[n] = a · y[n−1] + (1 − a) · x[n]

`a` is always in `[0, 1)`: `a → 0` as `τ → 0` (instant tracking, no
smoothing) and `a → 1` as `τ → ∞` (never moves). Every coefficient the
primitive uses (`aAtk`, `aRel`, `aRms`) is this same map applied to a
different `τ`.

**Coefficients are computed once, in the setters** (`setAttack`,
`setRelease`, `setRmsWindow`, and on `init`, which caches `fs` and
recomputes everything that depends on it) — never inside `process()`.
`process()` only ever multiplies by an already-cached `a`; the `exp()` call
never sits on the audio-rate path. This is a hard real-time-safety
requirement, not a style preference: a per-sample transcendental call is
exactly the kind of unbounded-cost work Constitution VI forbids in an
audio callback.

### RMS averaging

The RMS mean-square accumulator is itself a one-pole leaky integrator —
`meanSquare[n] = aRms·meanSquare[n−1] + (1 − aRms)·x[n]²` — with its own
time constant, `setRmsWindow(seconds)`, that is **independent** of the
attack/release ballistics. The two stages compose:

    level = sqrt(meanSquare)     -- how much to average before ballistics
    env   = smooth(level, aAtk, aRel)   -- how fast the envelope may move

This is deliberately two separate knobs rather than one. "How much energy
to average before the level is even meaningful" (the RMS window) and "how
fast the envelope is allowed to move once it has a level" (attack/release)
are different musical decisions a compressor designer tunes independently;
collapsing them into one time constant (e.g. deriving the RMS window from
`release`) would remove that control. The averaging is O(1) and
allocation-free — a scalar accumulator, not a ring buffer — matching the
same real-time constraint as every other stage.

### Detection domain

The detected/smoothed level can be returned in either domain:

- **linear** (`DetectDomain::linear`) — the base contract: a linear
  amplitude value, cheapest to compute, what limiters/gates/meters
  typically want directly.
- **decibel** (`DetectDomain::decibel`) — the level is clamped to a fixed
  **−120 dBFS floor**, converted with `20·log10(level)` **before** the
  ballistics smoother runs (not after), and the smoother then operates on
  the dB value itself. A level at or below the floor returns exactly
  −120 dB, never `-inf`.

Converting to dB *before* smoothing (rather than smoothing linear and
converting the result) is what gives dB-domain attack/release their
signature property: the same `attackSeconds` value produces the *same*
perceived attack time regardless of how loud the signal is, because a
fixed dB step is a fixed ratio in linear terms at any level. Smoothing in
the linear domain does not have this property — a step from silence to
full scale and a step 20 dB lower both take the same *linear* number of
seconds to settle, but they are perceptually very different attack speeds.
This level-independence is exactly what makes dB-domain detection the
compressor-feel default, while linear stays available as the cheaper,
simpler option (no `log10` on the hot path) for limiters, gates, and
meters that don't need it.

−120 dBFS is comfortably below 16/24-bit noise floors, so the clamp never
discards musically relevant low-level detail while still guaranteeing the
returned value is always finite.

**Usage contract — set the domain, then reset.** `setDomain()` only
switches which conversion `applyDomain()` performs; it does not by itself
re-baseline runtime state. The dB-domain silence baseline (`env == −120`)
is established by `clearRuntimeState()`, which only `init()` and `reset()`
call. Callers that need silence to read exactly −120 dB (rather than the
linear-domain initial value of `0`) MUST call `setDomain(DetectDomain::decibel)`
and then `reset()` before the first `process()` call — the same
init-then-configure-then-reset ordering `envelope-follower-db-test.cpp`
exercises. Once that baseline is established, silence reads −120 dB and
every subsequent `applyDomain()` call returns `20·log10(level)` clamped at
the −120 dB floor, as described above.

## Walkthrough

The kernel lives in a single header, `envelope-follower.h`, in
`namespace acfx`:

```cpp
enum class DetectMode   : std::uint8_t { peak, rms, peakHold };
enum class Ballistics   : std::uint8_t { branching, decoupled };
enum class DetectDomain : std::uint8_t { linear, decibel };

class EnvelopeFollower {
public:
    void  init(float sampleRate) noexcept;

    void  setMode(DetectMode) noexcept;
    void  setBallistics(Ballistics) noexcept;
    void  setSmooth(bool) noexcept;
    void  setDomain(DetectDomain) noexcept;
    void  setAttack(float seconds) noexcept;
    void  setRelease(float seconds) noexcept;
    void  setHold(float seconds) noexcept;
    void  setRmsWindow(float seconds) noexcept;

    void  reset() noexcept;
    float process(float x) noexcept;
};
```

`process()` implements the per-sample pipeline described above and in
`specs/envelope-followers/data-model.md` ("State transitions"):

```
detect: d = (peak)     |x|
          | (rms)      sqrt(meanSquare <- aRms*meanSquare + (1-aRms)*x^2)
          | (peakHold) latch/hold(|x|, holdCounter, heldPeak)
domain: s = (decibel)  toDb(clamp(d, -120dBFS)) : d
smooth: env = (branching) branch(s, aAtk, aRel)
             | (decoupled) attackStage(releaseStage(s, aRel[, smooth]), aAtk)
return env
```

Detect, domain conversion, and smoothing are three independently selected
stages (`mode` / `domain` / `ballistics` + `smooth`), so any combination —
e.g. `rms` + `decibel` + `decoupled` + `smooth` for a musical compressor
sidechain, or `peak` + `linear` + `branching` for the cheapest possible
limiter detector — is a valid, supported configuration rather than a
special case.

`init(sampleRate)` caches `fs` and clears runtime state; every `set*`
recomputes and caches whatever coefficients it affects (never a reset of
runtime state — changing `attackSeconds` mid-stream re-derives `aAtk` but
leaves `env` where it is). `reset()` clears only the runtime state
(`env`, `meanSquare`, `y1`, `heldPeak`, `holdCounter`) back to the defined
initial condition (silence: linear `env = 0`, dB `env = −120`), leaving
configuration untouched — the same init/reset split used by
`SvfPrimitive` and `Waveshaper`.

The full public contract, including per-guarantee spec references (attack
timing, release timing, RMS ripple, hold dwell, dB level-independence, the
RT-safety and numerical-safety invariants), is recorded in
`specs/envelope-followers/contracts/envelope-follower-api.md`; the
underlying design rationale and the alternatives considered for each
decision above are in `specs/envelope-followers/research.md` and the
design record, `docs/superpowers/specs/2026-07-01-envelope-followers-design.md`.

## Graduation status

`envelope-follower.h` has **graduated** from this lab to
**`core/primitives/dynamics/envelope-follower.h`** via `git mv`, in one
atomic commit that also updated `core/primitives/README.md` to move
`dynamics/` from a prospectus family to an inhabited category — this
primitive is the first inhabitant of `core/primitives/dynamics/`. The
public contract did not change across the move; only the include path
did (`core/labs/envelope-follower/envelope-follower.h` →
`core/primitives/dynamics/envelope-follower.h`).

Per Constitution IX, the lab folder persists after graduation: this
README (theory + walkthrough) and the host-only measurement harness
(`harness/envelope-follower-harness.cpp`) stay in
`core/labs/envelope-follower/`, pointing at the graduated primitive's new
location and serving as the living record of how and why the primitive
works — the same pattern already established by `core/labs/waveshaping/`
and `core/labs/state-variable-filter/`. There is no further graduation
step pending; the kernel is done, and this lab folder's remaining job is
documentation (this README) plus the desktop-only measurement harness
that exercises the graduated header and prints qualitative evidence
(attack/release timing versus configured values, branching-vs-decoupled
side-by-side samples, RMS settling to `A/√2`, peak-hold dwell length, and
the −120 dB floor/level-independence in the dB domain) rather than
fabricated numbers.

## How to run

The host-only harness, `harness/envelope-follower-harness.cpp`, drives
`EnvelopeFollower` through step, impulse, and sine stimuli and prints
attack/release, RMS-level, and peak-hold measurement evidence to stdout.
It is built desktop-side only and is never included by a portable unit.

```
export CPM_SOURCE_CACHE=external/.cpm-cache
cmake --build --preset test -j8
./build/test/acfx_lab_envelope_follower_harness
```

The behavioral guarantees are asserted by the host test suites under
`tests/core/`:

- `envelope-follower-test.cpp` — interface/peak/reset/edge cases
- `envelope-follower-ballistics-test.cpp` — attack/release timing;
  branching vs decoupled; smooth variant
- `envelope-follower-rms-test.cpp` — RMS level (`A/√2`) and settled ripple
- `envelope-follower-hold-test.cpp` — peak-hold dwell and restart-on-
  higher-peak
- `envelope-follower-db-test.cpp` — dB-domain level-independence and the
  −120 dBFS floor

See `specs/envelope-followers/quickstart.md` for the full scenario table
(setup, expected outcome, spec reference) and `make test` to run the whole
suite.
