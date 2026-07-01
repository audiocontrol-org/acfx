#!/usr/bin/env bash
#
# acfx explicit quality gates (T037, T038, T023) — visible steps, NOT a git hook
# (Constitution II). Run locally and in CI. Exits non-zero on any violation.
#
#   1. File-size budget (~300-500 lines; hard-fail above 500) — Constitution VII
#      Coverage: find core host adapters tests — sweeps core/labs/waveshaping/**,
#      core/primitives/nonlinear/**, core/labs/saturation/**, and core/effects/saturation/**
#      (once populated) generically. FR-022.
#   2. No platform headers in core/ — Constitution IV
#      Coverage: find core -not -path core/labs/*/harness/* — sweeps core/labs/waveshaping/*.h
#      (kernel headers), core/primitives/nonlinear/**, core/labs/saturation/**, and
#      core/effects/saturation/** generically. FR-022.
#   3. No JUCE / ProcessorNode in the MCU (daisy/teensy) dependency surface — SC-007
#   4. One-source-many-targets: no per-target #ifdef forks of the effect, and every
#      adapter links the same acfx_core — SC-001 / SC-005 (Scenario E)
#   C-1. Lab-harness isolation: no portable source includes a harness path — FR-005/SC-005
#      Coverage: find core/labs (non-harness) sweeps core/labs/waveshaping/*.h and
#      core/labs/saturation/*.h explicitly; find core/primitives and core/effects covers
#      core/primitives/nonlinear/** and core/effects/saturation/** once populated. FR-022.
#   C-2. Dependency direction: core/primitives never includes core/effects — FR-015
#      Coverage: grep core/primitives/ covers core/primitives/nonlinear/** once populated. FR-022.
#   C-3. MCU-harness backstop: no harness path in daisy/teensy build inputs — FR-005/SC-005
#   C-WS. Waveshaping kernel headers (core/labs/waveshaping/*.h) are harness-free and
#      platform-free — explicit named coverage per FR-022 (T023)
#   C-NL. core/primitives/nonlinear/ (when present) is gate-ready: platform-free,
#      effects-free, harness-free — explicit named coverage per FR-022 (T023); passes
#      vacuously when the directory is absent or empty (T024 creates it)
#   C-SAT. Saturation lab kernel headers (core/labs/saturation/*.h, non-harness) are
#      harness-free and platform-free — explicit named coverage per FR-021/022 (T020)
#   C-SFX. core/effects/saturation/ (the graduation target, T022) is gate-ready:
#      platform-free, harness-free — explicit named coverage per FR-021/022 (T020); passes
#      vacuously when the directory is absent or empty (currently pre-graduation)

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

note "== C-WS. Waveshaping kernel headers: harness-free + platform-free (FR-022/T023) =="
_cws_fail=0
while IFS= read -r f; do
  if grep -En '#include.*labs/[^/]*/harness/' "$f" 2>/dev/null; then
    note "  FAIL $f: waveshaping kernel header includes a harness path"
    _cws_fail=1
    fail=1
  fi
  if grep -En 'juce|libDaisy|daisy_seed|<Audio\.h>|<Arduino\.h>' "$f" 2>/dev/null; then
    note "  FAIL $f: waveshaping kernel header includes a platform header"
    _cws_fail=1
    fail=1
  fi
done < <(find core/labs/waveshaping -type f -name '*.h' \
          -not -path '*/harness/*' 2>/dev/null)
[ "$_cws_fail" -eq 0 ] && note "  OK: waveshaping kernel headers are harness-free and platform-free"

note "== C-NL. core/primitives/nonlinear: gate-ready when present (FR-022/T023) =="
_cnl_files=$(find core/primitives/nonlinear -type f \( -name '*.h' -o -name '*.cpp' \) 2>/dev/null)
if [ -z "$_cnl_files" ]; then
  note "  OK (vacuous): core/primitives/nonlinear/ absent or empty — gate ready for T024"
else
  _cnl_fail=0
  while IFS= read -r f; do
    if grep -En 'juce|libDaisy|daisy_seed|<Audio\.h>|<Arduino\.h>' "$f" 2>/dev/null; then
      note "  FAIL $f: nonlinear primitive includes a platform header"
      _cnl_fail=1
      fail=1
    fi
    if grep -En '#include.*effects/' "$f" 2>/dev/null; then
      note "  FAIL $f: nonlinear primitive includes an effects/ path"
      _cnl_fail=1
      fail=1
    fi
    if grep -En '#include.*labs/[^/]*/harness/' "$f" 2>/dev/null; then
      note "  FAIL $f: nonlinear primitive includes a harness path"
      _cnl_fail=1
      fail=1
    fi
  done <<< "$_cnl_files"
  [ "$_cnl_fail" -eq 0 ] && note "  OK: core/primitives/nonlinear/ is platform-free, effects-free, and harness-free"
fi

note "== C-SAT. Saturation lab kernel headers: harness-free + platform-free (FR-021/022) =="
_csat_fail=0
while IFS= read -r f; do
  if grep -En '#include.*labs/[^/]*/harness/' "$f" 2>/dev/null; then
    note "  FAIL $f: saturation lab kernel header includes a harness path"
    _csat_fail=1
    fail=1
  fi
  if grep -En 'juce|libDaisy|daisy_seed|<Audio\.h>|<Arduino\.h>' "$f" 2>/dev/null; then
    note "  FAIL $f: saturation lab kernel header includes a platform header"
    _csat_fail=1
    fail=1
  fi
done < <(find core/labs/saturation -type f -name '*.h' \
          -not -path '*/harness/*' 2>/dev/null)
[ "$_csat_fail" -eq 0 ] && note "  OK: saturation lab kernel headers are harness-free and platform-free"

note "== C-SFX. core/effects/saturation: gate-ready when present (FR-021/022) =="
_csfx_files=$(find core/effects/saturation -type f \( -name '*.h' -o -name '*.cpp' \) 2>/dev/null)
if [ -z "$_csfx_files" ]; then
  note "  OK (vacuous): core/effects/saturation/ absent or empty — gate ready for T022"
else
  _csfx_fail=0
  while IFS= read -r f; do
    if grep -En 'juce|libDaisy|daisy_seed|<Audio\.h>|<Arduino\.h>' "$f" 2>/dev/null; then
      note "  FAIL $f: saturation effect includes a platform header"
      _csfx_fail=1
      fail=1
    fi
    if grep -En '#include.*labs/[^/]*/harness/' "$f" 2>/dev/null; then
      note "  FAIL $f: saturation effect includes a harness path"
      _csfx_fail=1
      fail=1
    fi
  done <<< "$_csfx_files"
  [ "$_csfx_fail" -eq 0 ] && note "  OK: core/effects/saturation/ is platform-free and harness-free"
fi

if [ "$fail" -eq 0 ]; then
  note ""
  note "All portability gates passed."
else
  note ""
  note "Portability gate FAILED."
fi
exit "$fail"
