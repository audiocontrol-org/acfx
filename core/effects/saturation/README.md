> ============================================================================
> ‼ acfx COMMANDMENTS (non-negotiable) ‼
> 1. COMMIT AND PUSH EARLY AND OFTEN
> 2. NO GIT HOOKS, EVER
> 3. DESCRIPTIVE NAMES, NEVER NUMERIC PREFIXES
> (acfx Constitution, Principles I-III -- `.specify/memory/constitution.md`)
> ============================================================================

# Saturation Effect

`SaturationEffect` is a voiced saturation Production Effect: a nonlinear
waveshaping stage placed between two emphasis filters, wrapped in the Effect
contract for host consumption. It is the **first lab->effect graduation** — the
first composition kernel to be relocated (by `git mv`, not re-derivation) from
`core/labs/` into the *effects* layer, and the first to graduate a *composition
kernel* rather than a single primitive (Constitution Principle IX).

## What it is

Three portable headers, graduated together from `core/labs/saturation/`:

| Header | Role |
|---|---|
| `saturation-core.h` | The RT-safe composition kernel `SaturationCore`: pre-emphasis -> nonlinear waveshaper(drive, bias) -> post-de-emphasis -> tone tilt -> dry/wet mix -> output gain. Owns the composed sub-units; no allocation in `process()`. |
| `saturation-voicings.h` | The voicing table: four documented voicings (Soft Clip, Tape, Console, Tube Preamp), each fixing a nonlinear `Shape` plus a distinct pre-/post-emphasis SVF curve pair so the set is measurably distinct in harmonic + spectral character. Stateless constexpr selector. |
| `saturation-effect.h` | The host-facing wrapper `SaturationEffect`: a constexpr `ParameterDescriptor` table as the single source of parameter truth plus a lock-free atomic cross-thread parameter handoff, mirroring `core/effects/svf/svf-effect.h` exactly. Owns per-channel `SaturationCore` state; allocation-free per block. |

## Which primitives it composes, and why

`SaturationCore` introduces **no new DSP primitive** (FR-001) — it is pure
composition of already-shipped building blocks:

- **`core/primitives/nonlinear/waveshaper.h` (naive) and
  `core/primitives/nonlinear/adaa-waveshaper.h` (ADAA)** — the nonlinear stage.
  Both are held; `quality_` selects which one drives `process()` at run time, so
  the effect can trade CPU for anti-aliasing (the ADAA path suppresses the
  aliasing a naive memoryless nonlinearity would fold back into band). The
  waveshaper is what actually generates the harmonics that saturation is about.
- **`core/primitives/filters/svf-primitive.h`** — three instances: pre-emphasis
  and post-de-emphasis (the emphasis pair that gives each voicing its tonal
  fingerprint by shaping *what* the nonlinearity acts on and then undoing that
  tilt after) plus a tone-tilt filter on the user-facing tone control. The SVF
  is reused rather than reinvented because the emphasis curves are exactly
  second-order shelving/peaking shapes it already provides.

Composing the shipped Waveshaper/ADAAWaveshaper + SvfPrimitive — rather than
hand-rolling a bespoke saturation DSP block — is the whole point of the effects
layer: primitives are the vocabulary, effects are the sentences.

## Parameter surface

Exposed via the `saturation-effect.h` constexpr `kParams` table (drive, voicing,
tone, mix, output, bias, quality). `voicing` selects a row of the voicing table;
`quality` selects the naive vs ADAA waveshaper; `drive`/`bias` set the nonlinear
push and its asymmetry; `tone` tilts the post-stage; `mix` is the parallel
dry/wet blend; `output` is the final make-up gain. `setParameter()` is callable
from any thread and only publishes lock-free atomics that the audio thread
consumes at the top of `process()`.

## Pointer back to the lab

The theory (why each voicing sounds as it does), the tuning rationale, and the
host-only characterization **harness** live in `core/labs/saturation/`. That lab
persists as living documentation + harness after this graduation; it does not
disappear (Constitution Principle IX). Post-graduation the harness consumes these
graduated effect headers via `effects/saturation/...` includes — the one allowed
harness->effect dependency — while nothing portable may ever include the harness.

## Dependency + RT-safety contract

Platform-independent (no JUCE / libDaisy / Teensy headers), harness-free, and
allocation-/lock-free in every `process()` path. These invariants are enforced
mechanically by `scripts/check-portability.sh` (the `C-SFX` block for this
directory), run on purpose locally and in CI — never as a git hook.

Tests: `tests/core/saturation-*-test.cpp`.
