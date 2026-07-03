> ============================================================================
> ‼ acfx COMMANDMENTS (non-negotiable) ‼
> 1. COMMIT AND PUSH EARLY AND OFTEN
> 2. NO GIT HOOKS, EVER
> 3. DESCRIPTIVE NAMES, NEVER NUMERIC PREFIXES
> (acfx Constitution, Principles I-III -- `.specify/memory/constitution.md`)
> ============================================================================

# Tape Dynamics — Lab

Every nonlinear stage this codebase has taught so far — the waveshaping lab's
memoryless shapes, the saturation lab's voicings, program-dependent
saturation's envelope-tracked drive — is a pure function of the *current*
sample: `y = f(x)`. Feed the same `x` twice, ten years apart, and you get the
same `y` both times. Magnetic tape does not work that way. This lab is the
capstone of `phase-dynamic-systems`: it teaches **nonlinearity WITH MEMORY**,
via the **Jiles-Atherton (JA) model of magnetic hysteresis** — the one new
concept this feature owns (Constitution Principle XI).

## Theory

### Hysteresis: a transfer that is not single-valued

A tape head magnetizes iron-oxide particles by nudging microscopic magnetic
domains into alignment. When the applied field `H` relaxes, those domains do
not fully snap back — some of the alignment is *pinned* by material
imperfections (coercivity) and persists. So the tape's magnetization `M` at
this instant depends not just on the current `H`, but on the **path** `H`
took to get here: the same instantaneous `H`, reached by a rising sweep
versus a falling sweep, produces two different `M` values.

Plotted as `M` (vertical) against `H` (horizontal), that path-dependence
traces a closed **loop** with nonzero enclosed area — the rising and falling
branches are two different curves that meet only at the loop's tips. A
memoryless waveshaper, by contrast, always traces a single-valued curve no
matter how the input is swept; its loop area is zero. That closed-loop-area
test — area strictly greater than a defined threshold here, area ≈ 0 for a
static shaper — is this primitive's defining acceptance measurement
(`tests/core/hysteresis-test.cpp`, "closed-loop area > 0, memory proof",
SC-001/FR-018), and it is why `Hysteresis` is **stateful** (it holds `M` and
the previous field `H_prev` across calls) while every waveshaper in this
codebase is not.

### The Jiles-Atherton model: `dM/dH`

Rather than a static curve, JA models tape as an ODE: magnetization as a
function of the field variable, `dM/dH`, evaluated one step per sample. Five
ingredients build the model up from the ground:

1. **Effective field.** `H_e = H + α·M`. The domains do not feel only the
   field the tape head applies (`H`) — they also feel a contribution from
   their neighbors' alignment, proportional to the current magnetization
   itself. This mean-field coupling (`α`, inter-domain coupling) makes the
   model implicitly self-referential: `M` depends on `H_e`, and `H_e` depends
   on `M`.

2. **Anhysteretic magnetization.** `M_an(H_e) = Ms · L(H_e / a)`, the
   magnetization the material would settle to *if it had no memory at all* —
   a smooth, single-valued, `Ms`-bounded curve in the effective field. `L` is
   the **Langevin function**,

       L(x) = coth(x) − 1/x

   the classical statistical-mechanics shape describing how a population of
   dipoles under thermal agitation settles into partial alignment: it is odd,
   asymptotes to ±1, and has a removable singularity at `x = 0` (as `x → 0`,
   `coth(x) → 1/x`, so the direct formula is a difference of two large,
   nearly-equal numbers — a floating-point cancellation trap). Near the
   origin `L` is evaluated via its Taylor series instead,

       L(x) ≈ x/3 − x³/45 + …

   which sidesteps that cancellation entirely.

3. **Irreversible + reversible split.** Not all domain-wall motion is
   pinned. A fraction `c` (reversibility) is purely elastic — it snaps
   instantly to the anhysteretic slope with no lag — and the remaining
   `(1 − c)` is pinned/plastic, chasing the anhysteretic target through an
   **irreversible susceptibility**,

       dM_irr/dH = (M_an − M) / (δ·k − α·(M_an − M)),   δ = sign(dH)

   gated by the coercivity `k`: larger `k` means domain walls resist moving
   more, shrinking this term and *narrowing* the loop. `c = 1` collapses onto
   the single-valued anhysteretic curve (loop area → 0, fully reversible);
   `c = 0` is the widest possible loop for a given `k`. (The classic JA
   pathology — the raw formula can imply the irreversible term should push
   `M` *away* from `M_an` right at a loop tip when the field reverses — is
   guarded by zeroing the term whenever it would point the wrong way and
   clamping it non-negative.)

4. **Combine, with the effective-field feedback correction.** Blending the
   two contributions and correcting for step 1's self-reference gives the
   closed form both the graduated primitive and this lab's kernel implement:

       dM/dH = [ (1 − c)·dM_irr/dH + c·dM_an/dH_e ] / [ 1 − α·c·dM_an/dH_e ]

