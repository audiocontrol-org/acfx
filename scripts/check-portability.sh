#!/usr/bin/env bash
#
# acfx explicit quality gates (T037, T038) — visible steps, NOT a git hook
# (Constitution II). Run locally and in CI. Exits non-zero on any violation.
#
#   1. File-size budget (~300-500 lines; hard-fail above 500) — Constitution VII
#   2. No platform headers in core/ — Constitution IV
#   3. No JUCE / ProcessorNode in the MCU (daisy/teensy) dependency surface — SC-007
#   4. One-source-many-targets: no per-target #ifdef forks of the effect, and every
#      adapter links the same acfx_core — SC-001 / SC-005 (Scenario E)
#   C-1. Lab-harness isolation: no portable source includes a harness path — FR-005/SC-005
#   C-2. Dependency direction: core/primitives never includes core/effects — FR-015
#   C-3. MCU-harness backstop: no harness path in daisy/teensy build inputs — FR-005/SC-005

set -u
cd "$(dirname "$0")/.." || exit 2

fail=0
note() { printf '%s\n' "$*"; }

note "== 1. File-size budget (<= 500 lines) =="
while IFS= read -r f; do
  lines=$(wc -l < "$f" | tr -d ' ')
  if [ "$lines" -gt 500 ]; then
    note "  FAIL $f: $lines lines (> 500)"
    fail=1
  fi
done < <(find core host adapters tests -type f \( -name '*.h' -o -name '*.cpp' \) 2>/dev/null)
[ "$fail" -eq 0 ] && note "  OK: all source files within budget"

note "== 2. No platform headers in core/ (Constitution IV; C-4: harness paths exempt) =="
_c4_fail=0
while IFS= read -r f; do
  if grep -EHn 'juce|libDaisy|daisy_seed|<Audio\.h>|<Arduino\.h>' "$f" 2>/dev/null; then
    _c4_fail=1
  fi
done < <(find core -type f \( -name '*.h' -o -name '*.cpp' \) \
           -not -path 'core/labs/*/harness/*' 2>/dev/null)
if [ "$_c4_fail" -eq 1 ]; then
  note "  FAIL: platform header in core/ (non-harness)"
  fail=1
else
  note "  OK: core/ (non-harness) is platform-independent"
fi

note "== 3. No JUCE / ProcessorNode in MCU adapters (SC-007) =="
if grep -rEn 'juce|processor-node' adapters/daisy adapters/teensy ; then
  note "  FAIL: MCU adapter references JUCE or the desktop ProcessorNode"
  fail=1
else
  note "  OK: daisy + teensy reference neither JUCE nor ProcessorNode"
fi

note "== 4. One-source-many-targets (Scenario E, SC-001/SC-005) =="
if grep -rEn '#if(def)?.*(JUCE|DAISY|TEENSY|__arm__|DESKTOP)' core/effects/ ; then
  note "  FAIL: per-target #ifdef fork inside the effect source"
  fail=1
else
  note "  OK: no per-target #ifdef forks in core/effects/"
fi

# Assert the factory file links acfx_core so the factory path below is real.
if ! grep -q 'acfx_core' cmake/acfx-effect-targets.cmake 2>/dev/null; then
  note "  FAIL: cmake/acfx-effect-targets.cmake does not link acfx_core"
  fail=1
fi

# An adapter passes if its CMakeLists references acfx_core directly OR invokes an
# acfx_add_effect_ factory function (which links acfx_core via the factory).
for adapter in workbench plugin daisy teensy; do
  cml="adapters/$adapter/CMakeLists.txt"
  if grep -q 'acfx_core' "$cml" 2>/dev/null || \
     grep -qE 'acfx_add_effect_' "$cml" 2>/dev/null; then
    :
  else
    note "  FAIL: adapters/$adapter does not link acfx_core (neither directly nor via factory)"
    fail=1
  fi
done
[ "$fail" -eq 0 ] && note "  OK: every adapter links the same acfx_core"

note "== C-1. Lab-harness isolation (FR-005/SC-005) =="
_c1_fail=0
while IFS= read -r f; do
  if grep -En '#include.*labs/[^/]*/harness/' "$f" 2>/dev/null; then
    note "  FAIL $f: portable source includes a harness path"
    _c1_fail=1
    fail=1
  fi
done < <(
  find core/dsp core/primitives core/effects \
       -type f \( -name '*.h' -o -name '*.cpp' \) 2>/dev/null
  find core/labs -type f \( -name '*.h' -o -name '*.cpp' \) \
       -not -path '*/harness/*' 2>/dev/null
)
[ "$_c1_fail" -eq 0 ] && note "  OK: no portable source includes a harness path"

note "== C-2. Primitives never include effects (FR-015) =="
if grep -rEn '#include.*effects/' core/primitives/ 2>/dev/null; then
  note "  FAIL: core/primitives/ includes an effects/ path"
  fail=1
else
  note "  OK: core/primitives/ has no includes of effects/"
fi

note "== C-3. No harness in MCU build inputs (FR-005/SC-005) =="
if grep -rEn 'labs/[^/]*/harness/' adapters/daisy adapters/teensy 2>/dev/null; then
  note "  FAIL: MCU adapter references a labs harness path"
  fail=1
else
  note "  OK: daisy + teensy reference no labs harness paths"
fi

if [ "$fail" -eq 0 ]; then
  note ""
  note "All portability gates passed."
else
  note ""
  note "Portability gate FAILED."
fi
exit "$fail"
