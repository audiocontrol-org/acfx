# Research — WDF-primitives (Phase 0)

Implementation-shaping decisions. Each: Decision / Rationale / Alternatives considered.
No `NEEDS CLARIFICATION` remained after the operator-approved, third-party-reviewed design
record; these resolve the implementation-level unknowns the plan surfaced.

## R1 — Voltage-wave convention and the per-element scattering relations

**Decision.** Adopt **voltage waves** with an explicit port reference resistance `Rp`:
`a = v + Rp·i`, `b = v − Rp·i`, inverse `v = (a + b)/2`, `i = (a − b)/(2·Rp)` (current `i`
referenced into the port). The per-element adapted scattering, derived from each
constitutive law:

| One-port | constitutive law | port resistance `Rp` | reflected wave `b` |
|---|---|---|---|
| Resistor | `v = R·i` | `R` | `0` (adapted) |
| Capacitor | `i = C·dv/dt` (bilinear, R2) | `T/(2C)` | `a[n−1]` (unit delay) |
| Inductor | `v = L·di/dt` (bilinear, R2) | `2L/T` | `−a[n−1]` |
| Resistive voltage source | `v = E − R·i` | `R` | `E` (adapted) |
| Resistive current source | `i = I − v/R` | `R` | `R·I` (adapted) |
| Resistive termination | `v = R·i` | `R` | `0` (matched) |
| Short circuit | `v = 0` | (Rp-independent, R6) | `−a` |
| Open circuit | `i = 0` | (Rp-independent, R6) | `+a` |

**Rationale.** For a linear one-port `v = R·i`: `b = v − Rp·i = (R − Rp)·i` and
`a = (R + Rp)·i`, so `b = a·(R − Rp)/(R + Rp)`, reflection-free at `Rp = R`. Thévenin source
`v = E − R·i` gives `b = a·(R−Rp)/(R+Rp) + 2Rp/(R+Rp)·E`, which at `Rp = R` collapses to
`b = E`; the Norton dual gives `b = R·I` at `Rp = R`. Short (`v = 0`): `b = −Rp·i = −a`; open
(`i = 0`): `b = v = a` — both independent of `Rp`. Voltage waves are the modern virtual-analog
standard and keep the arithmetic in plain `double` with no `√Rp` normalization.

**Alternatives considered.** (a) **Power / normalized waves** (`a = (v + Rp·i)/(2√Rp)`) —
rejected (design Approach B): `√Rp` bookkeeping at every port for no benefit at the leaf
layer, less aligned with the contemporary references. (b) Current waves — same objection,
non-standard for this work.

## R2 — Bilinear discretization of the reactive one-ports (the unit delay)

**Decision.** Discretize capacitor and inductor with the **bilinear transform**
`s → (2/T)·(1 − z⁻¹)/(1 + z⁻¹)` (`T = dt`). This yields a **frequency-independent** port
resistance plus a **unit delay**:
- Capacitor: `Rp = T/(2C)`, reflected `b[n] = a[n−1]` (store the incident wave, reflect it
  next sample).
- Inductor: `Rp = 2L/T`, reflected `b[n] = −a[n−1]` (dual, sign-inverted).

**Rationale.** Bilinear is what makes a reactance a **memoryless-within-sample** port
resistance in series with a pure delay — the property the whole WDF paradigm and its adaptors
rely on, and the one that preserves passivity. Substituting the capacitor admittance
`Y(s) = sC` under bilinear and forming `b/a` gives exactly `z⁻¹` at port resistance `T/(2C)`;
the inductor is the dual. The known cost is bilinear frequency warping, addressed downstream
by oversampling (an existing primitive), never by a per-leaf rule change.

**Alternatives considered.** (a) **Selectable discretization at the leaf** (backward/forward
Euler, mirroring `implicit-integration`) — rejected (design Approach C): non-bilinear
discretizations give a frequency-dependent or non-passive port relation and break the clean
`b[n] = ±a[n−1]` unit delay the adaptors depend on. In the wave domain, bilinear is canonical
and non-optional at the leaf. (b) Trapezoidal-with-a-companion (the nodal approach) — that is
the *other* paradigm (implicit-integration); WDF's whole point is the delay, not a companion.

