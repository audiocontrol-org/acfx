# Contract — WDF one-ports (public header API)

Headers under `core/primitives/circuit/wdf/` · Namespace `acfx::wdf` · C++17, header-only.
This is the primitive family's public surface. Signatures are illustrative of the contract
(names/shape may refine in implementation); the **behavioral guarantees** are binding.

## The one-port port interface (concept)

```cpp
namespace acfx::wdf {

// Duck-typed concept (NOT a virtual base class). A type T is a OnePort iff it exposes:
//   double portResistance() const noexcept;   // Rp
//   double reflected()      const noexcept;    // b  (up-sweep output)
//   void   incident(double a) noexcept;        // a  (down-sweep input)
//   static constexpr bool isAdaptable;         // b independent of the current-sample a?
// (Expressed as a C++17 concept-emulation trait / SFINAE check, or a documented duck type;
//  no inheritance, no vtable on the wave path.)

} // namespace acfx::wdf
```

- **I1 (uniform surface).** Every leaf below satisfies this interface; `portResistance()`,
  `reflected()`, `incident()` are all `noexcept` and heap-free.
- **I2 (call ordering by `isAdaptable`).** `isAdaptable == true` → `reflected()` is valid
  **before** this sample's `incident()` (reads stored state / a constant). `isAdaptable ==
  false` (reflective) → `incident(a)` is called **first**, then `reflected()` returns `f(a)`.
- **I3 (static dispatch).** A generic up/down-sweep over a heterogeneous set of leaves resolves
  through the concept with **no** virtual dispatch (templates), satisfying SC-005.

## Voltage-wave convention (all leaves)

- **W1.** `a = v + Rp·i`, `b = v − Rp·i`; inverse `v = (a + b)/2`, `i = (a − b)/(2·Rp)`; current
  `i` referenced into the port (research R1).

## Adaptable leaves

### `Resistor`
```cpp
explicit Resistor(double R);
double portResistance() const noexcept;   // R
double reflected()      const noexcept;    // 0  (adapted)
void   incident(double a) noexcept;        // no-op (a absorbed)
static constexpr bool isAdaptable = true;
```
- **R1.** Throws `std::invalid_argument` if `R <= 0` (construction, off the hot path).
- **R2.** `portResistance() == R`; adapted `reflected() == 0` for any incident history.
- **R3.** The general unadapted reflection `b = a·(R − Rp)/(R + Rp)` is an analytical **test
  oracle only** — NOT a public method (arbitrary reference resistance is the adaptor layer's
  concern; spec FR-002).

### `Capacitor`
```cpp
Capacitor(double C, double dt);
double portResistance() const noexcept;   // T/(2C),  T = dt
double reflected()      const noexcept;    // state (= a[n-1]); 0 initially
void   incident(double a) noexcept;        // state := a
static constexpr bool isAdaptable = true;
```
- **C1.** Throws if `C <= 0` or `dt <= 0`. `Rp = T/(2C)` computed once in the constructor.
- **C2.** Unit delay: after `incident(a[n])`, the next sample's `reflected()` returns `a[n]`
  (`b[n] = a[n−1]`). Initial state is `0` → `b[0] = 0`.

### `Inductor`
```cpp
Inductor(double L, double dt);
double portResistance() const noexcept;   // 2L/T
double reflected()      const noexcept;    // -state (= -a[n-1])
void   incident(double a) noexcept;        // state := a
static constexpr bool isAdaptable = true;
```
- **L1.** Throws if `L <= 0` or `dt <= 0`. `Rp = 2L/T` computed once in the constructor.
- **L2.** Dual of the capacitor: `reflected()` returns `−state`; `b[n] = −a[n−1]`.

### `ResistiveVoltageSource`
```cpp
explicit ResistiveVoltageSource(double R, double E = 0.0);
double portResistance() const noexcept;   // R
double reflected()      const noexcept;    // E  (adapted)
void   incident(double a) noexcept;        // no-op
void   setVoltage(double E) noexcept;      // per-sample drive (audio input)
static constexpr bool isAdaptable = true;
```
- **VS1.** Throws if `R <= 0`. `portResistance() == R`; `reflected() == E` (current drive).
- **VS2.** `setVoltage(E)` updates the per-sample drive (research R7); the reflected wave tracks
  it. `Rp` is unaffected.

### `ResistiveCurrentSource`
```cpp
explicit ResistiveCurrentSource(double R, double I = 0.0);
double portResistance() const noexcept;   // R
double reflected()      const noexcept;    // R * I  (adapted)
void   incident(double a) noexcept;        // no-op
void   setCurrent(double I) noexcept;      // per-sample drive
static constexpr bool isAdaptable = true;
```
- **CS1.** Throws if `R <= 0`. `portResistance() == R`; `reflected() == R·I`.

### `ResistiveTermination`
```cpp
explicit ResistiveTermination(double R);
double portResistance() const noexcept;   // R
double reflected()      const noexcept;    // 0  (matched load)
void   incident(double a) noexcept;        // no-op
static constexpr bool isAdaptable = true;
```
- **RT1.** Throws if `R <= 0`. Matched load: `reflected() == 0`. (May reuse `Resistor`; a
  distinct type is optional for intent — a code-shape call.)

## Reflective leaves (non-adaptable)

### `ShortCircuit`
```cpp
explicit ShortCircuit(double Rp);
double portResistance() const noexcept;   // Rp  (externally-imposed reference)
double reflected()      const noexcept;    // -a  (from the last incident)
void   incident(double a) noexcept;        // a_ := a
static constexpr bool isAdaptable = false;
```
- **SH1.** Throws if `Rp <= 0`. Reflection `b = −a` is **independent of `Rp`** (research R6);
  `Rp` is carried only for junction wave↔KCL conversion.
- **SH2.** Call order: `incident(a)` then `reflected()` returns `−a` (reflective, I2).

### `OpenCircuit`
```cpp
explicit OpenCircuit(double Rp);
double portResistance() const noexcept;   // Rp
double reflected()      const noexcept;    // +a
void   incident(double a) noexcept;        // a_ := a
static constexpr bool isAdaptable = false;
```
- **OP1.** Throws if `Rp <= 0`. Reflection `b = +a`, independent of `Rp`.

## Cross-cutting guarantees

- **G1 (RT-safety).** `portResistance` / `reflected` / `incident` (and the source setters) are
  `noexcept`, allocation-free, lock-free, O(1). All state is in-object `double` (SC-004).
- **G2 (no fallback).** The only throws are construction-time parameter validation
  (`param <= 0` → `std::invalid_argument`), off the hot path. `Rp` is never clamped; reflections
  are never limited to force apparent passivity (spec FR-016).
- **G3 (passivity — validated, two criteria).** Memoryless passive leaves satisfy `|b| ≤ |a|`
  and `Rp > 0`; reactive leaves satisfy the wave-power balance `Σ(a[k]² − b[k]²) ≥ 0` (NOT
  same-sample `|b| ≤ |a|`), asserted by tests (spec FR-017/FR-021).
- **G4 (duality).** `Capacitor` and `Inductor` are duals: `Rp = T/2C` / `b = +state` vs.
  `Rp = 2L/T` / `b = −state` under the same `dt`.

## Non-goals (out of contract)

- No adaptors, no series/parallel scattering junctions (`wdf-adaptors`).
- No tree assembly, no reflection-free adaptation algorithm (`wdf-passive-networks`).
- No ideal (non-resistive) voltage source (`b = 2E − a`) and no nonlinear root (diode)
  (`wdf-complete-analog-stages`) — the interface admits them (I2 reflective ordering) but does
  not implement them.
- No `NodeId` topology (WDF topology is the tree); no consumption of the nodal solvers
  (MNA/Newton/integration) — WDF is a parallel reader of the physical vocabulary.
- No per-leaf variable-`dt`/`setSampleRate` path (fixed at construction; spec Open Question 2);
  no complex/AC scalar (v1 real `double`).
