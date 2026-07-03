> ============================================================================
> ‼ acfx COMMANDMENTS (non-negotiable) ‼
> 1. COMMIT AND PUSH EARLY AND OFTEN
> 2. NO GIT HOOKS, EVER
> 3. DESCRIPTIVE NAMES, NEVER NUMERIC PREFIXES
> (acfx Constitution, Principles I-III -- `.specify/memory/constitution.md`)
> ============================================================================

# Program-Dependent Saturation — Lab

A **program-dependent** (or "dynamic") saturator is a saturator whose *character*
moves with the signal instead of sitting still. It is built by composing an
envelope detector with a static saturation nonlinearity so that how hard, how
asymmetric, how bright, or how blended the saturation sounds tracks how loud
the program material is from moment to moment — the same "hit it harder, get
more" (or, with a sign flip, "hit it harder, get gentler") behavior every
tube amp, tape machine, and opto compressor exhibits, and that a static
saturator with a fixed drive knob cannot.

This lab is the **envelope-modulates-nonlinearity** counterpart to the
compressor lab's **envelope-modulates-gain** theory. A compressor turns a
detected envelope into a *gain-reduction* number and multiplies the signal by
it. This feature turns the same kind of detected envelope into a set of
**signed saturation-parameter offsets** — small, bounded pushes added on top
of a saturator's static `drive`/`bias`/`tone`/`mix` — and hands those offsets
to the already-shipped, unmodified `SaturationCore`. Nothing new is invented
in the nonlinearity itself; what is new is the small stateless mapping from
"how loud right now" to "how much extra drive/bias/tone/mix right now."

## Theory

### The compressor analogy: envelope in, saturation-parameter offset out

Every dynamics processor in this codebase shares the same skeleton: detect a
level, shape that level through a static curve, and apply the result to the
signal. `EnvelopeFollower` (`core/labs/envelope-follower/`) owns the
detection half — peak/RMS/peak-hold modes, branching/decoupled ballistics,
a decibel-domain convention with a −120 dBFS floor — and is reused *as
shipped*, unmodified, by every dynamics-family effect built on top of it.
`GainComputer` (`core/labs/compressor/`) is the compressor's static curve:
`computeGainDb(levelDb) → gainChangeDb`, a pure function with no runtime
state. Program-dependent saturation's static curve is the new primitive this
lab authors, `DynamicsModulator`: `modulate(envNorm) → signedOffset`, equally
pure, equally stateless.

The difference between the two mappers is what they are for. `GainComputer`
answers "how much should I attenuate this signal," a single scalar with one
consumer (a VCA multiply). `DynamicsModulator` answers a more general
question — "how much should I nudge *this* saturation parameter" — because a
saturator does not have one knob, it has (at minimum) four: drive, bias,
tone, and mix, and a musically convincing dynamic character usually moves
more than one of them together. So where a compressor composes *one*
detector with *one* gain computer, `ProgramDependentSaturationCore` composes
**one shared detector with four independent `DynamicsModulator` instances**
— one per target — each reading the same envelope and each free to have its
own depth, sign, and response curve. The saturator's static parameters never
disappear: the dynamic layer is strictly additive on top of them (see
"Zero-depth orthogonality" below), so a program-dependent saturator with
every depth at zero is, exactly, the static saturator.

### The four modulation targets

`SaturationCore` (`core/effects/saturation/saturation-core.h`, composed here
**unchanged** — see "Composing shipped units, not re-deriving them" below)
exposes four parameters worth modulating, each producing a different flavor
of dynamic character:

- **Drive** — the pre-gain ahead of the nonlinearity (`setDrive`). Modulating
  drive is the direct, load-bearing case this feature exists for: louder
  input pushes more of the waveform past the shaper's knee, so harmonic
  content rises with level (positive depth) or, with a negative depth, the
  saturator *backs off* as the signal gets louder — a self-limiting,
  softening response reminiscent of a compressor's gain reduction, except
  applied to distortion character instead of level.
- **Bias** — the nonlinearity's DC offset before shaping (`setBias`).
  Because only the sign-asymmetric shapes (see the saturation lab's
  "Voicing" section) produce even-harmonic content from a bias offset,
  modulating bias with level shifts *which* harmonics dominate as the
  program gets louder — a distinctly different color change than more/less
  drive, and the mechanism behind "gets warmer" rather than "gets dirtier."