## R3 — The one-port port interface: a duck-typed concept + the up/down-sweep protocol

**Decision.** The port interface is a **duck-typed C++ concept**, not a virtual base class.
Every leaf exposes, all `noexcept`:
- `double portResistance() const` — `Rp`, consumed by a parent adaptor for its coefficients.
- `double reflected() const` — `b`, the up-sweep output.
- `void incident(double a)` — the down-sweep input.
- an **adaptable/reflective** classifier (a compile-time trait and/or a `constexpr` bool):
  adaptable leaves (`b` independent of the *current-sample* incident wave) vs. reflective
  leaves (`b` depends on the current incident wave).

The classifier fixes the **call ordering** within a sample:
- **Adaptable** leaf: `reflected()` is meaningful **before** this sample's `incident()` (it
  reads stored state or a constant) → the up-sweep reads `b`, the down-sweep later sets `a`.
- **Reflective** leaf (short/open, and downstream ideal/nonlinear roots): `b` depends on the
  current incident wave, so `incident(a)` is called **first**, then `reflected()` returns
  `f(a)` — the root-evaluation order.

**Rationale.** The concept mirrors the shipped `CompanionSupply` seam (Constitution
Principle VII — composition over inheritance; no vtable on the wave path, fully inlinable).
Making the adaptable/reflective distinction a first-class classifier gives it real semantic
purpose: it is exactly the information a downstream adaptor needs to sequence `reflected()`
vs. `incident()` correctly and to know which port can be left unadapted (the root). Defining
this here — not letting the adaptor node retro-define it — is what makes this node the
foundation.

**Alternatives considered.** (a) **Virtual `OnePort` base class** — rejected (design
Approach F): a vtable indirection per port per sample on the hot path, and inheritance is
disfavored. (b) A single fixed call order for all leaves — rejected: it cannot express the
reflective root (whose `b` genuinely depends on the current `a`); the classifier is the
honest encoding of the two timings.

## R4 — Leaf lifecycle: construct with (parameter, dt); Rp once; no re-prepare

**Decision.** Reactive leaves are **constructed with their physical parameter and `dt`**
(`Capacitor(C, dt)`, `Inductor(L, dt)`), computing `Rp` **once in the constructor**;
memoryless leaves are constructed with their fixed parameter (`Resistor(R)`, etc.). There is
**no per-leaf `prepare()`/re-prepare/`setSampleRate` API in v1**. **Fixed vs. per-sample:**
fixed at construction = parameter, `dt`, and the derived `Rp`; mutable per sample = a reactive
leaf's stored wave `state` (via `incident`) and a source's drive value `E`/`I` (via a setter,
R7). Construction validates parameters (throw-permitted, off the hot path); the wave path is
`noexcept`.

**Rationale.** Pinned by the third-party design review: the nodal siblings' two-phase
`prepare()` shape sizes a *whole netlist* and does not apply to a single one-port — a leaf has
nothing to size, only `Rp` to compute from `dt`. Explicit `(param, dt)` construction removes
the constructed-vs-prepared ambiguity and keeps the wave path branch-free. A sample-rate change
is handled by **reconstruction** + downstream tree re-adaptation, off the hot path — the need
is captured (spec Open Questions 2–3), the API is not built at the leaf.

**Alternatives considered.** (a) A separate leaf-level `prepare(dt)` phase — rejected
(over-applies the netlist-scale pattern; re-introduces the ambiguity the review closed).
(b) In-place `setSampleRate(dt)` mutation — rejected for v1 (a `dt` change forces tree
re-adaptation anyway; a silent in-place mutation would desync a leaf from the adaptor
coefficients derived from its old `Rp`).

## R5 — Reuse the frozen physical constants, not the nodal container

