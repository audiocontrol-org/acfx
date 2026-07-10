# Data Model — WDF-primitives (Phase 1)

The primitive introduces **no new component types** to the frozen vocabulary
(Constitution / FR-015). These are the wave-domain one-port types and the state each owns.
All numeric values are `double`. Namespace `acfx::wdf`; header-only; C++17.

## The one-port port interface (concept)

A **duck-typed concept** every leaf satisfies (research R3) — NOT a virtual base class. The
binding surface (all `noexcept`):

| Member | Signature | Meaning |
|--------|-----------|---------|
| `portResistance` | `double portResistance() const noexcept` | the port reference resistance `Rp` |
| `reflected` | `double reflected() const noexcept` | the reflected wave `b` (up-sweep output) |
| `incident` | `void incident(double a) noexcept` | the incident wave `a` (down-sweep input) |
| adaptability | `static constexpr bool isAdaptable` (trait) | `true` if `b` is independent of the current-sample incident wave; `false` for reflective leaves |

**Call-ordering contract (set by `isAdaptable`, research R3):**
- `isAdaptable == true` — `reflected()` is valid **before** this sample's `incident()` (reads
  stored state or a constant): up-sweep reads `b`, down-sweep later sets `a`.
- `isAdaptable == false` (reflective) — `incident(a)` is called **first**, then `reflected()`
  returns `f(a)` (the root-evaluation order).

## Adaptable one-ports

All hold their port resistance `Rp` (computed once at construction); the wave path is
`noexcept`, heap-free.

### Resistor
| Field | Type | Meaning | Validation |
|-------|------|---------|------------|
| `R_` (→ `Rp_ = R`) | `double` | resistance / port resistance | `R > 0` at construction (throw) |
- `reflected() → 0` (adapted, reflection-free); `incident(a)` ignores `a`. `isAdaptable = true`,
  memoryless (no state). Construction: `Resistor(double R)`.

### Capacitor
| Field | Type | Meaning | Validation |
|-------|------|---------|------------|
| `Rp_ = T/(2C)` | `double` | bilinear port resistance | `C > 0`, `dt > 0` at construction (throw) |
| `state_` | `double` | previous incident wave `a[n−1]` (init `0`) | — |
- `reflected() → state_` (`b[n] = a[n−1]`, unit delay); `incident(a) → state_ = a`.
  `isAdaptable = true`, reactive (1 wave sample of state). Construction: `Capacitor(double C, double dt)`.

### Inductor
| Field | Type | Meaning | Validation |
|-------|------|---------|------------|
| `Rp_ = 2L/T` | `double` | bilinear port resistance | `L > 0`, `dt > 0` at construction (throw) |
| `state_` | `double` | previous incident wave `a[n−1]` (init `0`) | — |
- `reflected() → −state_` (`b[n] = −a[n−1]`, dual); `incident(a) → state_ = a`.
  `isAdaptable = true`, reactive. Construction: `Inductor(double L, double dt)`.

### ResistiveVoltageSource
| Field | Type | Meaning | Validation |
|-------|------|---------|------------|
| `R_` (→ `Rp_ = R`) | `double` | series resistance / port resistance | `R > 0` at construction (throw) |
| `E_` | `double` | drive value (mutable per sample, research R7) | — |
- `reflected() → E_` (adapted); `incident(a)` ignores `a`; `setVoltage(double E)` updates the
  per-sample drive (the audio input). `isAdaptable = true`, memoryless.
  Construction: `ResistiveVoltageSource(double R, double E = 0)`.

### ResistiveCurrentSource
| Field | Type | Meaning | Validation |
|-------|------|---------|------------|
| `R_` (→ `Rp_ = R`) | `double` | parallel resistance / port resistance | `R > 0` at construction (throw) |
| `I_` | `double` | drive value (mutable per sample) | — |
- `reflected() → R_ · I_` (adapted); `incident(a)` ignores `a`; `setCurrent(double I)` updates
  the drive. `isAdaptable = true`, memoryless.
  Construction: `ResistiveCurrentSource(double R, double I = 0)`.

