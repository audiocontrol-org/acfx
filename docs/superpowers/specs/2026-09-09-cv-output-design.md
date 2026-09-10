# CV output - design

Status: **draft, awaiting operator review**. 2026-09-09.

> **Revised the same day.** The VCO node was measured after this document was
> first written, and the measurements invalidated its central premise. The
> superseded reasoning is marked below rather than deleted, because the way it
> was wrong is worth keeping.

## Provenance of this document

Recorded explicitly because an earlier capture of this feature blurred it.

**Decided by the operator:**

- Control-only. Audio stays entirely analog outside the MCU and never enters the
  audio path, so a firmware fault cannot interrupt sound.
- One modulation channel, on the built-in DAC.
- Firmware lives in acfx; the hardware interface lives in the `pedals`
  repository.
- Authored through the superpowers process, not the speckit chain.

**Proposed by the agent, not yet decided.** Every item here needs an explicit
yes or no; none of it should be read as agreed:

- `CvSource` as a new concept rather than an `Effect`.
- A 48 kHz timer-driven update rate, so audio-rate modulation is possible rather
  than sub-audio only.
- The parameter set below, and its exposure over the existing MIDI CC map.
- **`LfoShape::stepped`** — see the open decision at the end. This originated as
  an agent suggestion for a six-channel arrangement the operator subsequently
  declined by choosing a single channel. It was then wrongly written up as an
  operator requirement. It is in this document only as a question.

**Forced by a fact, not a preference:** the output range is a declared parameter
rather than a constant. See *Why the output range cannot be a constant*.

**Superseded:** the `cv-output` entry in `DESIGN-INBOX.md` was captured under the
stack-control regime before the operator directed that this feature follow the
superpowers process. It over-attributes scope. This document supersedes it.

## Purpose

Give acfx the ability to emit a control voltage, so it can modulate an external
analog effect. The first and only target use is modulating a PT2399 analog
delay's VCO pin from a NUCLEO-F446RE.

Audio never enters the microcontroller. The MCU is a modulation source and
nothing else.

## Why this is not an `Effect`

acfx's `Effect` concept processes audio blocks: it takes an `AudioBlock` and a
`ProcessContext`. A CV source consumes no audio and produces none. Forcing it
through `Effect` would mean a null audio path that exists only to satisfy a type,
which is a lie in the type system and would have to be special-cased in every
adapter that enumerates effects.

`CvSource` is therefore a separate concept in `core/`, sharing the parameter-table
idiom but not the audio interface.

## Hardware context

Recorded here because it constrains the firmware. The full interface is in the
`pedals` repository at
`docs/superpowers/specs/2026-09-09-delay-module-pcb-design.md`.

The DAC output drives a series resistor into the PT2399's VCO pin. Two facts
matter:

**PA4 is the only usable DAC channel.** PA5 carries DAC2 but is the
NUCLEO-F446RE's LD2, which the existing acfx adapter uses for fault indication.

**The target pin has now been measured.** See the `pedals` repository at
`docs/measurements/2026-09-09-vco-characterization.md`.

```
Voc  = 2.414 V   behind   Rint = 341 ohm      (a half-supply node)
VCO current spans 45:1 across the delay control: 1.80 mA down to 40 uA
pin 6 voltage moves only 1.80 V to 2.40 V
fck proportional to I^0.79       delay = 684332 / fck
```

### What this overturned

This document originally argued that the output window must be a declared
parameter because the pin's voltage was unknown and might sit near 4 V, making a
3.3 V DAC swing one-directional. **That premise was wrong.** The pin sits near
half supply, and the 4 V figure came from a third-party inference that
measurement refuted.

The conclusion survives, for a different and stronger reason, but the mechanism
changes completely.

**Fixed-resistor injection into the pin does not work at all.** A fixed series
resistor delivers roughly constant current into an operating point that varies
45:1, so modulation depth varies 45:1 inversely: 4% of nominal at the short-delay
end, and 250% at the long end where the injection exceeds the oscillator's own
current and swamps it. No resistor value fixes this; it is structural.

**The working topology drives the low end of the resistance chain**, replacing
ground, so that

```
I = (Voc - Vdrive) / (Rint + R_ext)
```

and fractional depth becomes independent of the delay setting. A 1.0 V drive
gives 41% current reduction at both extremes, which through the 0.79 exponent is
a **51% delay change at both extremes**.

### Consequences for this firmware

**The DAC cannot drive the node directly.** At the short-delay end the drive must
sink **1.80 mA**. A buffered STM32 DAC output is typically specified into 5 kOhm
or more, roughly 0.66 mA. A buffer able to sink a few milliamps is required, and
its presence is a hardware precondition for this firmware doing anything useful.
Confirm against the F446 datasheet, which is held in neither repository.

**The output range stays a declared parameter**, but now because the useful drive
window is bounded by what the buffer can sink and by how much delay change is
wanted, not because the target voltage is unknown. The firmware still must not
assume a symmetric swing: `Vdrive` cannot go below ground, so modulation
lengthens delay from the knob setting and does not shorten it.

**The failure mode is now dangerous rather than benign.** Under injection, a dead
MCU meant no modulation. Under bottom-drive it leaves the chain's low end
floating and **the delay stops oscillating entirely**. The hardware carries a
jumper selecting drive or ground; the firmware should additionally bring the DAC
to a defined level early in startup rather than leaving the pin high-impedance
through initialisation.

## Architecture