The five physical parameters this exposes — `Ms` (saturation magnetization,
the ceiling `|M|` approaches), `a` (anhysteretic shape — softer vs harder
knee), `α` (inter-domain coupling), `k` (coercivity — loop width / memory),
`c` (reversibility — loop openness) — are the entire physical vocabulary of
the model; every audible character (wide/narrow loop, soft/hard saturation,
"stickier" vs "looser" tape) is some combination of these five numbers.

### The numerical-solver tradeoff: explicit vs implicit

There is no closed-form solution to the JA ODE — it must be integrated one
step per sample, and the step is genuinely **stiff** near the coercive field
(the slope `dM/dH` changes rapidly relative to the step size). This lab
teaches the standard numerical-methods response to that: three selectable
solvers, all sharing the same `dMdH(H, M, dH)` slope function and differing
only in how they combine evaluations of it.

- **RK2 (Heun)** — explicit, 2 slope evaluations per step (predictor +
  corrector average). Cheapest, `O(dH³)` local error.
- **RK4** — explicit, 4 slope evaluations per step (start, two midpoint
  refinements, end; classic 1:2:2:1 weights). `O(dH⁵)` local error — a
  meaningfully tighter approximation of the true trajectory for roughly
  double RK2's per-step cost. This is the default solver.
- **Newton-Raphson** — *implicit*. Rather than stepping forward from known
  values, it solves the backward-Euler residual `r(M₁) = M₁ − M₀ −
  dH·dMdH(H₁, M₁, dH) = 0` for the unknown end-of-step magnetization `M₁`,
  via a **hard-capped, bounded-iteration** Newton loop (never an unbounded
  `while` — RT-safety, Constitution VI) with a central-finite-difference
  Jacobian. Implicit steppers trade extra per-step work for stability on the
  stiffest transients — exactly where an explicit stepper is most likely to
  overshoot. On non-convergence within the iteration cap, or a non-finite
  iterate, it bails to the explicit RK4 estimate: a defined, documented
  fallback (Constitution V), not a silent one.

This is a genuine accuracy-vs-CPU, explicit-vs-implicit, order-of-accuracy,
stability-under-stiffness lesson, not a cosmetic knob: at a fixed
oversampling factor the three solvers' loops agree only within a stated
tolerance, and that agreement tightens as the oversampling factor rises
(`tests/core/hysteresis-solver-test.cpp`, SC-002) — precisely the classic
numerical-ODE relationship between step size, solver order, and accuracy.

Underneath all three, a **stiff-solver stability guard** runs after every
step: a non-finite result falls back to the last known-finite magnetization
(never snapped to zero — "erased tape" mid-signal would itself be an audible
discontinuity); a finite-but-wildly-out-of-range result is clamped to a small
multiple of `Ms` (a bound loose enough not to clip a legitimately large
physical excursion, tight enough to catch a diverging transient long before
float overflow). This is what makes "no NaN/Inf on any finite input,
regardless of solver" (SC-005) a hard invariant rather than a hope.

### Why ADAA does NOT apply here

The waveshaping lab's antiderivative-antialiasing (ADAA) trick — replace a
naive per-sample `f(x)` with the finite difference of its antiderivative,
`(F(u[n]) − F(u[n−1])) / (u[n] − u[n−1])` — relies on one property: the
nonlinearity is a **static** function of the current sample alone. Because
`f` is static, its antiderivative `F` is *also* a static function, precomputed
once and reused forever, and the finite-difference substitution is
mathematically exact.

`Hysteresis` has no such `f`. What it computes each sample is not "a fixed
curve evaluated at the current `H`" — it is the **next state of an ODE**
whose right-hand side is itself a function of the evolving state `M`, not of
`H` alone. The same `H` produces different outputs on different calls,
depending on `H_prev` and `M`. There is no single static `F(H)` whose
derivative reproduces this output — an antiderivative substitution
fundamentally requires a function, and this is a *trajectory*, not a
function. So the antialiasing route available here is the other one: run the
stateful step at a **higher sample rate than the audio rate** (oversampling),
pushing the harmonic energy the nonlinearity generates up past the folding
frequency of a subsequent decimation filter. This is exactly why
`TapeDynamicsCore` (`core/effects/tape-dynamics/tape-dynamics-core.h`) runs
the JA step as the `evalAtHighRate` callable inside the shipped
`Oversampler<Factor>::process(x, eval)` (`core/primitives/oversampling/`),
reused verbatim rather than modified — the same contrast the compressor lab
draws with the gain computer, but here the contrast is with the waveshaping
lab's own antialiasing technique. This is a key teaching point of the
feature: not every nonlinearity is ADAA's target, and recognizing *why* one
isn't is as instructive as applying ADAA where it does work.

### Emergent dynamic compression

