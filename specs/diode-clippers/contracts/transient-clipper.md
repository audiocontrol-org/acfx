# Contract — TransientClipper (host-only lab solver)

**Location:** `core/labs/diode-clippers/solver/transient-clipper.h`
**Layer:** lab — host-only, C++20 OK, **non-normative, Phase-5-superseded scaffolding**.
**Consumes:** the frozen `component-abstractions` vocabulary + its lab `LinearSolver`
(`core/labs/component-abstractions/solver/`). The primitive does **not** depend on this.

## Surface

```cpp
namespace acfx::labs::diode_clippers {

template <int MaxNodes, int MaxComponents, int MaxDiodes = 4>
class TransientClipper {
public:
    explicit TransientClipper(int maxIterations = 50,
                              double voltageTol = 1e-9,
                              double currentTol = 1e-12);   // throws invalid_argument if N<1 or tol<=0
    void reset() noexcept;                                   // cold circuit (clear history + warm-start)

    NewtonStatus step(const Netlist<MaxNodes, MaxComponents>& nl, double dt);  // advance one timestep

    double voltage(NodeId node) const;        // final-iterate node voltage (meaningful iff converged)
    double clipperVoltage() const;            // V(portP) - V(portN)
    int    maxIterations() const noexcept;
    double voltageTolerance() const noexcept;
    double currentTolerance() const noexcept;
};

}
```

Internal augmented netlist: `Netlist<MaxNodes, MaxComponents + 2*MaxDiodes>` (each diode →
`Resistor` + `CurrentSource` companion). Backed by a nested
`LinearSolver<MaxNodes, MaxComponents + 2*MaxDiodes>`. All stack `std::array` — no heap in `step()`.

## Guarantees

1. **Separated timestep / Newton loops (the load-bearing mechanism, FR-009).** Per `step()`:
   reactive companions `{Geq, Ieq}` are computed **once** from held history (via the frozen
   `capacitor.h`/`inductor.h` `companion(dt, ·)`); the inner Newton iterations hold them **fixed**;
   reactive history is advanced **exactly once**, after Newton converges. This is what makes the
   reactive+nonlinear case correct — the case the static `NewtonClipper` refuses.
2. **Bounded Newton (FR-010).** Each iteration companion-linearizes the diode string
   (`Geq=g(vAK)`, `Ieq=I(vAK)−Geq·vAK`) via `Diode::evaluate`, appends the Norton pair as linear
   stamps, solves the purely-linear system, damps the new junction voltage through
   `Diode::limitJunctionVoltage` (pnjlim), tests `|Δv| < voltageTol`.
3. **Non-convergence is surfaced, never faked (FR-011).** Returns `NewtonStatus` every call;
   `converged=false` carries the last iterate + residuals; no fallback, no fabricated output.
   Defaults `N=50`, `voltageTol=1e-9`, `currentTol=1e-12`; not silently retuned.
4. **Bounded & non-MNA (FR-012).** Single clipper port; a single nonlinearity *location* (one node
   pair carrying the diode string, up to `MaxDiodes` diodes). ≥2 *interacting* nonlinearities
   (distinct node pairs) → descriptive `std::runtime_error` (out of scope; Phase 5). No general
   MNA / gmin / general nonlinear engine.
5. **Fail loud on ill-posed input (FR-013).** `dt ≤ 0` → `std::invalid_argument`; a singular
   reduced system → `std::runtime_error`. Never a silent wrong answer.
6. **Double precision (FR-014).** All computation in `double`; no `float` audio boundary.

## Validation obligations (FR-015..018, exercised by the harness + Tier-2 test)

- Linear-only RC run matches the analytic backward-Euler recurrence to ~1e-9.
- Each clipper's DC steady state matches the independent bisection oracle (data-model.md) to ~1e-6
  and the existing static `NewtonClipper` curve.
- Assembled invariants: odd-symmetry (symmetric) / DC-offset (asymmetric); forward saturation;
  passivity (output energy ≤ input); the reactive signature (larger `Cf` ⇒ less post-clip HF at
  1 kHz / 100 kHz / >5 kHz).
- An explicit **starved-budget non-convergence** case asserts `converged=false` is reported.
