# Contract: Layering Rules (enforced by `scripts/check-portability.sh`)

The mechanically-checked invariants this feature adds. Each is a predicate over the
source tree with a definite pass/fail; the gate exits non-zero and names the first
violation. These extend — never replace — the gate's existing four checks.

## C-1 — Lab-harness isolation (FR-005, SC-005)

**Rule**: No *portable* source file `#include`s a path under any `labs/*/harness/`.
Portable source = `core/dsp/**`, `core/primitives/**`, `core/effects/**`, and lab
**kernels** (`core/labs/**` excluding `*/harness/**`).

- **Pass**: a harness includes `primitives/filters/svf-primitive.h` (harness → core, allowed).
- **Fail**: `core/effects/svf/svf-effect.h` includes `labs/state-variable-filter/harness/svf-harness.h` (core → harness, forbidden).

**Check shape**: grep portable files for `#include` lines matching `labs/[^/]*/harness/`; any match ⇒ fail.

## C-2 — Dependency direction: primitives never include effects (FR-015)

**Rule**: No file under `core/primitives/**` `#include`s a path under `core/effects/**`.

- **Pass**: `effects/svf/svf-effect.h` includes `primitives/filters/svf-primitive.h`.
- **Fail**: `primitives/filters/svf-primitive.h` includes `effects/svf/svf-effect.h`.

**Check shape**: grep `core/primitives/**` for `#include` lines matching `effects/`; any match ⇒ fail.

## C-3 — Harness never in an MCU cross-compile (FR-005, SC-005)

**Rule**: No `labs/*/harness/**` source is part of the `daisy` or `teensy` target sources.

- **Enforced primarily at the build graph**: the harness is a host-only CMake target absent from the MCU presets (research Decision 3).
- **Gate backstop**: the MCU-adapter scan (existing check 3) is extended so a harness path appearing in `adapters/daisy` / `adapters/teensy` build inputs ⇒ fail.

## C-4 — Existing checks extended over `core/labs` (FR-017)

- **File-size budget** (existing check 1): already globs `core` — confirm `core/labs/**` `.h`/`.cpp` are within the ≤ 500-line budget.
- **No platform headers in core** (existing check 2): the scan covers `core/`; lab **kernels** must stay platform-free. Harness sources are host-only and exempt (they legitimately use host/plot/JUCE-free measurement code) — the scan must therefore target `core/` *excluding* `core/labs/*/harness/`.

## Exit contract

Unchanged from today: `exit 0` iff every check (the original four + C-1..C-4) passes;
`exit 1` on any violation with a `FAIL <detail>` line; `exit 2` on usage error.
Runs locally and in CI; never a git hook.
