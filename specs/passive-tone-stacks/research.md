# Phase 0 Research — passive-tone-stacks

All Technical-Context unknowns resolved below. Format per decision: **Decision / Rationale / Alternatives considered.** These feed the Phase-1 data-model and contracts; nothing here re-opens a load-bearing design decision (those are fixed in the approved design record).

## R1 — FMV tone-stack topology and per-pot wiring

**Decision.** Model a **Fender/Marshall/Vox (FMV)-style passive 3-band stack** (basic Bassman form: treble + bass shaping caps): the input feeds the **treble capacitor** `C1` into the top of the **treble pot** and, through a **slope resistor** `R1`, into the **bass/mid branch** (`C2` to the bass pot, then the mid pot to ground). Per-pot wiring (finalized against the built topology):

- **Treble pot** — 3-terminal divider: `wiper()` splits its track into two `Resistor` legs; the wiper node is the output tap.
- **Bass pot** — **rheostat** (`rheostat()`, 2-terminal), in series after the bass cap `C2` toward the mid pot (the classic Fender bass wiring is a variable series resistor, not a divider).
- **Mid pot** — **rheostat to ground** (`rheostat()`, 2-terminal): the mid control is the resistance from the bass/mid junction to ground; the mid pot near 0 collapses the junction toward ground (the recognizable mid scoop).