- **Tone** — the post-shaping spectral tilt (`setTone`). Modulating tone
  with level produces dynamic brightness: a signal that opens up (or, with a
  negative depth, darkens) as it gets louder, independent of how much
  distortion is actually being generated.
- **Mix** — the dry/wet parallel blend (`setMix`). Modulating mix with level
  is a *dynamic parallel-saturation* blend: quiet passages lean drier
  (cleaner), loud passages lean wetter (more character) or vice versa — the
  "self-compression feel" that tape and some optical saturators are known
  for, where the effect itself seems to breathe with the program material.

All four targets share the **same envelope** (a single shared `EnvelopeFollower`
instance, not four independent detectors — see "Update rate" and the MCU-
viability note below) and are otherwise fully independent: a positive drive
depth and a zero bias depth means drive alone moves, with no cross-talk onto
bias, tone, or mix.

### Signed depth and the three response curves

Every `DynamicsModulator` is configured by exactly two things: a **signed
depth** in `[-1, +1]` and a **response curve**. The sign of depth is the
*direction* control: a positive depth means "louder pushes the parameter up"
(more drive, brighter tone, more even harmonics, wetter mix); a negative
depth means "louder pulls the parameter down" — dynamic softening or
ducking, the mirror image of the same mechanism. The magnitude of depth
scales how far the parameter travels at full envelope. A depth of exactly
`0` is the orthogonality identity: `modulate` returns `0` for every envelope
value, so the modulated parameter equals the static base at all times (see
below).

The **response curve** (`ModCurve { linear, logarithmic, exponential }`)
shapes *how abruptly* the offset arrives as the envelope rises, independent
of depth. All three curves are functions of the normalized envelope
`n ∈ [0,1]` that satisfy the same two anchor constraints — `curve(0) = 0`
and `curve(1) = 1` — so that depth alone controls the endpoints and the
curve only reshapes the path between them:

- **`linear`** — `curve(n) = n`. The offset is directly proportional to the
  normalized envelope: a straight-line, "no editorializing" response.
- **`logarithmic`** — a **concave** map that reaches most of its offset
  quickly at low-to-moderate level and flattens out approaching full scale
  (early onset). This is the curve that reads as "grabs immediately, then
  eases off" — useful for a character that should already be present at
  moderate loudness and not keep climbing aggressively into the loudest
  peaks.
- **`exponential`** — a **convex** map that stays low through most of the
  range and rises sharply only near full scale (late onset). This is the
  curve that reads as "stays out of the way until it's loud, then commits" —
  useful for a character that should be nearly inaudible on quiet passages
  and arrive decisively on transients/peaks.

Because all three curves are monotonic, bounded, and anchored at the same
two points, swapping curves at a fixed depth changes only the *shape* of the
envelope→offset trajectory, never its endpoints — the linear, log, and
exponential trajectories are distinguishable everywhere in between `n = 0`
and `n = 1` but agree exactly at both ends.

### The normalized-dB-window envelope mapping

`DynamicsModulator` does not consume a raw level; it consumes a single
**normalized envelope** `envNorm ∈ [0,1]`, shared by all four target
mappers, that is derived once per sample (or per key sample, under external
sidechain) from the composed `EnvelopeFollower`. The derivation has two
steps:

1. **Detect in the decibel domain.** `EnvelopeFollower` runs its ballistics
   in dB (not linear amplitude) — the same "why convert to dB before
   smoothing" argument the envelope-follower lab makes: level *perception*
   is logarithmic, so attack/release behave consistently across the dynamic
   range only when the smoothing happens after the log conversion. The
   result is a dB-domain envelope, floored at −120 dBFS so silence never
   produces `-∞`.
2. **Normalize over a reference window.** That dB envelope is linearly
   normalized over a reference window — **default −60..0 dBFS** — and
   clamped to `[0, 1]`: `envNorm = clamp((envDb − loDb) / (hiDb − loDb), 0, 1)`.
   A signal sitting at the window floor (or below it) reads as `0`; a
   signal at or above the window ceiling reads as `1`. The window endpoints
   are a tunable default, not a hard-coded constant — a quieter mix or a
   hotter one can widen or narrow the window that "louder" is measured
   against.