Drive the magnetics harder and the material audibly "glues" — dynamic range
narrows as level rises. This is not a control-path compressor bolted on top;
it is a **property that falls out of the physics**: `M_an` is `Ms`-bounded,
so as the effective field grows, the anhysteretic curve (and with it, the
whole JA trajectory) flattens toward the ceiling, and equal steps in input
level produce shrinking steps in output level. Measured with the explicit
trim disabled, the output-vs-input level curve is monotonic and compressive
above a threshold, and a dynamic-range-reduction metric grows with `drive`
(SC-003) — with **no** dedicated compression parameter anywhere on the
effect (FR-012): the only way to see it is to measure the level curve
itself.

Layered *on top* of that emergent behavior, `TapeDynamicsEffect` offers an
**optional**, explicit envelope-driven trim — composing the already-shipped
`EnvelopeFollower` (detection) and `GainComputer` (static gain-reduction
curve) exactly as the compressor lab does, controlled by `trim.enabled` /
`trim.attack` / `trim.release` / `trim.amount`. When disabled, the signal
path is bit-exact the magnetics-only core — the trim adds a second,
independently-controllable compression layer without ever being required to
get the first, physics-driven one.

### Where this lives

- **Graduated primitive** — `core/primitives/nonlinear/hysteresis.h`: the
  stateful `Hysteresis` class (RK2/RK4/Newton-Raphson, the stiff-solver
  guard), the first *stateful* inhabitant of `nonlinear/` (documented as such
  in `core/primitives/README.md`, alongside the stateless waveshaper family).
- **Lab kernel** — `core/labs/tape-dynamics/kernel/hysteresis-kernel.h`: the
  same physics and math as the graduated primitive, but written and
  commented as the "read me first" version — one heavily-annotated RK4
  stepper, so a reader meets the model once before opening the terser
  production header with its extra RK2/Newton-Raphson steppers.
- **Effect** — `core/effects/tape-dynamics/` (`tape-dynamics-core.h`,
  `tape-dynamics-effect.h`, `tape-dynamics-parameters.h`,
  `tape-dynamics-presets.h`): the host-facing `TapeDynamicsEffect`, composing
  `Hysteresis` under `Oversampler<Factor>` plus the optional
  `EnvelopeFollower` + `GainComputer` trim, mirroring the
  `SaturationEffect`/`CompressorEffect`/`SvfEffect` idiom (`prepare`/
  `process`, lock-free parameter handoff, named presets).
- **Tests** — `tests/core/hysteresis-test.cpp` (closed-loop area/memory
  proof, `reset()` reproducibility, parameter response) and
  `tests/core/hysteresis-solver-test.cpp` (RK2/RK4 agreement,
  Newton-Raphson, the stiff-solver stability guard).
- **Harness** — `core/labs/tape-dynamics/harness/` (host-only): drives the
  primitive/effect to produce the loop-area, solver-agreement, THD/alias-vs-
  oversampling-factor, and dynamic-range-reduction measurements this theory
  predicts, reusing the shipped `host/analysis/thdn.h` and `alias-sweep.h`
  infrastructure.

## Graduation status

Graduated. `core/primitives/nonlinear/hysteresis.h` carries the full JA
model — Langevin anhysteretic curve, irreversible/reversible split, all
three solvers, the stiff-solver guard — moved there from this lab, unchanged
in its physics, refined in its comments for a reader who no longer needs the
first-encounter walkthrough. Per Constitution IX, this lab folder persists
after graduation: this README stays as the living theory record, the
annotated `hysteresis-kernel.h` stays as the "read me first" companion to
the terser production header, and the host-only `harness/` stays here,
driving the graduated primitive to produce the measured evidence FR-015
calls for.

## How to run

```
export CPM_SOURCE_CACHE=external/.cpm-cache
cmake --build --preset test --target acfx_lab_tape_dynamics_harness
./build/test/acfx_lab_tape_dynamics_harness
```

The behavioral guarantees are asserted by the host doctest suites:
`tests/core/hysteresis-test.cpp` and `tests/core/hysteresis-solver-test.cpp`
for the primitive; the `TapeDynamicsEffect` suites enumerated in
`specs/tape-dynamics/tasks.md` and `specs/tape-dynamics/quickstart.md` for
the composed effect. The full public contract lives in
`specs/tape-dynamics/contracts/hysteresis-api.md`; the rationale for each
design decision above is in `specs/tape-dynamics/research.md`
(R1 model, R3 solver surface, R4 antialiasing/oversampling, R5 stability
guard, R6 emergent-vs-explicit compression) and the design record,
`docs/superpowers/specs/2026-07-03-tape-dynamics-design.md`.

## References

- D. C. Jiles and D. L. Atherton, "Theory of Ferromagnetic Hysteresis,"
  *Journal of Magnetism and Magnetic Materials*, 61(1-2), 1986.
- J. Chowdhury, "Real-Time Physical Modelling for Analog Tape Machines,"
  Proceedings of the 22nd International Conference on Digital Audio Effects
  (DAFx-19), 2019 (CHOW Tape) — the closed-form `dM/dH` arrangement and
  real-time integration approach this primitive follows.
