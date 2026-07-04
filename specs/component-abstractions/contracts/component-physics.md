# Contract — Component physics (the primitive's public surface)

The vocabulary each v1 component exposes. This is the contract Phase 5 MNA and Phase 6 WDF
adapt against. All physics is `double` (FR-022). No component exposes any solver-specific
operation (FR-006).

## Common shape

Each component is a value struct binding terminals to `NodeId`s plus its parameters. The
`Component` container is `std::variant<Resistor, Capacitor, Inductor, VoltageSource,
CurrentSource, Diode>` visited with `std::visit` (FR-008).

## Linear elements — `admittance()`

```
Resistor{ NodeId a, b; double R }         → admittance() = 1/R
```

- **Contract:** returns the conductance `G` contributed between terminals `a` and `b`, with the
  nodal sign convention `(+G, +G)` on the two diagonal entries and `(−G, −G)` off-diagonal.
- **Precondition:** `R > 0`. `R = 0` (ideal short) and `R = ∞` (open) are represented but a
  short that forms an ill-posed loop with an ideal source is caught by netlist validation.
- **Test (US1.1):** `Resistor{a,b,R}.admittance() == 1/R` exactly.

## Nonlinear element — `evaluate(v)`

```
Diode{ NodeId anode, cathode; double Is, n, Vt }
  evaluate(vAK) → { current = Is*(exp(vAK/(n*Vt)) - 1),
                    conductance = (Is/(n*Vt))*exp(vAK/(n*Vt)) }
```

- **Contract:** returns the Shockley current and its analytic small-signal conductance `dI/dV`
  at bias `vAK` (R2). Deterministic, side-effect-free.
- **Test (US1.2):** at a chosen forward bias, `current` and `conductance` match the closed form
  within `double` tolerance.

## Reactive elements — `companion(dt, state)`

```
Capacitor{ NodeId a, b; double C }
  companion(dt, vPrev) → { Geq = C/dt, Ieq = Geq*vPrev }

Inductor{ NodeId a, b; double L }
  companion(dt, iPrev) → { Geq = dt/L, Ieq = -iPrev }
```

- **Contract:** returns the backward-Euler equivalent `{Geq, Ieq}` for timestep `dt`. The
  history `state` (previous voltage for C, previous current for L) is **passed in by the
  solver** — the component stores none (design D2 / R3). Backward Euler is non-normative (OQ1).
- **Test (US1.3):** for a chosen `C`/`L` and `dt`, `Geq` and `Ieq` match the formulas exactly.

## Sources

```
VoltageSource{ NodeId p, n; double V }   // ideal — pins (p - n) = V; solver uses fixed-node reduction (R1)
CurrentSource{ NodeId p, n; double I }   // injects +I at p, -I at n (RHS contribution)
```

- **Contract:** the voltage source is ideal (no series resistance); the solver imposes its
  constraint by fixed-node reduction, never by a finite-conductance approximation (R1, no
  fallback). The current source contributes to the system RHS only.

## Invariants (asserted)

- **No solver leakage (FR-006):** the public surface is exactly `admittance()` /
  `evaluate()` / `companion()` / parameter access. Grep-level test: no `stamp`/`scatter`
  symbol in `core/primitives/circuit/`.
- **C++17 + platform-independent (FR-017):** headers compile under `-std=c++17` with no
  JUCE/libDaisy/Teensy include.
- **Solver-independent (SC-007):** these headers compile and their tests pass with
  `core/labs/component-abstractions/` absent.