The normalized envelope is then handed to each target's `DynamicsModulator`,
whose signed depth scales the *curve-shaped* normalized value into that
target's own **native span**, supplied by the caller (`ProgramDependentSaturationCore`),
not baked into the modulator itself:

```
offset = depth · span · curve(envNorm)
modulated = clamp(staticBase + offset, targetValidRange)
```

The native spans differ per target — drive's span is measured in dB,
bias/tone's span is a fraction of their `±1` range, mix's span is a
fraction of its `0..1` range — but because the modulator itself only ever
returns a **normalized** signed offset, one `depth` value reads consistently
across all four targets: "depth = 0.5" always means "half of that target's
configured span," never a different fraction depending on which parameter
it's attached to. This is exactly why the modulator is stateless and
target-agnostic: it does one universal job (shape a normalized envelope into
a normalized signed offset), and the caller — who already knows the
target's native units — does the unit conversion.

### Detection topology: feedforward versus feedback

`PdsDetection` selects **what signal the shared detector reads**, and the two
choices produce audibly different character:

- **`feedForward`** — the default. The detector reads the (optionally
  externally-keyed, optionally sidechain-filtered) **input** signal
  directly. This is the predictable topology: the modulation at sample `n`
  is a function of the input at sample `n` (through the detector's
  ballistics), with no dependency on what the saturator itself has been
  doing.
- **`feedBack`** — the detector reads the **previous final output sample
  `y`** — the value `SaturationCore::process()` actually returned last
  sample: post-mix, post-output-trim, the fully realized output. This is a
  deliberate, narrow choice, not an arbitrary one: `SaturationCore` is
  composed here **unchanged** (it exposes no intermediate tap between its
  internal stages), so the *only* value available to feed back without
  modifying the composed core is the one thing it already hands back to its
  caller — its final output. Reading the realized output has a second,
  free benefit: when the composed `SaturationCore` is running its
  oversampled quality tier (which introduces wet-path latency), the
  feedback tap automatically accounts for that latency, because it reads
  whatever `SaturationCore` actually, eventually produced — there is no
  separate latency bookkeeping to get right.

  Feedback is the topology behind vintage tube/tape saturators' "self-
  regulating" feel: because the detector is watching the *already-saturated*
  signal rather than the raw input, a loud transient that pushes drive up
  also pushes the detected level up, which (for a positive depth) pushes
  drive up further — but the loop is bounded by the modulated-parameter
  clamp (see "Extreme depth" below) and reads a one-sample-delayed value,
  so it settles to a fixed point for steady input rather than diverging.
  **Cold start** (no prior output sample yet) reads a defined initial
  output — silence/floor — never uninitialized state, so the very first
  sample of a feedback-topology stream has a well-formed starting condition.

### Update rate: per-sample versus per-block

Not every `SaturationCore` setter costs the same. `setDrive`, `setBias`, and
`setMix` are cheap scalar assignments — no coefficient recompute — so
recomputing and pushing their modulated values **every sample** costs
essentially nothing extra. `setTone`, by contrast, recomputes the tone-tilt
`SvfPrimitive`'s filter coefficients internally every time it is called
(`applyToneTilt()`), because tone is realized as a state-variable filter
tilt, not a scalar multiply. Running that coefficient recompute on every
single sample would be wasteful on desktop and outright MCU-hostile on an
embedded target.

The resolution: **drive, bias, and mix modulate per-sample**; **tone
modulates per-block** (control-rate) — the modulated tone offset is computed
once per processing block from a representative envelope sample and pushed
to `SaturationCore::setTone` once, rather than once per sample. This keeps
the audio path viable on constrained targets while preserving genuinely
per-sample dynamics on the three targets that can afford it; tone is also,
musically, the slowest-moving of the four characters (a spectral tilt, not a
transient-shaping control), so block-rate granularity is not expected to be
audibly coarser than per-sample tone modulation would have been. When a
target's depth is exactly zero, its modulated setter call is skipped
entirely — including the per-block tone call — so a neutral configuration
never performs redundant coefficient work on top of not changing the sound
(see "Zero-depth orthogonality" next).

