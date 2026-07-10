# WDF Primitives — Wave-Domain Leaf One-Ports

This is a primitive family (namespace `acfx::wdf`): wave-domain leaf one-ports 
under the voltage-wave convention:

  - `a = v + Rp·i` (incident wave)
  - `b = v − Rp·i` (reflected wave)

A parallel wave-domain reader of the frozen circuit vocabulary (reuses physical 
constants R/C/L/E/I, carries NO NodeId, does NOT depend on the nodal solvers 
mna/newton/integration).

## Port Interface

Leaves satisfy a duck-typed `OnePort` interface with:
  - `portResistance()` — characteristic impedance
  - `reflected()` — compute and return reflected wave *b* from incident wave *a*
  - `incident(a)` — absorb incident wave
  - `isAdaptable` — trait marking port adaptability

The interface itself (`is_one_port` / `is_one_port_v` concept trait, plus the
`waveToVoltage` / `waveToCurrent` inverse helpers) lives in `one-port.h`; it
carries no leaf implementations.

## Leaf One-Ports (8)

  - **`wave-elements.h`** — `Resistor` (memoryless, adapted `b = 0`);
    `Capacitor`, `Inductor` (reactive, unit-delay `b[n] = ±a[n-1]`).
  - **`wave-sources.h`** — `ResistiveVoltageSource` (adapted `b = E`),
    `ResistiveCurrentSource` (adapted `b = R·I`).
  - **`wave-terminations.h`** — `ResistiveTermination` (adapted `b = 0`);
    `ShortCircuit`, `OpenCircuit` (reflective, non-adaptable, `b = ∓a` / `b = ±a`).

## Implementation Notes

Reactive elements (capacitor, inductor) use **bilinear discretization**; 
capacitors map to unit-delay filters. Non-physical parameters throw at 
construction time (no fallbacks, no clamping).

## Out of Scope

Adaptors, tree assembly/adaptation, nonlinear roots, and ideal-source roots are 
sibling WDF nodes (wdf-adaptors, wdf-passive-networks, wdf-complete-analog-stages) 
and are not covered here.
