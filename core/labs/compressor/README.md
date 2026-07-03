> ============================================================================
> ‼ acfx COMMANDMENTS (non-negotiable) ‼
> 1. COMMIT AND PUSH EARLY AND OFTEN
> 2. NO GIT HOOKS, EVER
> 3. DESCRIPTIVE NAMES, NEVER NUMERIC PREFIXES
> (acfx Constitution, Principles I-III -- `.specify/memory/constitution.md`)
> ============================================================================

# Compressor — Lab

A dynamics processor decides how much gain to apply to a signal based on how
loud that signal is. This lab covers the two things that decision requires:
a **gain computer** — a stateless static curve mapping detected level to gain
change — and the **compressor effect** that composes the gain computer with
the shipped `EnvelopeFollower`, `SvfPrimitive`, and `DelayLine` primitives
into a full feedforward/feedback, lookahead-capable dynamics processor.

**Scope of this lab is both layers.** `GainComputer` is the pure curve (no
runtime state, no ballistics); `CompressorCore`/`CompressorEffect` are the
stateful composition that turns that curve into an audio-rate processor. The
detector itself — attack/release ballistics, RMS/peak/peak-hold, the dB-
domain convention — is out of scope here; it is the shipped, already-
graduated `EnvelopeFollower` (`core/labs/envelope-follower/README.md`,
`core/primitives/dynamics/envelope-follower.h`), reused rather than
re-derived.

## Theory

### The static gain-computer curve

`GainComputer::computeGainDb(levelDb)` is a pure function: given a detected
level in dB, it returns a gain change in dB (`≤ 0`, i.e. always attenuation
or unity — a compressor family never boosts on its own). Above/below the
threshold, each `GainMode` is a piecewise-linear map of the *output* level as
a function of the *input* level; the gain returned is `output − input`.

- **compress** — above threshold, output rises more slowly than input:

      out   = thr + (level − thr) / ratio      -- level > thr
      gain  = out − level

  Below threshold, `out = level` (unity, `gain = 0`). A ratio of `4:1` means
  a signal 10 dB over threshold comes out only 2.5 dB over.

- **limit** — the same shape as compress with `ratio → ∞`: the output is
  held flat at `thr` for any level above it (`out = thr`, `gain = thr −
  level`). Limiting is not a separate curve — it is compress's limiting
  case, which is why `GainMode::limit` exists as its own enumerator rather
  than asking callers to pass an enormous ratio number into `compress`: the
  degenerate "ratio at infinity" is exact and named, not approximated.

- **expand** — the *downward* mirror of compress, applied below threshold:
  the input is attenuated faster than it falls, bounded by a floor:

      out  = thr − (thr − level) · ratio,  clamped to  out ≥ thr + range
      gain = out − level                    -- level < thr

  (`range` is a negative dB value — the maximum attenuation the expander is
  allowed to apply, so quiet content doesn't get pushed to silence at any
  ratio.)

- **gate** — the extreme case of expand: below threshold minus knee, the
  signal is attenuated all the way to the `range` floor rather than along a
  ratio slope. Above threshold, unity, same as expand.

Both compress/limit (above-threshold, downward) and expand/gate
(below-threshold, downward-in-the-other-direction) are two ends of the same
idea: move the signal toward a target trajectory relative to `thr`, bounded
so the result never crosses back past unity or past `range`. That symmetry
is exactly what lets one soft-knee implementation serve every mode (next
section) instead of four.

### One knee, every mode

Every `GainMode` shares a **single unified quadratic, C¹-continuous** soft
knee straddling the threshold — not four separate per-mode knee formulas.
For compress/limit the knee interpolation sits **above** threshold (blending
the unity segment below into the ratio segment above); for expand/gate it
sits **below** threshold (blending the unity segment above into the downward
segment below) — same quadratic machinery, mirrored across the threshold
because those modes bend downward in the opposite direction. As `knee → 0`
the quadratic interpolation region shrinks to nothing and the curve reduces
exactly to the hard corner at `thr`: knee is a continuous parameter, not a
mode switch, and `knee = 0` is not a special case in the code, just the
limit of the general one.