### Zero-depth orthogonality: the dynamic layer is strictly additive

The load-bearing contract underneath all of the above is orthogonality:
**with every target depth at `0`, the program-dependent saturator is
*exactly* the static `SaturationEffect`** — same voicing, same drive, same
tone, same mix, same output, same bias, same quality behavior, within a
tight tolerance (byte-for-byte where the paths coincide). This holds
structurally, not by approximation, because `DynamicsModulator::modulate`
returns `0` for any envelope when depth is `0`; the pushed parameter is then
literally `staticBase + 0`, the same value the standalone static saturator
would have used. The detector and feedback machinery still run underneath
(the envelope is still detected, the feedback tap is still recorded) — they
simply have no effect on the output, because every offset they could
produce is multiplied by a depth of zero before it reaches the signal. This
is what lets an author dial in dynamics incrementally from a known-clean
starting point, and what lets this feature ship confidently alongside the
untouched static `SaturationEffect`: the dynamic layer can never silently
color a "clean" setting.

### Named dynamic characters: opto, vari-mu, tape-comp

Three recognizable, hardware-inspired characters are captured as named
**presets** — fixed, documented configurations of the same modulation
matrix described above, not new DSP:

- **`opto`** — modeled on optical compressors' famously slow, smoothed
  response: a **slow, level-smoothed** envelope (long ballistics) driving a
  **negative drive depth**, so the saturator's drive *softens* as the
  program gets louder — a gentle, program-dependent taming rather than an
  aggressive push, arriving gradually because the underlying detector
  itself is slow.
- **`variMu`** — modeled on vari-mu vacuum-tube compressors, whose gain
  element is a tube biased so that its operating point (and therefore its
  distortion character) shifts with signal level: a **positive bias depth**
  paired with a **positive drive depth**, so louder passages both push
  harder into the nonlinearity *and* shift its asymmetry — the combined
  drive-push-plus-bias-shift is what reads as "tube" rather than "generic
  distortion," because both the amount and the *kind* of harmonic content
  change together with level.
- **`tapeComp`** — modeled on tape machines' well-known self-compression:
  loud passages saturate the tape more (more harmonic content, i.e. more
  drive) while also naturally leaning further into the saturated/wet
  character rather than the clean pass-through (more mix) — a **positive
  drive depth** paired with a **positive mix depth**, under tape-ish
  ballistics, so the "breathing" character comes from two targets moving
  together rather than one.

Each preset is a single, fixed vector over the existing matrix parameters
(per-target depths and curves, detection topology, detector mode and
ballistics) — selecting a preset is a convenience that writes those
parameters in one move; it introduces no capability that manually dialing
the same matrix by hand could not already reach. `none` is the fourth,
neutral preset value: it leaves every depth at `0`, i.e. it is the static
saturator (the orthogonality baseline above). The concrete per-preset
numbers are recorded alongside the effect's parameter table once
implemented; this lab's job is the *mechanism* each preset configures, not
the tuning-pass constants themselves.

### Composing shipped units, not re-deriving them

Consistent with every other lab in this codebase, this feature invents
**exactly one** new piece of DSP — the stateless `DynamicsModulator`
mapper — and composes three already-shipped, already-characterized units
unchanged:

- **`SaturationCore`** (`core/effects/saturation/saturation-core.h`) — the
  nonlinearity itself: voicings, drive/bias/tone/mix/output, gain
  compensation, naive/ADAA/oversampled quality tiers. Nothing about its
  internal signal chain is modified; the dynamic layer only ever calls its
  existing `set*` methods with modulated values.
- **`EnvelopeFollower`** (`core/primitives/dynamics/envelope-follower.h`) —
  the single shared detector: peak/RMS/peak-hold modes, branching/decoupled
  ballistics, the dB-domain convention and its −120 dBFS floor, all reused
  exactly as the compressor lab reuses them.
