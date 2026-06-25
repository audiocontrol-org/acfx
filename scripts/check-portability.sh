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

note "== 2. No platform headers in core/ (Constitution IV) =="
if grep -rEn 'juce|libDaisy|daisy_seed|<Audio\.h>|<Arduino\.h>' core/ ; then
  note "  FAIL: platform header in core/"
  fail=1
else
  note "  OK: core/ is platform-independent"
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
for adapter in workbench plugin daisy teensy; do
  if ! grep -rq 'acfx_core' "adapters/$adapter/CMakeLists.txt" 2>/dev/null; then
    note "  FAIL: adapters/$adapter does not link acfx_core"
    fail=1
  fi
done
[ "$fail" -eq 0 ] && note "  OK: every adapter links the same acfx_core"

if [ "$fail" -eq 0 ]; then
  note ""
  note "All portability gates passed."
else
  note ""
  note "Portability gate FAILED."
fi
exit "$fail"