**Decision.** WDF leaves reuse the **physical constants** (`R`/`C`/`L`/`E`/`I`) of the frozen
vocabulary but are their **own wave-domain types** under `acfx::wdf`: they carry **no
`NodeId`** and reactive leaves **hold state**. They are NOT literal readers of the nodal
`Component{a, b, …}` variant. A leaf may be constructed from a plain physical value (e.g. a
`double R`) or, as a convenience, from a frozen value type's parameter accessor
(`Capacitor::C()` etc.) — but it does not embed the nodal struct.

**Rationale.** `DEVELOPMENT-NOTES.md:232` frames WDF as a wave-domain **reader** of the
immutable vocabulary: the same physical circuit can be modeled nodally or as a WDF tree. The
nodal `Resistor{a, b, R}` carries `NodeId` topology meaningless in a WDF tree and is
deliberately stateless (wrong for a reactive WDF leaf). Reusing the *constants* honors the
solver-neutral seam without dragging nodal topology or statelessness into the wave family
(design Approach D rejection).

**Alternatives considered.** (a) WDF leaves as literal readers of `Component` — rejected
(smuggles `NodeId`/statelessness into the wave family). (b) A `Component → WDF-leaf`
convenience adapter in v1 — deferred: that mapping needs a tree context and belongs to the
passive-networks/assembly node, not the leaf.

## R6 — Reflective one-ports (short / open): Rp-independent reflection, non-adaptable

**Decision.** Short (`b = −a`) and open (`b = +a`) have reflections **independent of the port
resistance**, so they are classified **non-adaptable** (R3). They still expose
`portResistance()` — carrying the **externally-imposed reference resistance** of the port they
terminate (supplied at construction; independent of the reflection) — so an adaptor can convert
between wave and Kirchhoff variables at the junction. In v1 the reference resistance defaults to
a caller-supplied value; the reflection `∓a` never depends on it.

**Rationale.** From `v = 0` / `i = 0` and the voltage-wave definitions, the reflection is
`∓a`/`±a` for any `Rp` (R1). A short/open cannot be made reflection-free, so it is exactly the
kind of port that a tree leaves **unadapted** (a root position) — but *where* it may sit is the
adaptor/tree node's decision (design Decision 12). This node only reports the fact via the
classifier and provides the `∓a` scattering.

**Alternatives considered.** (a) Omit `portResistance()` for reflective leaves — rejected:
breaks the uniform concept the adaptors consume; a port still needs a reference resistance for
wave↔KCL conversion even when the element's reflection is `Rp`-independent. (b) Treat short/open
as special adaptor cases rather than leaves — rejected: the roadmap names them as terminations
in *this* node, and they are legitimate one-ports.

## R7 — Source drive-value update (the signal-input path)

**Decision.** A resistive voltage source's drive value `E` (and a current source's `I`) is
**mutable per sample** via a setter (`setVoltage(double)` / `setCurrent(double)`); the port
resistance stays fixed at construction. The adapted reflected wave tracks the current drive
value (`b = E` / `b = R·I`). This is the audio-**input** injection point.

**Rationale.** In a WDF, the input signal enters as the time-varying source value of a leaf; the
source's `Rp = R` is constant while `E` changes each sample. The setter keeps the drive update
off the `Rp` computation and on the (still `noexcept`, heap-free) wave path. Whether a broader
input/output helper belongs at the leaf or the network layer is spec Open Question 1 (leaning:
assembly concern) — v1 provides only the per-leaf setter.

**Alternatives considered.** (a) Reconstruct the source each sample to change `E` — rejected
(needless, and construction is where validation/throws live). (b) Pass `E` into `reflected()` —
rejected: breaks the uniform zero-argument `reflected()` the concept requires.

## R8 — No-fallback parameter validation; passivity as a validated property (two criteria)

**Decision.** Non-physical parameters (`R ≤ 0`, `C ≤ 0`, `L ≤ 0`, `dt ≤ 0`) are surfaced as
construction-time errors (throw, off the hot path), never clamped or substituted. Reflections
are **never** clamped to force apparent passivity. Passivity is **validated**, with **two
distinct criteria** by element memory:
- **Memoryless** passive leaves (adapted resistor, resistive termination): the **instantaneous**
  bound `|b| ≤ |a|` and `Rp > 0`.
