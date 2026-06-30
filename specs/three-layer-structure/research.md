# Research: Three-Layer DSP Core Structure

Phase 0 decisions. The feature is structural, so "research" here resolves the
mechanism choices the plan implies. No external/library research is required — no
new dependency is introduced.

## Decision 1 — Migration mechanism: `git mv`, not copy-then-delete

- **Decision**: Move each existing primitive with `git mv core/primitives/<x>.h core/primitives/<category>/<x>.h`.
- **Rationale**: Preserves file history across the move (honors Principle IX "evolve, never discard" — the graduated kernel is the same lineage, refined in place, not re-derived). The header bodies are unchanged by the move itself; only consumer include paths change.
- **Alternatives considered**: Copy + delete (loses `git log --follow` continuity, reads as throw-away-and-rewrite — rejected). Leaving symlinks/shims at old paths (a fallback path the constitution forbids; would let stale includes silently survive — rejected).

## Decision 2 — Include-path updates resolve against the unchanged `core/` root

- **Decision**: After the move, update the 5 consumer references to the new paths; the `acfx_core` INTERFACE include dir (`core/`) is unchanged.
- **Rationale**: Includes are written `#include "primitives/svf-primitive.h"` and resolve against `target_include_directories(acfx_core INTERFACE .../core)`. Moving a header deeper in `core/` only lengthens the path string (`primitives/filters/svf-primitive.h`); no CMake include-dir change is needed. DaisySP's `#include "Filters/svf.h"` inside the SVF primitive resolves against DaisySP's own include dir and is unaffected by the move.
- **Consumers to re-point** (from `grep`): `core/effects/svf/svf-effect.h`; `core/effects/modulated-delay/{wow-flutter.h,modulated-delay-effect.h}`; `tests/core/{lfo-test.cpp,delay-line-test.cpp}`.
- **Alternatives considered**: Adding per-category include dirs so includes stay short (`#include "svf-primitive.h"`) — rejected: hides the taxonomy at the call site and multiplies include dirs; the category in the path is informative.

## Decision 3 — Lab harness build: a new host-only CMake target, excluded from MCU presets

- **Decision**: The SVF lab harness (`core/labs/state-variable-filter/harness/svf-harness.cpp`) builds as a dedicated host-only target (e.g. `acfx_lab_svf_harness`), wired under the `test`/`desktop` configuration only and absent from the `daisy`/`teensy` presets.
- **Rationale**: Enforces FR-005/SC-005 (harness never in an MCU cross-compile) at the build-graph level, not just by the grep gate. Keeps the harness out of the header-only `acfx_core` (which every target links). The harness MAY link `acfx_core` to drive the graduated primitive — that direction (harness → core) is allowed; the forbidden direction is core → harness.
- **Alternatives considered**: Compiling the harness into `acfx_core_tests` (conflates "lab evidence driver" with "regression suite", and pulls harness code into the test binary's link — rejected; the harness reuses measurement *intent* but is its own artifact). A standalone shell/Python plotter outside CMake (adds a non-C++ toolchain dependency for a parked open question — rejected for this round).

## Decision 4 — Taxonomy document location: `core/primitives/README.md`

- **Decision**: The full intended taxonomy lives in `core/primitives/README.md`, co-located with the categories it documents.
- **Rationale**: A reader listing `core/primitives/` sees the categories that exist *and* a README naming the ones that don't yet (the prospectus families + code-forced `delays/`/`modulation/`). Documentation sits next to the thing it describes; no empty `.gitkeep` dirs needed (FR-008, SC-006).
- **Alternatives considered**: A top-level `docs/` taxonomy page (further from the code it governs — rejected). Empty placeholder dirs with `.gitkeep` (the cruft FR-008 explicitly forbids — rejected).

## Decision 5 — Gate implementation: extend the existing grep-based checks in style

- **Decision**: Add two checks to `scripts/check-portability.sh` mirroring its existing `grep -rEn ... && fail=1` idiom: (a) **harness isolation** — no portable-core file (`core/dsp`, `core/primitives`, `core/effects`, and `core/labs/*` kernels, i.e. `core/labs` excluding `*/harness/`) contains an `#include` of a `labs/*/harness/` path; (b) **dependency direction** — no `core/primitives/**` file includes a `core/effects/**` path. Extend the existing file-size (check 1) and platform-header (check 2) scans to cover `core/labs` (already covered for size since the `find` includes `core`; explicitly include `core/labs` kernels in the platform-header scan).
- **Rationale**: Reuses the gate that already exists and already runs in CI; matches its bash idiom and exit-code contract; adds no new tool. Grep over `#include` lines is sufficient for these include-graph invariants in a header-rooted core.
- **Alternatives considered**: A compiled include-graph analyzer / `clang-scan-deps` (heavier, new toolchain dependency for two simple invariants — rejected). A separate new gate script (fragments the single quality-gate entry point — rejected; FR-016 says extend the existing one).

## Decision 6 — Harness evidence reuses existing measurement intent

- **Decision**: The SVF harness produces per-mode frequency-response + high-resonance stability evidence by exercising the graduated `SvfPrimitive` the way the existing `tests/core/svf-test.cpp` and measurement tests already do, rather than introducing a new measurement framework.
- **Rationale**: FR-013/Assumption — the lab/harness *output contract* (plot vs CSV vs assertions) is a parked open question; this round the harness demonstrates the kernel and emits human-readable measurement output without committing to a standardized format.
- **Alternatives considered**: Defining a formal harness output schema now (contradicts the parked open question; premature before a second lab exists — rejected).