**Rationale.** This is the canonical guitar tone-stack shape. The mixed wiring (one divider + two rheostats) exercises **both** `wiper()` and `rheostat()` in one topology. Because the AC solver is proven exact (R6), the stack is validated by exact DC resistive limits + monotonic invariants + passivity (see the spec's 2026-07-04 implementation-decision) rather than a hand-transcribed published rational; exact-vendor-BOM fidelity to a specific Fender part list is the later `design:feature/fender-tone-stack` item.

**Alternatives considered.** *Big Muff tone control* (a treble/bass blend/crossover) — a genuinely different topology, captured for the later `big-muff` feature, not v1. *Marshall/Vox value variants* — same FMV topology, different BOM; captured as re-valued instances of `toneStackFMV`, not separate builders.

## R2 — Baxandall (passive James) topology

**Decision.** Model the **passive James network** (the passive form of the Baxandall bass/treble control): two interleaved RC shelving branches, a **bass pot** and a **treble pot**, each a 3-terminal divider (`wiper()`), with **linear** taper. Output taken at the summing node into the explicit `rLoad`.

**Rationale.** A genuinely different shape from FMV (2-band shelving vs 3-band interacting) — the anti-over-fit exemplar (design record: the L-precedent). Passive James avoids the op-amp the active Baxandall needs (that element is deferred to `opamp-stages`, per OQ5). Baxandall/James controls are conventionally **linear-taper** pots — the network does the shaping — which is why v1's two tapers (Linear + Log) suffice and no exemplar needs antilog (D10).

**Alternatives considered.** *Active op-amp Baxandall* — needs the deferred controlled-source/op-amp element; out of scope (→ `opamp-stages`). *Tilt-EQ / single-knob variants* — captured, not v1.

## R3 — Log ("audio") taper-law form

**Decision.** `Taper::Log` maps mechanical `pos ∈ [0,1]` to an electrical fraction `f` by a **pure exponential audio-taper law** `f = (pow(base, pos) - 1) / (base - 1)` with a fixed `base` (a ~10× / one-decade audio taper, e.g. `base = 10`), giving `f(0)=0`, `f(1)=1`, and the concave audio curve. `Taper::Linear` is `f = pos`. `wiper` then returns `rBottom = f·rTrack`, `rTop = (1-f)·rTrack`, each floored per R4.

**Rationale.** A single closed-form exponential is exact, monotonic, and cheap; it hits the endpoints exactly and needs no piecewise breakpoints. It is a modelled taper, testable against a reference fraction at any `pos` (SC-002).

**Alternatives considered.** *Two-segment piecewise-linear approximation* (what physical pots often use) — closer to a specific vendor part but introduces an arbitrary breakpoint and a discontinuity in slope; deferred as a possible future refinement, not needed for the analytic cross-check. *Antilog / reverse-log* — omitted entirely (D10): no v1 exemplar uses it, and a dead enum case would be a stub.

## R4 — Wiper end-resistance floor

**Decision.** Each wiper leg is floored at a **fixed 10 Ω** modelled contact/end resistance: `leg = max(computed, 10.0)`, applied **per leg independently**. `wiper` returns the floored `{rTop, rBottom}`; `rheostat` returns the single floored leg.

**Rationale (from D8).** A real pot never reaches 0 Ω; a literal 0-Ω `Resistor` would create a degenerate node that trips `Netlist::prepare()`. Fixed Ω (not fraction-of-track) is physically correct — contact resistance is a property of the wiper, not proportional to track value — and behaves sanely across a stack mixing 10 kΩ (mid) and 250 kΩ–1 MΩ (bass/treble) pots. The floor bites only at the extremes; near an endpoint `rTop + rBottom` may exceed `rTrack` by up to 10 Ω, which is physically correct series end resistance and is within the 0.1 dB analytic tolerance (OQ4).

**Alternatives considered.** *Fraction-of-track floor* — rejected (design review): not physical, over-floors large pots. *No floor / epsilon* — rejected: risks the degenerate-node throw and hides a real modelling choice behind a magic epsilon.

## R5 — Per-topology `Netlist` capacities

**Decision.** Size `Netlist<MaxNodes, MaxComponents>` per topology as compile-time constants derived from each bill of materials, with a small headroom margin:

- **FMV**: interior nodes for the input, the slope-resistor junction, the treble-wiper/output, the bass-wiper, and the bass/mid junction (~5–6 non-ground nodes); components = input `VoltageSource` + `R1` + three caps + the pot legs (treble 2 + bass 2 + mid 1) + `rLoad` (~11–12). Choose `kFmvNodes`/`kFmvComponents` covering this exactly.
- **Baxandall**: fewer nodes (2-band); `kBaxNodes`/`kBaxComponents` sized to the James BOM.

Exact counts are finalized in `data-model.md` against the wired schematic; over-capacity is impossible by construction (the builder emits a fixed component set), and `Netlist::add`/`addNode` still throw `std::out_of_range` if a future edit exceeds the constant.

**Rationale.** Fixed compile-time capacities keep the builder heap-free (SC-007) and match the templated `Netlist` from `component-abstractions`. Per-topology constants (not one global max) keep each stack's footprint honest.

**Alternatives considered.** *One shared global capacity* — wasteful and hides each topology's real size. *Runtime-sized container* — violates the heap-free invariant (Constitution VI).

## R6 — Complex `.ac` solver (lab)

**Decision.** Implement `solveAC(netlist, ω, inNode, outNode) → std::complex<double>` by **reusing the `LinearSolver` assembly structure over `std::complex<double>`**: build the reduced nodal admittance matrix `Y(jω)` by stamping each component's admittance at `jω` — `Resistor → 1/R`, `Capacitor → jωC`, `Inductor → 1/(jωL)` — impose the ideal input `VoltageSource` by the same fixed-node reduction `LinearSolver` uses (R1 from `component-abstractions`), and solve by **complex Gaussian elimination with partial pivoting**. Return `H = V(outNode)/V(inNode)`; a zero/near-singular pivot throws `std::runtime_error` naming `ω`.

**Rationale.** AC analysis is a *linear complex* solve — the same reduction and elimination `LinearSolver` already implements, only the scalar type changes (`double → std::complex<double>`) and the reactive stamps become the exact phasor admittances (no backward-Euler companion, no history). It yields the **exact continuous-time** `H(jω)`, matching the textbook Duncan curve directly. It is explicitly **not** MNA (no branch-current unknowns) and **not** Newton — the `component-abstractions` "lab must never grow into MNA" boundary (FR-013) is preserved.

**Alternatives considered.** *Swept-sine on the time-domain `LinearSolver`* — validates only the backward-Euler-warped response, indirect and slow (design record, rejected). *Reuse `LinearSolver` verbatim by templating its scalar type* — attractive but couples the frozen `component-abstractions` lab to this deliverable's need; instead the AC solver is a **new sibling** in this deliverable's own lab (D6), leaving `LinearSolver` untouched. Graduation of `ac-solver.h` beside `LinearSolver` is deferred to a second consumer (OQ3).

## R7 — Independent analytic reference (Duncan)

**Decision.** The cross-check compares `solveAC` against the **published Duncan tone-stack transfer function** transcribed independently of our solver — a known rational function `H(s)` in the R/C/pot values for the FMV stack, and the standard passive-Baxandall/James response — evaluated at `s = jω`. Plus topology-independent **musical invariants** (DC gain, HF asymptote, monotonic mid-scoop vs mid pot, monotonic HF vs treble pot) as a second, derivation-free check.

**Rationale.** "Independent" means the reference is not the solver graded against itself (mirrors how `component-abstractions` graded the divider/RC/RLC against closed-form). The Duncan model is the accepted canonical derivation; the musical invariants catch a whole class of errors without a full symbolic derivation.

**Alternatives considered.** *Grade the solver against a second numerical solver* — not independent enough (two solvers can share a bug). *Full hand symbolic derivation only* — fragile per-topology; used as one leg, backed by the invariants.

## Resolved unknowns

All Technical-Context items are resolved: language/version (C++17 primitive, C++20 lab), dependencies (frozen `circuit/` vocabulary + `<complex>`), testing (Tier-1/Tier-2 + harness), capacities (R5), solver approach (R6), and the analytic reference (R7). No `NEEDS CLARIFICATION` remains (OQ4 was resolved in the clarify session; the spec carries the 0.1 dB / 10-pts-decade values).
