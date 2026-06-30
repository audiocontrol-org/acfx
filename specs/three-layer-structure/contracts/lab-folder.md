# Contract: Lab Folder Shape

The required structure of a `core/labs/<concept>/` unit. Downstream phases copy this
shape; the SVF lab is the reference instance.

## Required members

```text
core/labs/<concept>/
├── README.md            # REQUIRED
├── <concept>-kernel.h   # REQUIRED pre-graduation; ABSENT post-graduation (moved to primitives/)
└── harness/             # REQUIRED
    └── <concept>-harness.cpp
```

### `README.md` — required sections

- **Theory** — the concept the lab introduces (background, the math/idea).
- **Walkthrough** — how the kernel implements it.
- **Graduation target** — the explicit primitive path the kernel graduates to
  (e.g. `core/primitives/filters/svf-primitive.h`). Post-graduation this reads as
  "graduated to <path>".
- **Measurements** — what the harness produces as evidence (per-mode frequency
  response, stability, …).

### Kernel (`<concept>-kernel.h`)

- Portable, RT-safe (no heap/locks in `process()`), no platform headers, ≤ 500 lines.
- Depends on `core/dsp/` only.
- **Graduation**: `git mv` into `core/primitives/<category>/` and refine in place;
  the lab folder persists (README + harness remain).

### `harness/`

- Host-only; a dedicated CMake target built under `test`/`desktop`, never in `daisy`/`teensy`.
- May allocate, plot, measure, and drive the kernel/primitive.
- May include `core/primitives/**`, `core/dsp/`, and the lab's kernel.
- **Nothing portable may include a harness header** (contract C-1).

## Lifecycle states

| State | Kernel location | Harness drives | README graduation line |
|---|---|---|---|
| pre-graduation | `core/labs/<c>/<c>-kernel.h` | the in-lab kernel | "graduates to → `<primitive path>`" |
| graduated | `core/primitives/<category>/…` | the graduated primitive | "graduated to `<primitive path>`" |

## Reference instance (this feature)

`core/labs/state-variable-filter/` ships in the **graduated** state: the kernel lives at
`core/primitives/filters/svf-primitive.h`; the lab provides `README.md` + a host-only
`harness/svf-harness.cpp` that emits per-mode frequency-response + high-resonance
stability evidence by driving the graduated `acfx::SvfPrimitive`.
