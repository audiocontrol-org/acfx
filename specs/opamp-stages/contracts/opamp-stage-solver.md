# Contract — nullor-augmented lab solver (host-only)

**File:** `core/labs/opamp-stages/solver/opamp-stage-solver.h`
**Scope:** the bounded nullor MNA augmentation + the clipper's Newton coupling (design D5, FR-011..FR-018). Host-only, non-normative, NOT a shippable RT path.

## Capability

Given an assembled op-amp-stage `Netlist`, solve for node voltages by **bordering** the reduced nodal system with one row + one column per `OpAmp`:

```
[ G   B ] [ v ]   [ i ]
[ C   0 ] [ j ] = [ 0 ]
```

- `B`: `+j` into the KCL of the op-amp `out` node (norator output current).
- `C`: `V(inPlus) − V(inMinus) = 0` (nullator / virtual short).
- Solved by the lab's existing fixed-size Gaussian elimination with partial pivoting.

## Modes

1. **Linear resistive stages** — one bordered solve; output ratio equals the analytic gain.
2. **Active first-order stage** — one bordered solve per timestep with a backward-Euler `Cf` companion (frozen `capacitor.h::companion`); reactive history advanced **once per step**.
3. **Op-amp feedback-diode clipper** — `diode-clippers`' **separated timestep/Newton** structure around the bordered solve: companions once per step; inner Newton holds them fixed, companion-linearizes the diode string into a Norton pair, solves the **bordered** linear system (nullor rows included), pnjlim-damps the junction voltage (`Diode::limitJunctionVoltage`), tests `|Δv| < tol`; advance history once after convergence. Returns a per-sample `NewtonStatus`.

## Guarantees

1. **Exact linear** — analytic gains (`1+Rf/Rg`, `−Rf/Rin`) to ~1e-9; first-order response to ~1e-9.
2. **Non-convergence surfaced** — `NewtonStatus{converged,iterations,voltageResidual,currentResidual}`; never a fabricated output. Defaults `50 / 1e-9 / 1e-12`, not silently retuned to hide a failure.
3. **Well-posedness authority** — a **singular augmented system** raises a descriptive error (the authoritative gate). `contributesConductivePath` is only a conservative nodal-only pre-filter (verified sound for the four exemplars, not a general law).
4. **No op-amp fallback** — the op-amp is the nullor constraint, **never** a large-but-finite-gain source / large conductance (gmin forbidden).
5. **Heap-free** on the solve path; `double` throughout; no `float` audio boundary.

## Bounded charter — three checkable tripwires (FR-015, SC-007)

1. **`OpAmp`-specific augmentation** — only `OpAmp` branches are augmented; `VoltageSource` stays fixed-node reduction; R/C/L/`CurrentSource` stay nodal/companion. Anything else needing augmentation → descriptive stop.
2. **Single-nonlinearity-location refusal** — ≥2 interacting nonlinearities (distinct node pairs) → descriptive out-of-scope error (deferred to Phase 5).
3. **One row/col per op-amp, sized at instantiation** — no dynamic growth; augmented dimension is a compile-time function of the template capacities.

## Prohibitions

- MUST NOT modify the `component-abstractions` `LinearSolver` (its charter forbids MNA growth).
- MUST NOT introduce general MNA, gmin stepping, or a general nonlinear engine.
- `dt ≤ 0` → descriptive `std::invalid_argument`.