- **`SvfPrimitive`** (`core/primitives/filters/svf-primitive.h`) — the
  optional pre-detector sidechain highpass, in highpass mode, exactly as
  the compressor lab uses it for the same purpose: keeping low-frequency
  energy from dominating the detected envelope (so bass content does not,
  by itself, dictate how much the saturator's drive moves) without
  touching the main saturation signal path at all.

Composing these three rather than re-deriving detection, filtering, or the
nonlinearity means this feature inherits ballistics correctness, filter
stability, and harmonic behavior that are already measured and tested
elsewhere. The only genuinely new state machine here is the small mapping
from a shared normalized envelope to four independent signed parameter
offsets, plus the glue that reads the detector, computes those offsets, and
pushes them into `SaturationCore` at the right rate.

## Lab structure and the graduation target

This lab's deliverable is the **Theory + Laboratory** stage of the
project's three-layer graduation model (Constitution Principle IX): this
README (the theory captured above), an RT-safe kernel header authored as
`core/labs/program-dependent-saturation/dynamics-modulator.h`, and a
host-only measurement `harness/` that drives that kernel (and, once it
exists, the composed effect) to produce orthogonality, THD-vs-level, and
step-response evidence — following the same lab shape every prior primitive
in this codebase has used (`envelope-follower/`, `compressor/`,
`saturation/`, `waveshaping/`, `state-variable-filter/`).

The kernel authored here is `DynamicsModulator` — the stateless
envelope→signed-offset mapper theorized above (`setDepth`, `setCurve`,
`float modulate(float envNorm) const noexcept`; see
`specs/program-dependent-saturation/contracts/dynamics-modulator-api.md`
for the full public-API contract). Once its implementation and static-curve
tests pass, it **graduates** — by `git mv`, refined in place, never
re-derived — into `core/primitives/dynamics/dynamics-modulator.h`. That
category folder already has two inhabitants, `envelope-follower.h` and
`gain-computer.h`; `dynamics-modulator.h` becomes the **third**. The same
atomic commit that performs the move also updates
`core/primitives/README.md`, moving the modulation mapper from a documented
prospectus family into an inhabited member of `dynamics/`, exactly as the
compressor lab's `gain-computer.h` graduation did.

Per Constitution IX, this lab folder **persists after graduation** — the
README stays as the living theory record, and the host-only harness stays
here, pointed at the graduated primitive's new include path, printing
qualitative evidence (curve samples per `ModCurve`, signed-direction
comparison, the zero-depth identity, and — once the composed effect exists —
orthogonality/THD-vs-level/feedback-convergence measurements) rather than
fabricated numbers.

`ProgramDependentSaturationCore` and `ProgramDependentSaturationEffect`,
the stateful composition kernel and host-facing wrapper that consume
`DynamicsModulator` alongside `SaturationCore`/`EnvelopeFollower`/
`SvfPrimitive`, are **not** lab-authored primitives themselves — like
`CompressorCore`/`CompressorEffect` before them, they live permanently in
`core/effects/program-dependent-saturation/`, composing the graduated
primitives rather than graduating themselves.

## How to run

Once the kernel and harness land (see "Lab structure" above), the harness
builds and runs the same way every other lab harness in this codebase does:

```
export CPM_SOURCE_CACHE=external/.cpm-cache
cmake --build --preset test --target acfx_lab_program_dependent_saturation_harness
./build/test/acfx_lab_program_dependent_saturation_harness
```

The behavioral guarantees themselves are asserted by the host doctest
suites under `tests/core/` — `dynamics-modulator-test.cpp` (curve shapes,
statelessness, the zero-depth identity), and, once the composed effect
exists, `program-dependent-saturation-orthogonality-test.cpp`,
`program-dependent-saturation-test.cpp`, `program-dependent-saturation-
matrix-test.cpp`, `program-dependent-saturation-topology-test.cpp`,
`program-dependent-saturation-presets-test.cpp`, `program-dependent-
saturation-sidechain-test.cpp`, and `program-dependent-saturation-effect-
test.cpp` — enumerated in `specs/program-dependent-saturation/tasks.md` and
`specs/program-dependent-saturation/quickstart.md`. The full public
contracts live in `specs/program-dependent-saturation/contracts/
dynamics-modulator-api.md` and `program-dependent-saturation-effect-api.md`;
the rationale for each design decision above is in
`specs/program-dependent-saturation/research.md` and the design record,
`docs/superpowers/specs/2026-07-02-program-dependent-saturation-design.md`.