### ResistiveTermination
- A resistor used as a matched load: `Rp_ = R`, `reflected() → 0`, `incident(a)` ignores `a`.
  `isAdaptable = true`, memoryless. May be the `Resistor` type reused, or a thin distinct type
  for intent — a code-shape call for the plan. Construction: `ResistiveTermination(double R)`.

## Reflective one-ports (non-adaptable)

Reflection depends on the current incident wave and is **independent of `Rp`** (research R6).
They carry an externally-imposed reference resistance so an adaptor can convert at the junction.

### ShortCircuit
| Field | Type | Meaning | Validation |
|-------|------|---------|------------|
| `Rp_` | `double` | externally-imposed port reference resistance | `Rp > 0` at construction (throw) |
| `a_` | `double` | last incident wave (set by `incident`) | — |
- `incident(a) → a_ = a`, then `reflected() → −a_` (`b = −a`). `isAdaptable = false`.
  Construction: `ShortCircuit(double Rp)`.

### OpenCircuit
| Field | Type | Meaning | Validation |
|-------|------|---------|------------|
| `Rp_` | `double` | externally-imposed port reference resistance | `Rp > 0` at construction (throw) |
| `a_` | `double` | last incident wave (set by `incident`) | — |
- `incident(a) → a_ = a`, then `reflected() → +a_` (`b = +a`). `isAdaptable = false`.
  Construction: `OpenCircuit(double Rp)`.

## Invariants

- **`Rp` computed once at construction** (from parameter + `dt` for reactive leaves); the wave
  path never recomputes it. No per-leaf re-prepare/sample-rate API in v1 (research R4).
- **Reactive leaves are stateful** (one `double` `state_`); memoryless leaves and sources hold
  no wave state (a source holds a drive value, not delayed wave state).
- **Determinism**: given identical `(state, incident sequence, drive values)`, a leaf yields an
  identical reflected sequence.
- **Zero heap on the wave path**: every field is an in-object `double`; no allocation, no locks.
- **Passivity is validated, not enforced** (two criteria, research R8): memoryless `|b| ≤ |a|`
  + `Rp > 0`; reactive wave-power balance `Σ(a² − b²) ≥ 0`.
- **No fallback**: non-physical parameters throw at construction; reflections are never clamped.

## Relationships / data flow (one leaf, one sample)

```text
construction (once):  parameter (+ dt for reactive)  ──▶  Rp   (fixed thereafter)
                                                          state_ := 0 (reactive)

per sample, adaptable leaf:        per sample, reflective leaf (short/open):
  up-sweep:   b = reflected()        down-sweep:  incident(a)   → a_ := a
              (state_ / constant)    then:        b = reflected() → ∓a_
  down-sweep: incident(a)
              reactive: state_ := a
              memoryless: (ignored)
  source drive (optional): setVoltage(E) / setCurrent(I) between samples
```

## Consumed existing types (unchanged)

- `acfx::Resistor` / `acfx::Capacitor` / `acfx::Inductor` / `acfx::VoltageSource` /
  `acfx::CurrentSource` (`core/primitives/circuit/models/`) — the **physical constants**
  (`R`/`C`/`L`/`V`/`I`) are reused as construction parameters. WDF leaves do **not** embed these
  nodal structs and carry no `NodeId` (research R5). Stateless nodal value types, unchanged.
- `tests/support/allocation-sentinel.h` — reused for the zero-heap assertion (test-only).

## Out of model (deferred to sibling nodes)

- Series/parallel **adaptors** and their scattering coefficients (`wdf-adaptors`).
- **Tree** assembly and the reflection-free **adaptation** algorithm (`wdf-passive-networks`).
- **Ideal (non-resistive) voltage source** (`b = 2E − a`) and **nonlinear root** (diode)
  one-ports (`wdf-complete-analog-stages`) — the port interface is designed so these satisfy the
  same `reflected()`/`incident()` protocol as reflective leaves (research R3), but they are not
  built here.
