# Contract — Reference solver (lab-only; NOT the primitive)

Lives in `core/labs/component-abstractions/`. A deliberately-naive validator that makes the
vocabulary runnable before Phase 5 MNA exists. **Explicitly non-normative and
Phase-5-superseded** (design D3, OQ2). It must never grow into MNA.

## Scope (hard boundary — FR-016)

| Circuit class | Handled? | How |
|---|---|---|
| Linear (R, C, L, sources) | ✅ | fixed-size Gaussian elimination on the reduced nodal system |
| Single nonlinearity (one diode, or an antiparallel diode pair treated as one clipper element) | ✅ | bounded fixed-iteration, voltage-limited Newton (R2) |
| **≥2 interacting nonlinear components** | ❌ | **refuses** with `"out of reference-solver scope — deferred to Phase 5"` (FR-016) |

## Linear solve contract (US3)

1. Assemble the reduced conductance system `G' · v = i'` by reading each component's physics:
   `Resistor.admittance()`, reactive `companion(dt, state)`, `CurrentSource` → RHS.
2. Impose each ideal **`VoltageSource` by fixed-node reduction** (R1): the pinned node is a
   known; move its column to the RHS and drop its row. **No gmin/finite-conductance fallback.**
3. Solve by Gaussian elimination on the fixed-size working matrix — **no heap allocation**
   in the solve (FR-013/011).
4. **Guarantees:** resistive divider → exact ratio (SC-002); RC/RLC → analytic magnitude/phase
   within the documented backward-Euler tolerance (SC-003).

## Nonlinear solve contract (US4)

1. For the single-diode / antiparallel clipper: iterate Newton using `Diode.evaluate(v) →
   {current, conductance}`, with junction **voltage limiting** each step (R2).
2. Stop at convergence (residual < tol) or at the **iteration bound `N`**.
3. On non-convergence within `N`: **report the final residual/status** — never fall back, never
   fabricate output (FR-015).
4. **Guarantee:** single/antiparallel clipper static transfer matches the analytic soft-clip
   within tolerance (SC-004).

## Reactive time-stepping

- Backward Euler only (OQ1, non-normative). The solver owns per-node previous voltages and
  per-inductor previous currents and passes them into each reactive `companion(dt, ...)` (R3).

## Non-goals (explicit)

- No general multi-nonlinearity Newton, no MNA source-branch augmentation, no trapezoidal /
  higher-order integration, no sparse/large-system handling. Each is a Phase 5 subject; reaching
  for any of them here is a contract violation.

## Isolation guarantee (SC-007)

- The primitive (`core/primitives/circuit/`) has **no dependency** on this solver. Removing the
  lab leaves the vocabulary and its physics/netlist tests fully functional; only the
  assembled-circuit harness validations go away.
