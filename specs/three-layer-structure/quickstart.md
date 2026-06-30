# Quickstart / Validation Guide: Three-Layer DSP Core Structure

How to prove the feature works end-to-end. Each scenario maps to a user story and
its acceptance criteria. Run from the repo root.

## Prerequisites

- The repo builds today (`cmake --preset test` succeeds on `main`).
- No new dependency is required by this feature.

## Scenario A — Structure exists, SVF migrated, tests unchanged (US1 / SC-001, SC-002)

```bash
# The three layers + substrate are present:
ls core/labs core/primitives core/effects core/dsp
# SVF is graduated into the taxonomy:
ls core/primitives/filters/svf-primitive.h
# The lab exists with README + host-only harness:
ls core/labs/state-variable-filter/README.md core/labs/state-variable-filter/harness

# Host tests pass UNCHANGED (no DSP behavior change from the move):
cmake --preset test
cmake --build --preset test
ctest --preset test
```

**Expected**: all existing tests pass (per-mode SVF frequency response, high-resonance
stability, no-allocation invariant) with no change in expected values. The SVF lab
README names `core/primitives/filters/svf-primitive.h` as its graduation target.

## Scenario B — Run the SVF lab harness (US1 / FR-013, SC-005)

```bash
cmake --build --preset test --target acfx_lab_svf_harness   # host-only target
./build/.../acfx_lab_svf_harness                            # exact path per preset
```

**Expected**: the harness prints per-mode frequency-response + stability evidence for the
graduated `SvfPrimitive`. The harness target does NOT appear in the `daisy`/`teensy`
configurations.

## Scenario C — No flat primitives remain (US2 / SC-003)

```bash
# No loose headers directly under core/primitives/ (only category dirs + README):
ls core/primitives
find core/primitives -maxdepth 1 -name '*.h'   # expect: no output
# Taxonomy doc enumerates inhabited + documented-only categories:
cat core/primitives/README.md
```

**Expected**: `core/primitives/` shows only `filters/`, `delays/`, `modulation/`, and
`README.md`; the README lists the prospectus families (`nonlinear/`, `dynamics/`,
`analog/`, `circuit/`, `convolution/`, `wdf/`, `physical/`) as documented-but-not-created.

## Scenario D — Portability gate enforces the invariants (US3 / SC-004)

```bash
# Conformant tree passes:
./scripts/check-portability.sh ; echo "exit=$?"      # expect exit=0

# Deliberate violation 1 — core includes a harness (revert after):
#   add an #include of a labs/*/harness/ path into a core/ file, then:
./scripts/check-portability.sh ; echo "exit=$?"      # expect exit=1, names harness-isolation

# Deliberate violation 2 — a primitive includes an effect (revert after):
#   add an #include "effects/svf/svf-effect.h" into a core/primitives/ header, then:
./scripts/check-portability.sh ; echo "exit=$?"      # expect exit=1, names dependency-direction
```

**Expected**: exit 0 on the clean tree; exit 1 naming the specific violated rule on each
injected violation. (Revert the injected edits — they are validation probes, not commits.)

## Scenario E — Cross-compile excludes the harness (US3 / SC-005)

```bash
cmake --preset daisy   && cmake --build --preset daisy    # requires ARM toolchain
cmake --preset teensy  && cmake --build --preset teensy
```

**Expected**: MCU builds configure with no `labs/*/harness/` source in their target sets;
migrated primitive includes resolve to their new taxonomy paths.

## Done when

- Scenario A–E pass (C and E gated on the relevant toolchains being present).
- `scripts/check-portability.sh` exits 0 on the committed tree.
- The acceptance scenarios in `spec.md` (US1–US3) are all satisfied.