```
core/                     platform-independent, host-testable
  primitives/modulation/lfo.h    EXISTS - sine, triangle, saw, smoothed random
  modulation/cv-source.h         NEW    - CvSource: LFO -> normalized CV
  modulation/cv-parameters.h     NEW    - constexpr descriptor table

adapters/nucleo/
  cv-output-service.h            NEW    - DAC1/PA4 init, timer tick, scale to code
```

`core/` returns a **normalized** control value in `[0, 1]` and knows nothing about
DAC codes, bit depth, or STM32 peripherals. The adapter maps `[0, 1]` onto the
12-bit range. This keeps Principle VI intact and lets the whole mapping be tested
host-side under Principle X.

### What already exists and is not rebuilt

`core/primitives/modulation/lfo.h` provides sine, triangle, saw and smoothed
random from a seedable xorshift, is allocation-free and deterministic, and is
written to compile under the C++17 Teensy toolchain. `CvSource` composes it
rather than reimplementing it.

`core/dsp/parameter.h` provides `ParameterDescriptor` with `isValidDescriptor`,
which effects `static_assert` over their constexpr table so a malformed
descriptor is a build error rather than a runtime NaN. The CV parameter table
follows that pattern.

## The mapping

```
lfo = lfo_.tick()                              // [-1, 1]
u   = 0.5 + 0.5 * depth * lfo + offset         // nominal [0, 1]
u   = clamp(u, 0, 1)
cv  = outMin + (outMax - outMin) * u           // [outMin, outMax]
```

`clamp` is load-bearing, not defensive tidying: at `depth = 1` with a non-zero
`offset` the intermediate exceeds `[0, 1]`, and without the clamp it would wrap
or produce a discontinuity. This is a named test case.

## Parameters

All normalized values are fractions of the DAC's full scale; the adapter converts.

| Parameter | Unit | Min | Max | Default | Skew | Kind |
| --- | --- | --- | --- | --- | --- | --- |
| rate | Hz | 0.01 | 5000 | 1.0 | logarithmic | continuous |
| depth | normalized | 0 | 1 | 0.5 | linear | continuous |
| shape | - | - | - | sine | - | discrete |
| offset | normalized | -0.5 | +0.5 | 0 | linear | continuous |
| outMin | normalized | 0 | 1 | 0 | linear | continuous |
| outMax | normalized | 0 | 1 | 1 | linear | continuous |

`rate` is logarithmic, so its minimum must be greater than zero or
`isValidDescriptor` rejects it and denormalization yields NaN. 0.01 Hz is a
100-second period, which is the slow end worth having.

`outMin` and `outMax` are the window described above. Their defaults span the
full range; the measurement will narrow them.

## Update rate, and an honest limitation

A 48 kHz timer-driven update makes audio-rate modulation possible, not just
sub-audio LFO rates. At the DAC this costs nothing extra.

**But the LFO shapes alias at audio rate.** They are generated by direct
evaluation with no band-limiting, so a saw or triangle driven at a few kHz folds
its harmonics back down. Sine is unaffected. This is recorded rather than fixed:
band-limiting a modulation source aimed at a deliberately lo-fi delay is probably
not worth the complexity, but it should be a known property rather than a
surprise. The `rate` maximum of 5 kHz keeps the fundamental well below Nyquist;
it does not stop harmonic folding on the non-sine shapes.

Cost estimate: `std::sin` at 48 kHz on a 180 MHz Cortex-M4F is on the order of a
few percent of one core. If that proves too much, a lookup table is the
straightforward answer, but it is not built pre-emptively.

## Failure behaviour

Per Principle VII there are no fallbacks. If DAC1 or its timer cannot be
initialised, the service raises a descriptive fault naming what is absent and
signals it through the adapter's existing LD2 fault path. It does **not** silently
emit a constant voltage, which would look like a working circuit sitting at a
fixed delay time.

Note that the LD2 fault path is on PA5 and the CV output is on PA4, so the fault
indicator and the CV channel do not contend.

## Verification

Host-side, per Principle X. No hardware required for any of these.

- Mapping produces expected values across depth, offset and window combinations.
- Clamping holds at `depth = 1` with `offset` at both extremes.
- A window with `outMin` equal to `outMax` produces a constant, and a window with
  `outMin` greater than `outMax` is rejected rather than silently swapped.
- `reset()` gives a reproducible sequence for the random shape, so tests are
  deterministic.
- The descriptor table passes `isValidDescriptor` at compile time.
- No heap allocation in the tick path, matching the existing effects' guard.

## Out of scope

- Any audio path through the MCU.
- More than one CV channel.
- Level-shifting hardware to obtain a symmetric swing. Whether it is needed
  depends on the pin measurement.
- MIDI clock sync or tempo-locked rates.
- Presets.

## Open decision for the operator

**Does `LfoShape::stepped` belong in scope?**

The existing `random` shape interpolates between targets and its comment states
this is click-free by design. `modulated-delay` depends on that behaviour, so a
stepped variant would be an **additive** enum case, never a change to the existing
one.

Arguments for: stepped random is a distinct musical behaviour, and the primitive
is the natural place for it.

Arguments against: it originated as an agent suggestion for a six-channel
arrangement that was declined; the existing four shapes may well be enough; and
it edits a shared primitive that another shipped effect depends on, which is a
cost the single-channel use case may not justify.

**Default if unanswered: out.** The shared primitive stays untouched.

## Blocking dependency

The pin voltage measurement in the `pedals` project gates the *defaults* for
`outMin` and `outMax`, and determines whether a level-shifting stage is needed at
all. It does not block writing or testing any of the code above, because the
window is a parameter.
