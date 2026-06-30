# State-Variable Filter — Lab

A state-variable filter (SVF) produces lowpass, bandpass, and highpass outputs
simultaneously from a single topology, with independently tunable cutoff frequency
and resonance (Q). This lab introduced the SVF concept to the acfx DSP core and
produced the evidence that the implementation meets the project's RT-safety and
correctness requirements.

## Theory

A state-variable filter is a second-order IIR structure whose feedback network
computes LP, BP, and HP outputs in lockstep on every sample. The canonical
discrete-time recurrence (Chamberlin 1980, Zölzer 2011) is:

    HP[n] = x[n] − LP[n-1] − q · BP[n-1]
    BP[n] = f · HP[n] + BP[n-1]
    LP[n] = f · BP[n] + LP[n-1]
    notch[n] = HP[n] + LP[n]

where x[n] is the input sample, f = 2 · sin(π · fc / fs) is the frequency
coefficient derived from cutoff fc and sample rate fs, and q controls resonance
(lower q → higher Q / narrower peak).

Key properties:

- Cutoff frequency and resonance are tunable by two independent scalar coefficients.
- All four classic responses (LP, HP, BP, notch) emerge from the same state
  without re-computation — the topology computes them all on every sample.
- Stability holds while resonance stays in [0, 1] and cutoff stays in
  (0, sampleRate/3); DaisySP enforces these as the API's documented bounds.

## Walkthrough

`acfx::SvfPrimitive` (at `core/primitives/filters/svf-primitive.h`) is a thin,
allocation-free wrapper over `daisysp::Svf`, the proven SVF implementation from
the DaisySP pure-DSP library. Wrapping DaisySP rather than re-implementing the
recurrence was an explicit design decision (see `research.md` decision 1): the
library is platform-independent and has no platform headers, so depending on it
keeps `core/` free of hardware concerns (Constitution IV).

### Initialization

```cpp
acfx::SvfPrimitive svf;
svf.init(sampleRate);   // stores sample rate; calls daisysp::Svf::Init()
```

`init()` both prepares internal coefficients and clears all filter state.
It is safe to call at buffer-start or after a silence event. The stored
sample rate is used by `reset()` to re-initialize without requiring the
caller to supply it again.

### Parameter control

```cpp
svf.setFreq(hz);   // 0 < hz < sampleRate/3  — caller (SvfEffect) must clamp
svf.setRes(r);     // r in [0, 1]
svf.setMode(acfx::SvfMode::lowpass);    // or ::highpass, ::bandpass
```

`setFreq` and `setRes` forward directly to `daisysp::Svf::SetFreq` / `SetRes`;
the DaisySP layer converts to the f and q coefficients described above. Mode
selection is owned entirely by `SvfPrimitive`; the underlying DaisySP filter
always computes all three outputs on every call to `Process`, and `process()`
dispatches to the selected output.

### Per-sample processing

```cpp
float out = svf.process(in);   // RT-safe: no allocation, bounded work
```

`process()` calls `daisysp::Svf::Process(in)` then dispatches on `mode_`:

- `SvfMode::lowpass`  → `svf_.Low()`
- `SvfMode::highpass` → `svf_.High()`
- `SvfMode::bandpass` → `svf_.Band()`

No heap allocation, no locks, and no unbounded branching occur inside
`process()` — it is safe for use in audio callbacks (Constitution VI).

### Reset

```cpp
svf.reset();   // re-runs Init() with the stored sample rate, clears state
```

Useful when a voice is stolen or a bypass is lifted and stale state must be
discarded without losing the current sample-rate configuration.

## Graduation target

Graduated to `core/primitives/filters/svf-primitive.h`.

The kernel originally developed in this lab has been moved via `git mv` to
`core/primitives/filters/svf-primitive.h` and refined in place. The lab folder
now holds only this README and the host-only measurement harness; the primitive
itself lives in the `core/primitives/` layer and is available to the full
effects layer.

## Measurements

The host-only harness at `core/labs/state-variable-filter/harness/svf-harness.cpp`
drives `acfx::SvfPrimitive` directly (no effect layer, no DAW) and emits two
categories of evidence.

### Per-mode frequency response

For each of the three modes the harness configures `SvfPrimitive` at a reference
cutoff and sample rate, then measures steady-state output magnitude at a
passband frequency and a stopband frequency using a swept-tone approach:

- **Lowpass** — passband magnitude (below cutoff) meets a minimum gain threshold;
  stopband magnitude (above cutoff) falls below a maximum gain threshold;
  passband gain exceeds stopband gain.
- **Highpass** — magnitude at the low-frequency edge falls below the stopband
  threshold; magnitude at the high-frequency edge meets the passband threshold;
  high-frequency gain exceeds low-frequency gain.
- **Bandpass** — magnitude at the centre (cutoff) frequency exceeds magnitude at
  both the low and high edges, confirming the resonant peak.

### High-resonance stability

With resonance normalized to 0.99 (near the DaisySP-documented stability limit),
the harness feeds a single impulse followed by 200 000 samples of silence in
bandpass mode. It then asserts:

- Every output sample passes `std::isfinite`: no NaN or denormal escapes the
  filter state under near-maximum resonance.
- Peak absolute output remains below 100.0: any self-oscillation decays rather
  than diverging.

Together these two checks confirm that `SvfPrimitive` preserves DaisySP's
stability guarantees under extreme resonance settings before the primitive is
relied upon by the effects layer.