- **Reactive** leaves (capacitor, inductor): a **wave-power balance across state transitions** —
  the accumulated absorbed wave energy `Σ(a[k]² − b[k]²)` stays `≥ 0` (for the lossless
  capacitor it telescopes to the currently-stored `a[N]²`), NOT same-sample `|b| ≤ |a|`.

**Rationale.** Constitution Principle V and the nodal siblings' precedent (no silent gmin, no
fabricated output). The two-criteria split is a correction from the third-party spec review:
same-sample `|b| ≤ |a|` is **invalid** for a reactive leaf because `b[n] = a[n−1]` returns
energy stored from a previous sample (`a[n−1]=1, a[n]=0 → b[n]=1 > |a[n]|=0` for a *correct*
capacitor). The wave-power identity `a² − b² = 4·Rp·(v·i)` makes `Σ(a² − b²)` proportional to
the net electrical energy absorbed; for the capacitor it telescopes to `a[N]² ≥ 0`, proving
energy is **stored and returned**, never created or dissipated.

**Alternatives considered.** (a) Same-sample `|b| ≤ |a|` for all leaves — rejected: it would
reject correct reactive implementations (the review's blocking finding). (b) Clamp `Rp` or `b`
to a safe range on a bad parameter — rejected (Principle V; a masked non-physical state is a
bug factory).

## R9 — Validation strategy and tolerances (per-element only)

**Decision.** Per the recorded `circuit-model-validation-approach`, all **per-element** (no
adaptors/tree exist yet):
- **Exact scattering closed forms** — adapted resistor `b = 0`; resistive voltage source
  `b = E`; resistive current source `b = R·I`; capacitor unit delay `b[n] = a[n−1]`; inductor
  `b[n] = −a[n−1]`; short `b = −a`; open `b = +a`. Exact (tolerance `≈1e-12`; the delay and
  sign relations are exact).
- **Reactive wave-power balance** — drive an incident-wave sequence and assert
  `Σ(a[k]² − b[k]²) ≥ 0` at every prefix, equal to the stored `a[N]²` for the lossless
  capacitor (R8).
- **Bilinear-impedance agreement** — a reactive leaf's discrete port impedance matches the
  bilinear-discretized analog impedance at test frequencies.
- **Duality** — capacitor/inductor port resistance and reflected-wave sign are swapped under the
  same `dt`.
- **Memoryless passivity** — `|b| ≤ |a|`, `Rp > 0` for the memoryless passive leaves.
- **Zero-heap** — `AllocationSentinel` around a many-sample wave loop asserts
  `allocations() == 0 && deallocations() == 0`.
- **No-fallback** — non-physical parameters throw at construction; no reflection is clamped.
  **Not** transcribed published values.

**Rationale.** Proves each one-port exact where the scattering math is closed-form, proves the
reactive elements are passive/lossless by the *correct* (state-aware) criterion, and enforces
the RT + no-fallback contracts — all without a tree (full-circuit frequency response and
adaptation correctness are explicitly deferred to the adaptor/network nodes, spec FR-023).

**Alternatives considered.** (a) Full-circuit response validation here — rejected: no adaptors
exist yet; that is the passive-networks node's job. (b) Transcribed SPICE/published traces —
rejected (brittle, not first-principles; the memory forbids it).

## R10 — Placement: `core/primitives/circuit/wdf/` (co-located) vs top-level `wdf/`

**Decision.** Place the family under `core/primitives/circuit/wdf/`, namespace `acfx::wdf`,
sibling to `acfx::mna` / `acfx::newton` / `acfx::integration`.

**Rationale.** WDF fundamentally models circuits and reuses the circuit element physics, and the
three most recent sibling primitives all established the `circuit/<name>/` co-location. This
keeps the whole circuit-modeling + solver + WDF family together.

**Alternatives considered.** The prospectus's primitive diagram lists a **top-level
`primitives/wdf/`** peer to `circuit/` (a paradigm-separation signal) — noted as revisitable at
plan time if the later phase nodes argue for it (spec Open Question 5), but co-location wins for
v1 on physics reuse and sibling consistency.