The reason one knee formula suffices for every mode is that a knee is
nothing but "blend the two piecewise-linear segments across a small window
around the corner instead of joining them with a discontinuous slope
change." The quadratic form (the standard Reiss soft-knee interpolation) is
the cheapest curve that gives C¹ continuity — matched value *and* matched
slope at both edges of the knee window — with pure branch-and-multiply
arithmetic: no `log`, no `exp`, no transcendental call anywhere in
`computeGainDb`. That is what makes the primitive MCU-viable as well as
desktop-viable.

### Feedforward vs feedback: what the detector reads

`Detection::feedForward` is the obvious topology: the detector reads the
(optionally externally-keyed, optionally sidechain-filtered) **input**
signal. `Detection::feedBack` is different in a way that matters: the
detector reads the **compressor's own previous output sample**, but not
just any point in the output chain — specifically **post-makeup,
pre-mix**. That is, after the gain-reduction multiply and the makeup-gain
stage, but *before* the dry/wet mix blend and the final output-trim stage.

Why that exact tap point, and not the final output `y`: tapping after mix
would fold the dry blend into the feedback loop, so the effective ratio the
detector sees would depend on the mix knob — a knob that has nothing to do
with the compressor's dynamics response. Tapping before makeup would exclude
makeup gain from what the detector sees, so the effective threshold would
silently drift every time makeup changes (turn up makeup, and the detector
now sees a quieter signal than what's actually driving the ratio). Tapping
post-makeup/pre-mix is the one point where the loop's effective ratio stays
well-defined and independent of the two knobs (mix, output trim) that sit
outside the detection-relevant signal path. This is the "classic
optical/FET feel": the detector senses the *already-compressed* signal, so
the loop self-regulates and the effective knee/ratio is gentler than the
feedforward number in front of it, exactly as vintage feedback-topology
hardware compressors behave. The feedback read is a one-sample delay (last
processed output), not an extra buffer — cold start is a defined initial
value (silence/floor) so the loop has a well-formed starting condition
before the first real sample arrives.

### Where you smooth: level site vs gain site

Once a level is detected, *something* has to apply attack/release ballistics
before the result reaches the listener as a moving gain — but there are two
different places to put that smoothing, and they sound different:

- **`BallisticsSite::level`** — smooth the *detected level* first (the
  composed `EnvelopeFollower` does its normal attack/release job on the
  level), then feed the already-smoothed level through `GainComputer`,
  which is evaluated as an instantaneous function of whatever it's handed.
  The gain trajectory inherits its shape entirely from the detector's
  ballistics.

- **`BallisticsSite::gain`** — run the level detector close to
  instantaneous (fast/peak, effectively no averaging), evaluate
  `GainComputer` on that raw, unsmoothed level every sample, and then run a
  **second** `EnvelopeFollower` instance — this one smoothing the resulting
  gain-reduction signal itself — with attack/release applied there instead.

These are not equivalent, even though both end up producing a smoothly
moving gain: level-site smoothing rounds off the *detector*, so the gain
computer only ever sees a slow-moving input and its own nonlinearity (the
knee, the ratio slope) is applied to an already-averaged signal. Gain-site
smoothing lets the gain computer react to the instantaneous level — so a
knee or a hard ratio corner is evaluated against the true peak — and only
smooths *after* that nonlinear decision has been made. This is the
"smoothed-gain" reference variant in Reiss et al.: it is generally
considered to track transients more faithfully through the nonlinearity,
at the cost of a second ballistics stage. Both sites reuse the same
`EnvelopeFollower` primitive — a second instance of it for the gain site —
rather than inventing new smoothing code; the dB-domain convention, guard
clamps, and coefficient caching all carry over unchanged.

### Auto-makeup: a closed form, not a tracker

Manual makeup is a fixed dB value the caller sets. Auto-makeup is computed
**analytically**, not tracked at runtime:

    makeupDb = −computeGainDb(0 dBFS)

That is: ask the gain computer what it would do to a signal sitting at full
scale, and apply the exact opposite as makeup gain. A compressor with
threshold −20 dB and ratio 4:1 attenuates a 0 dBFS input by some fixed
amount; auto-makeup adds back precisely that amount, so a full-scale signal
comes back out at 0 dBFS regardless of threshold/ratio/knee — the curve and
its makeup track each other automatically as parameters change, with no
runtime state, no averaging window, and no separate "how loud is the
program material" estimate to get wrong. It is recomputed once whenever
threshold/ratio/knee/mode change (in the setter path, never per sample) —
FR-016 and SC-009 turn this into a closed-form analytic test rather than a
statistical one.

Auto-makeup is **zero for `expand`/`gate`**: those modes only ever pull
level *down* below threshold; there is nothing to compensate above
threshold, and inflating an already-downward-only curve with a positive
makeup gain would defeat the point of gating/expanding in the first place.

### Composing the shipped primitives — not re-deriving them

`CompressorCore` does not reimplement level detection, sidechain
filtering, or delay memory. It **composes** three already-graduated,
already-characterized primitives:

- **`EnvelopeFollower`** (`core/primitives/dynamics/envelope-follower.h`) —
  used twice: once as the **detector** (peak/RMS mode, dB domain, so time
  constants stay level-independent — see the envelope-follower lab's "why
  convert to dB before smoothing" argument) driving the level path, and
  again, a second independent instance, as the **gain-site smoother** when
  `BallisticsSite::gain` is selected. Both instances inherit the −120 dBFS
  floor, the branching/decoupled ballistics choice, and the RT-safety
  guarantees the envelope-follower lab already established — none of that
  is re-derived here.
- **`SvfPrimitive`** (`core/primitives/filters/svf-primitive.h`), in
  highpass mode, as the **sidechain filter**: it highpasses the detection
  path (the external key or the main input) before the detector ever sees
  it, so a compressor can be told to ignore low-frequency energy (the
  classic "de-ess"/"don't let the kick trigger the bass buss" move) without
  touching the main signal path at all. Cutoff `0` Hz is defined as bypass.
- **`DelayLine`** (`core/primitives/delays/delay-line.h`) as the
  **lookahead** buffer on the main (non-detection) path: the detector sees
  the signal early, the gain the detector computes is applied to a delayed
  copy of the same signal, so the gain reduction can arrive *before* the
  transient that provoked it, at the cost of `lookaheadSamples` of reported
  latency. The buffer is sized once in `prepare()` from the maximum
  lookahead the caller configures; `process()` never allocates.

Composing these three rather than writing three new bespoke pieces means
the compressor inherits detection ballistics, filter stability, and delay
correctness that are already measured and tested elsewhere — the only
genuinely new state machine in this feature is the gain computer's static
curve and the small amount of glue (key select → HPF → detect → curve →
gain-site smoother → makeup → lookahead → VCA multiply → feedback tap →
mix → output) that wires them together every sample.

## Walkthrough

The kernel that graduates out of this lab is a single header,
`gain-computer.h`, in `namespace acfx`:

```cpp
enum class GainMode : std::uint8_t { compress, limit, expand, gate };

class GainComputer {
public:
    void  setMode(GainMode) noexcept;
    void  setThreshold(float dB) noexcept;
    void  setRatio(float ratio) noexcept;    // guarded >= 1; limit treats as infinity
    void  setKnee(float dB) noexcept;         // 0 = hard corner; > 0 = unified quadratic knee
    void  setRange(float dB) noexcept;        // expand/gate attenuation floor, <= 0

    // Pure static curve: no runtime state, call-order independent.
    float computeGainDb(float levelDb) const noexcept;
};
```

`computeGainDb` is the whole contract: given the four configuration
setters' current values and a level in dB, it returns a gain change in dB.
Nothing about it depends on what sample came before — the acceptance test
for the static curve (SC-001/003/005) is a pure table of `(levelDb) →
(expectedGainDb)` pairs evaluated directly against the analytic formulas
above, no stimulus/response measurement required, because there is no state
to warm up.

One layer up, `CompressorCore` (`core/effects/compressor/compressor-core.h`)
is the stateful per-channel kernel that turns the curve into an audio
processor:

```cpp
enum class Detection      : std::uint8_t { feedForward, feedBack };
enum class BallisticsSite : std::uint8_t { level, gain };

class CompressorCore {
public:
    void  prepare(float sampleRate, int maxLookaheadSamples) noexcept;
    void  reset() noexcept;

    void  setMode(GainMode) noexcept;
    void  setThreshold(float dB) noexcept;
    void  setRatio(float ratio) noexcept;
    void  setKnee(float dB) noexcept;
    void  setRange(float dB) noexcept;
    void  setAttack(float seconds) noexcept;
    void  setRelease(float seconds) noexcept;
    void  setDetection(Detection) noexcept;
    void  setBallisticsSite(BallisticsSite) noexcept;
    void  setDetector(DetectMode) noexcept;
    void  setSidechainHpf(float hz) noexcept;   // 0 = bypass
    void  setLookahead(int samples) noexcept;
    void  setMakeup(float dB) noexcept;
    void  setAutoMakeup(bool) noexcept;
    void  setMix(float wet) noexcept;
    void  setOutput(float dB) noexcept;

    float process(float x, float key) noexcept; // key == x for keyless callers
};
```

The per-sample chain `process()` implements is, in order: select the
detection input (`key`, sidechain-filtered) or the feedback tap
(post-makeup/pre-mix previous output) → detect (`EnvelopeFollower`, dB
domain) → `GainComputer::computeGainDb` → the gain-site smoother, if
selected → makeup (manual or closed-form auto) → delay the main signal by
`lookaheadSamples` (`DelayLine`) → multiply the delayed signal by the
linear gain → record this as next sample's feedback tap → mix wet/dry →
apply output trim. `CompressorEffect`
(`core/effects/compressor/compressor-effect.h`) wraps one-or-per-channel
`CompressorCore`s in the shipped `SaturationEffect`/`SvfEffect` host-facing
idiom exactly: a single constexpr `ParameterDescriptor` table as the
parameter contract's single source of truth, a lock-free atomic
cross-thread parameter handoff consumed at the top of `process()`, and
stereo linking that derives one common gain from the cross-channel maximum
detector reading rather than running each channel fully independently.

The full public contracts — every method, every behavioral guarantee, and
its spec cross-reference — are recorded in
`specs/compressors/contracts/gain-computer-api.md` and
`specs/compressors/contracts/compressor-effect-api.md`; the rationale for
each design decision above and the alternatives considered are in
`specs/compressors/research.md` and the design record,
`docs/superpowers/specs/2026-07-02-compressors-design.md`. The canonical
reference for the static-curve and topology math throughout this document
is J. D. Reiss, B. Bendiksen, et al. (and the associated author list),
*"Digital Dynamic Range Compressor Design — A Tutorial and Analysis"*
(Journal of the Audio Engineering Society) — the same reference the
envelope-follower lab cites for the branching/decoupled ballistics split.

## Graduation status

Graduated. `GainComputer` now lives at
`core/primitives/dynamics/gain-computer.h`, moved there — unchanged in its
public contract — in a single atomic commit (`git mv
core/labs/compressor/gain-computer.h
core/primitives/dynamics/gain-computer.h`, Task T009) once its
implementation and static-curve tests passed. This is the same pattern
`envelope-follower.h` already went through, making `gain-computer.h` the
`dynamics/` category's **second** inhabitant. `CompressorCore`/
`CompressorEffect` are not lab-authored primitives; per the contracts
above, they live permanently in `core/effects/compressor/`, composing the
graduated `EnvelopeFollower`, `SvfPrimitive`, and `DelayLine` rather than
graduating themselves.

Per Constitution IX, this lab folder persists after graduation — this
README stays as the living theory record, and the host-only measurement
harness (`harness/compressor-harness.cpp`) stays here pointed at the
graduated primitive's new include path, printing qualitative evidence
(static curve samples per mode, knee continuity, feedforward-vs-feedback
step response, level-vs-gain ballistics-site comparison, and auto-makeup
unity at 0 dBFS) rather than fabricated numbers.

## How to run

The host-only harness, `harness/compressor-harness.cpp`, will drive
`GainComputer` and `CompressorCore` through static-level sweeps and
step/impulse stimuli once implemented, mirroring the envelope-follower
lab's harness:

```
export CPM_SOURCE_CACHE=external/.cpm-cache
cmake --build --preset test -j8
./build/test/acfx_lab_compressor_harness
```

The behavioral guarantees are asserted by the host test suites under
`tests/core/` (see `specs/compressors/tasks.md` for the full list, and
`specs/compressors/quickstart.md` for the scenario table): `gain-computer-
test.cpp` (static curve, all four modes, knee continuity), `compressor-
test.cpp` (feedforward/level-site compress, static map + attack timing),
`compressor-topology-test.cpp` (feedback fixed point, gain-site
smoothing), `compressor-sidechain-test.cpp` (HPF, external key),
`compressor-lookahead-test.cpp` (sample-accurate latency), `compressor-
makeup-link-test.cpp` (auto-makeup unity, stereo linking), and
`compressor-effect-test.cpp` (the `Effect` contract, parameter table,
thread-safe handoff). `make test` runs the whole suite.
