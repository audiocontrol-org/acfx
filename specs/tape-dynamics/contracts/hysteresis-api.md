# Contract: `Hysteresis` primitive API

**Header**: `core/primitives/nonlinear/hysteresis.h`
**Namespace**: `acfx`
**Layer**: graduated primitive (first *stateful* member of `nonlinear/`)
**Satisfies**: FR-001..007, FR-016, SC-001, SC-005, SC-006

Platform-independent (`<cmath>`/`<array>` only; no JUCE/libDaisy/Teensy). RT-safe: no heap/locks in
`process()`; all sizing at construction/`prepare()`.

## Types

```cpp
enum class Solver : std::uint8_t { rk2, rk4, newtonRaphson };

struct JAParams {
    double Ms;     // saturation magnetization (ceiling)        > 0
    double a;      // anhysteretic shape                         > 0
    double alpha;  // inter-domain coupling                      >= 0
    double k;      // coercivity (loop width / memory)           > 0
    double c;      // reversibility (loop openness)              0..1
};
```

## Surface

```cpp
class Hysteresis {
public:
    // Configuration — call outside the audio hot path.
    void prepare(double sampleRate) noexcept;   // set integrator step size
    void reset() noexcept;                       // M = 0, Hprev = 0 (defined initial condition)

    void setParams(const JAParams& p) noexcept;  // clamps to constrained domain
    void setMs(double) noexcept;                  // per-parameter setters (FR-002)
    void setA(double) noexcept;
    void setAlpha(double) noexcept;
    void setK(double) noexcept;
    void setC(double) noexcept;
    void setSolver(Solver) noexcept;

    // Audio path — RT-safe, one high-rate step. Returns magnetization-derived output.
    [[nodiscard]] float process(float H) noexcept;

private:
    [[nodiscard]] double dMdH(double H, double M, double dH) const noexcept; // shared by all solvers
};
```

## Contract guarantees

- **C1 (memory)**: driven by a sinusoidal `H`, the `(H, process(H))` trace forms a **closed loop with
  area > 0** (SC-001). A static waveshaper would be single-valued (area ≈ 0).
- **C2 (reproducibility)**: after `reset()`, identical input sequences produce identical output (no
  hidden global state; FR-003).
- **C3 (finiteness)**: for any finite `H` and any `Solver`, `process` returns a finite value — the
  stiff-solver guard clamps non-finite/out-of-range intermediate state to a defined stable value
  (FR-006, SC-005). Newton's iteration count is bounded and bails to the explicit estimate on
  non-convergence (no unbounded loop — Constitution VI).
- **C4 (solver agreement)**: for a fixed input, the loops produced under rk2/rk4/newtonRaphson agree
  within a stated tolerance, tightening as the caller's oversampling factor rises (SC-002). (Oversampling
  is applied by the *consumer* — the primitive integrates one step per call.)
- **C5 (RT-safety)**: `process()` performs no heap allocation and takes no locks; work is O(solver
  stages) bounded (FR-007, SC-007).
- **C6 (parameter response)**: increasing `k` widens the loop; increasing `Ms` raises the saturation
  ceiling (FR-002, acceptance US2.2).

## Consumers

`core/effects/tape-dynamics/tape-dynamics-core.h` (run under `Oversampler<Factor>`), plus external
primitive consumers. Unit-tested by `tests/core/hysteresis-test.cpp`.
