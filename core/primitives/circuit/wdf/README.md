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

## Adaptors (N-port scattering junctions)

  - **`series-adaptor.h`** / **`parallel-adaptor.h`** — `SeriesAdaptor<Child...>`
    and `ParallelAdaptor<Child...>` connect N ≥ 1 child one-ports plus one
    upward port. Each is itself a `OnePort`, so adaptors compose recursively
    (as a child of another adaptor) into arbitrary-depth/width filter trees
    built from the leaves above.
  - **Local upward-port adaptation** — each adaptor makes its upward port the
    single reflection-free (adapted) port: `R_up = Σ_child R_child` for the
    series case, `1/R_up = Σ_child 1/R_child` for the parallel case. The
    scattering coefficients are derived from these sums (`adaptor-detail.h`
    holds the shared variadic-sweep machinery; each file supplies only its own
    coefficient formula).
  - **Adaptable children only** — an adaptor's children must all report
    `isAdaptable == true`; a non-adaptable child (`ShortCircuit`, `OpenCircuit`,
    or any future reflective root) is a **compile-time** `static_assert`
    rejection, since a reflective port anywhere but the tree root would close a
    delay-free loop the sweep cannot solve. The single permitted reflective
    port is the tree **root**, owned and driven by the sibling nodes below —
    never an adaptor child.
  - **Convention and safety** — adaptors reuse the voltage-wave convention
    above unchanged (no new wave convention). The per-sample up-sweep
    (`reflected()`) / down-sweep (`incident(a)`) is `noexcept`, allocation-free,
    and O(N); construction gathers and validates child port resistances and
    precomputes all coefficients off the hot path, throwing
    `std::invalid_argument` (naming the offending child) on a non-positive or
    non-finite resistance — never clamping or substituting a fallback value.

## Out of Scope

Nonlinear roots and ideal-source roots are sibling WDF nodes
(wdf-complete-analog-stages) and are not covered here. Within the adaptors
themselves, the following are also out of scope (owned by sibling nodes):
the single-sample **root driver**, **whole-tree topology / root-port
selection**, **R-type / rigid adaptors**, and **named passive networks**
(all `wdf-passive-networks`); the **ideal-source** and **nonlinear** roots
(`wdf-complete-analog-stages`). Full network transfer-function / frequency-
response tests, which require a source, a root, and a runnable tree over
time, are deferred to the root-driver-owning sibling.
