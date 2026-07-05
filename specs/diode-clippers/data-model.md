# Phase 1 — Data Model: diode-clippers

Value types and their validation. All are plain aggregates (no inheritance, no retained state);
physics units are SI (`double`). The frozen `component-abstractions` types (`Resistor`,
`Capacitor`, `Diode`, `VoltageSource`, `Netlist`, `NodeId`) are consumed unchanged.

## Diode parameters (shared)

- **`DiodeSpec`** — `{ double Is; double n; double Vt; }` — the Shockley parameters fed to each
  `Diode` component (reverse saturation current, ideality factor, thermal voltage). Validation:
  all `> 0` (FR-007). Canonical default a silicon signal diode (`Is=1e-14`, `n=1`, `Vt=0.02585`),
  matching the `component-abstractions` reference set.

## Per-topology bill of materials

- **`SymmetricShuntValues`** — `{ double R; double Cf; DiodeSpec diode; }` — series resistor,
  filter cap across the diodes, one diode spec (the matched antiparallel pair reuses it). Output ≡
  the shunt node.
- **`AsymmetricShuntValues`** — `{ double R; double Cf; DiodeSpec diode; int upCount; int downCount; }`
  — same structure with an unequal population (`upCount` diodes anode→shunt-node, `downCount`
  reversed). v1 canonical `{ upCount=2, downCount=1 }`. Validation: `1 ≤ upCount+downCount ≤ MaxDiodes`,
  `upCount ≠ downCount` (else it is the symmetric case).
- **`SeriesValues`** — `{ double Cc; double R; DiodeSpec diode; int seriesCount; }` — input
  coupling cap (series), the inline diode count `seriesCount` (v1 canonical `1` or `2` in series),
  a resistor to ground. Output ≡ the post-diode node.

Validation (all): every resistance / capacitance `> 0`, every count `≥ 1`, total diodes `≤ MaxDiodes`;
violation → `std::invalid_argument` on the build thread (FR-007). No silent clamp.

## Assembled netlist (per topology)

- **`Clipper<MaxNodes, MaxComponents>`** — `{ Netlist<MaxNodes, MaxComponents> netlist; NodeId inNode; NodeId outNode; NodeId portP; NodeId portN; }`
  — the emitted topology plus the input, output, and clipper-port node handles (the port is the
  node pair carrying the diode string, consumed by the solver).
- **Per-topology capacity aliases** (sized to the BOM, small): e.g.
  `SymmetricShuntClipper = Clipper<4, 8>`, `AsymmetricShuntClipper = Clipper<4, 8>`,
  `SeriesClipper = Clipper<5, 8>`. Exact `<MaxNodes, MaxComponents>` finalized in implementation
  to the smallest values that hold each topology's node/component count with a little headroom.

## Solver types (lab)

- **`NewtonStatus`** — `{ bool converged; int iterations; double voltageResidual; double currentResidual; }`
  — the per-sample convergence report (FR-011). `converged=false` is a legitimate, surfaced result.
- **`TransientClipper<MaxNodes, MaxComponents, MaxDiodes = 4>`** — the solver; owns reactive
  history and the previous-converged port voltage. Internal augmented netlist
  `Netlist<MaxNodes, MaxComponents + 2·MaxDiodes>` (R2). Holds a nested
  `LinearSolver<MaxNodes, MaxComponents + 2·MaxDiodes>`. Key operations in the contract file.

## DC-limit oracle equations (independent validation)

Steady-state (caps fully charged / open), used by the bisection root-find (FR-015):
- **Symmetric shunt:** `0 = v + R · 2·Is·sinh(v / (n·Vt)) − Vin` (antiparallel pair ⇒ `sinh`).
- **Asymmetric shunt:** `0 = v + R · [ upCount·Is·(exp(v/(nVt)) − 1) − downCount·Is·(exp(−v/(nVt)) − 1) ] − Vin`
  (unequal populations ⇒ asymmetric, non-zero DC offset).
- **Series (`seriesCount` diodes inline, DC ⇒ coupling cap blocks DC, so the steady-state through
  the diodes is 0 and the output settles to 0 V):** the *transient/AC* behavior is what the series
  form validates; its DC-limit check is the coupling-cap-blocks-DC invariant (output → 0 at DC),
  cross-checked against the settled transient. (Its clipping is validated dynamically, not at DC.)

## Reactive-signature measurement (FR-017/SC-005)

- **Excitation:** 1 kHz sine, fixed amplitude driven into clipping, at `dt = 1e-5 s` (100 kHz).
- **Metric:** output spectral energy above a 5 kHz cutoff (a fixed HF-band energy scalar).
- **Assertion:** strictly decreasing across an ascending `Cf` sweep (per topology with a `Cf`).

## State & lifecycle

- Builders: **stateless pure functions**; a BOM change is a fresh call (control-rate rebuild).
- Solver: holds reactive history + previous port voltage across `step()` calls; `reset()` returns
  it to a cold circuit. No heap on the `step()` path.
