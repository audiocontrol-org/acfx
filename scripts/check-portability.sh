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
#   C-OS-PRIM. core/primitives/oversampling/ (when present) is gate-ready: platform-free,
#      harness-free — explicit named coverage per FR-022 (T002); passes vacuously when the
#      directory is absent or empty or contains only placeholder files
#   C-OS-LAB. Oversampling lab kernel headers (core/labs/oversampling/*.h, non-harness) are
#      platform-free — explicit named coverage per FR-022 (T002); passes vacuously when the
#      directory is absent, empty, or contains no non-harness kernel headers
#   C-AN-PRIM. core/primitives/analysis/ (when present) is gate-ready: platform-free,
#      harness-free — explicit named coverage per FR-022 (harmonic-analysis T002); passes
#      vacuously when the directory contains only a README (no headers yet)
#   C-AN-DIR. Dependency direction: nothing under core/ includes host/analysis/ or
#      adapters/ — the portable core never reaches the host analysis engine or adapters
#      (Constitution IV; harmonic-analysis plan § Structure Decision, T002)
#   C-EF-PRIM. core/primitives/dynamics/ (when present) is gate-ready: platform-free,
#      effects-free, harness-free — explicit named coverage per FR-021 (envelope-follower
#      T005); passes vacuously when the directory is absent or empty
#   C-EF-LAB. Envelope-follower lab kernel headers (core/labs/envelope-follower/*.h,
#      non-harness) are harness-free and platform-free — explicit named coverage per
#      FR-021 (envelope-follower T005)
#   C-CMP-PRIM. core/primitives/dynamics/gain-computer.h (when present) is gate-ready:
#      platform-free, effects-free, harness-free — explicit named coverage per FR-027
#      (compressor T007); passes vacuously until graduation (T009) lands the file
#   C-CMP-LAB. Compressor lab kernel headers (core/labs/compressor/*.h, non-harness) are
#      harness-free and platform-free — explicit named coverage per FR-027 (compressor T007)
#   C-CMP-SFX. core/effects/compressor/ (when present) is gate-ready: platform-free,
#      harness-free — explicit named coverage per FR-027 (compressor T007); passes
#      vacuously when the directory is absent or empty (pre-implementation); MAY include
#      core/dsp/ and the shipped primitives (that is allowed for effects)
#   C-PDS-PRIM. core/primitives/dynamics/dynamics-modulator.h (when present) is
#      gate-ready: platform-free, effects-free, harness-free — explicit named coverage
#      per FR-024 (program-dependent-saturation T007), for parity with C-CMP-PRIM;
#      passes vacuously until graduation (T009) lands the file
#   C-PDS-LAB. program-dependent-saturation lab kernel headers
#      (core/labs/program-dependent-saturation/*.h, non-harness) are harness-free and
#      platform-free — explicit named coverage per FR-024 (T007)
#   C-PDS-SFX. core/effects/program-dependent-saturation/ (when present) is gate-ready:
#      platform-free, harness-free — explicit named coverage per FR-024 (T007); passes
#      vacuously when the directory is absent or empty (pre-implementation); MAY include
#      the shipped primitives + core/effects/saturation (SaturationCore)
#   C-TD-PRIM. core/primitives/nonlinear/hysteresis.h (when present) is gate-ready:
#      platform-free, effects-free, harness-free — explicit named coverage per FR-017
#      (tape-dynamics T004), for parity with C-CMP-PRIM/C-PDS-PRIM; passes vacuously
#      until the primitive lands (FR-001/FR-016 graduation)
#   C-TD-LAB. tape-dynamics lab kernel headers (core/labs/tape-dynamics/**/*.h,
#      non-harness) are harness-free and platform-free — explicit named coverage per
#      FR-015/FR-017 (tape-dynamics T004); passes vacuously while only the harness and
#      an empty kernel/ placeholder exist (pre-T011)
#   C-TD-SFX. core/effects/tape-dynamics/ (when present) is gate-ready: platform-free,
#      harness-free — explicit named coverage per FR-008/FR-017 (tape-dynamics T004);
#      passes vacuously when the directory is absent or empty

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

note "== C-OS-PRIM. core/primitives/oversampling: gate-ready when present (FR-022/T002) =="
_cosprim_files=$(find core/primitives/oversampling -type f \( -name '*.h' -o -name '*.cpp' \) 2>/dev/null)
if [ -z "$_cosprim_files" ]; then
  note "  OK (vacuous): core/primitives/oversampling/ absent or empty — gate ready"
else
  _cosprim_fail=0
  while IFS= read -r f; do
    if grep -En 'juce|libDaisy|daisy_seed|<Audio\.h>|<Arduino\.h>' "$f" 2>/dev/null; then
      note "  FAIL $f: oversampling primitive includes a platform header"
      _cosprim_fail=1
      fail=1
    fi
    if grep -En '#include.*labs/[^/]*/harness/' "$f" 2>/dev/null; then
      note "  FAIL $f: oversampling primitive includes a harness path"
      _cosprim_fail=1
      fail=1
    fi
  done <<< "$_cosprim_files"
  [ "$_cosprim_fail" -eq 0 ] && note "  OK: core/primitives/oversampling/ is platform-free and harness-free"
fi

note "== C-OS-LAB. Oversampling lab kernel headers: platform-free (FR-022/T002) =="
_coslab_files=$(find core/labs/oversampling -type f -name '*.h' -not -path '*/harness/*' 2>/dev/null)
if [ -z "$_coslab_files" ]; then
  note "  OK (vacuous): core/labs/oversampling/ has no non-harness kernel headers — gate ready"
else
  _coslab_fail=0
  while IFS= read -r f; do
    if grep -En 'juce|libDaisy|daisy_seed|<Audio\.h>|<Arduino\.h>' "$f" 2>/dev/null; then
      note "  FAIL $f: oversampling lab kernel header includes a platform header"
      _coslab_fail=1
      fail=1
    fi
  done <<< "$_coslab_files"
  [ "$_coslab_fail" -eq 0 ] && note "  OK: oversampling lab kernel headers are platform-free"
fi

note "== C-AN-PRIM. core/primitives/analysis: gate-ready when present (FR-022/T002) =="
_canprim_files=$(find core/primitives/analysis -type f \( -name '*.h' -o -name '*.cpp' \) 2>/dev/null)
if [ -z "$_canprim_files" ]; then
  note "  OK (vacuous): core/primitives/analysis/ absent or empty (README only) — gate ready"
else
  _canprim_fail=0
  while IFS= read -r f; do
    if grep -En 'juce|libDaisy|daisy_seed|<Audio\.h>|<Arduino\.h>' "$f" 2>/dev/null; then
      note "  FAIL $f: analysis primitive includes a platform header"
      _canprim_fail=1
      fail=1
    fi
    if grep -En '#include.*labs/[^/]*/harness/' "$f" 2>/dev/null; then
      note "  FAIL $f: analysis primitive includes a harness path"
      _canprim_fail=1
      fail=1
    fi
  done <<< "$_canprim_files"
  [ "$_canprim_fail" -eq 0 ] && note "  OK: core/primitives/analysis/ is platform-free and harness-free"
fi

note "== C-AN-DIR. core/ never reaches host/analysis or adapters (Constitution IV/T002) =="
_candir_fail=0
while IFS= read -r f; do
  if grep -En '#include.*"?(\.\./)*host/analysis/' "$f" 2>/dev/null; then
    note "  FAIL $f: core/ includes host/analysis/ (portable core must not reach the host engine)"
    _candir_fail=1
    fail=1
  fi
  if grep -En '#include.*"?(\.\./)*adapters/' "$f" 2>/dev/null; then
    note "  FAIL $f: core/ includes adapters/ (portable core must not reach adapters)"
    _candir_fail=1
    fail=1
  fi
done < <(find core -type f \( -name '*.h' -o -name '*.cpp' \) 2>/dev/null)
[ "$_candir_fail" -eq 0 ] && note "  OK: core/ does not reach host/analysis/ or adapters/"

note "== C-EF-PRIM. core/primitives/dynamics: gate-ready when present (FR-021/T005) =="
_cefprim_files=$(find core/primitives/dynamics -type f \( -name '*.h' -o -name '*.cpp' \) 2>/dev/null)
if [ -z "$_cefprim_files" ]; then
  note "  OK (vacuous): core/primitives/dynamics/ absent or empty — gate ready"
else
  _cefprim_fail=0
  while IFS= read -r f; do
    if grep -En 'juce|libDaisy|daisy_seed|<Audio\.h>|<Arduino\.h>' "$f" 2>/dev/null; then
      note "  FAIL $f: dynamics primitive includes a platform header"
      _cefprim_fail=1
      fail=1
    fi
    if grep -En '#include.*effects/' "$f" 2>/dev/null; then
      note "  FAIL $f: dynamics primitive includes an effects/ path"
      _cefprim_fail=1
      fail=1
    fi
    if grep -En '#include.*labs/[^/]*/harness/' "$f" 2>/dev/null; then
      note "  FAIL $f: dynamics primitive includes a harness path"
      _cefprim_fail=1
      fail=1
    fi
  done <<< "$_cefprim_files"
  [ "$_cefprim_fail" -eq 0 ] && note "  OK: core/primitives/dynamics/ is platform-free, effects-free, and harness-free"
fi

note "== C-EF-LAB. Envelope-follower lab kernel headers: harness-free + platform-free (FR-021/T005) =="
_ceflab_files=$(find core/labs/envelope-follower -type f -name '*.h' -not -path '*/harness/*' 2>/dev/null)
if [ -z "$_ceflab_files" ]; then
  note "  OK (vacuous): core/labs/envelope-follower/ has no non-harness kernel headers — gate ready"
else
  _ceflab_fail=0
  while IFS= read -r f; do
    if grep -En '#include.*labs/[^/]*/harness/' "$f" 2>/dev/null; then
      note "  FAIL $f: envelope-follower kernel header includes a harness path"
      _ceflab_fail=1
      fail=1
    fi
    if grep -En 'juce|libDaisy|daisy_seed|<Audio\.h>|<Arduino\.h>' "$f" 2>/dev/null; then
      note "  FAIL $f: envelope-follower kernel header includes a platform header"
      _ceflab_fail=1
      fail=1
    fi
  done <<< "$_ceflab_files"
  [ "$_ceflab_fail" -eq 0 ] && note "  OK: envelope-follower lab kernel headers are harness-free and platform-free"
fi

note "== C-CMP-PRIM. core/primitives/dynamics/gain-computer.h: gate-ready when present (FR-027) =="
_ccmpprim_files=$(find core/primitives/dynamics -type f -name 'gain-computer.h' 2>/dev/null)
if [ -z "$_ccmpprim_files" ]; then
  note "  OK (vacuous): core/primitives/dynamics/gain-computer.h absent — gate ready for graduation (T009)"
else
  _ccmpprim_fail=0
  while IFS= read -r f; do
    if grep -En 'juce|libDaisy|daisy_seed|<Audio\.h>|<Arduino\.h>' "$f" 2>/dev/null; then
      note "  FAIL $f: gain-computer primitive includes a platform header"
      _ccmpprim_fail=1
      fail=1
    fi
    if grep -En '#include.*effects/' "$f" 2>/dev/null; then
      note "  FAIL $f: gain-computer primitive includes an effects/ path"
      _ccmpprim_fail=1
      fail=1
    fi
    if grep -En '#include.*labs/[^/]*/harness/' "$f" 2>/dev/null; then
      note "  FAIL $f: gain-computer primitive includes a harness path"
      _ccmpprim_fail=1
      fail=1
    fi
  done <<< "$_ccmpprim_files"
  [ "$_ccmpprim_fail" -eq 0 ] && note "  OK: core/primitives/dynamics/gain-computer.h is platform-free, effects-free, and harness-free"
fi

note "== C-CMP-LAB. Compressor lab kernel headers: harness-free + platform-free (FR-027) =="
_ccmplab_files=$(find core/labs/compressor -type f -name '*.h' -not -path '*/harness/*' 2>/dev/null)
if [ -z "$_ccmplab_files" ]; then
  note "  OK (vacuous): core/labs/compressor/ has no non-harness kernel headers — gate ready"
else
  _ccmplab_fail=0
  while IFS= read -r f; do
    if grep -En '#include.*labs/[^/]*/harness/' "$f" 2>/dev/null; then
      note "  FAIL $f: compressor kernel header includes a harness path"
      _ccmplab_fail=1
      fail=1
    fi
    if grep -En 'juce|libDaisy|daisy_seed|<Audio\.h>|<Arduino\.h>' "$f" 2>/dev/null; then
      note "  FAIL $f: compressor kernel header includes a platform header"
      _ccmplab_fail=1
      fail=1
    fi
  done <<< "$_ccmplab_files"
  [ "$_ccmplab_fail" -eq 0 ] && note "  OK: compressor lab kernel headers are harness-free and platform-free"
fi

note "== C-CMP-SFX. core/effects/compressor: gate-ready when present (FR-027) =="
_ccmpsfx_files=$(find core/effects/compressor -type f \( -name '*.h' -o -name '*.cpp' \) 2>/dev/null)
if [ -z "$_ccmpsfx_files" ]; then
  note "  OK (vacuous): core/effects/compressor/ absent or empty — gate ready"
else
  _ccmpsfx_fail=0
  while IFS= read -r f; do
    if grep -En 'juce|libDaisy|daisy_seed|<Audio\.h>|<Arduino\.h>' "$f" 2>/dev/null; then
      note "  FAIL $f: compressor effect includes a platform header"
      _ccmpsfx_fail=1
      fail=1
    fi
    if grep -En '#include.*labs/[^/]*/harness/' "$f" 2>/dev/null; then
      note "  FAIL $f: compressor effect includes a harness path"
      _ccmpsfx_fail=1
      fail=1
    fi
  done <<< "$_ccmpsfx_files"
  [ "$_ccmpsfx_fail" -eq 0 ] && note "  OK: core/effects/compressor/ is platform-free and harness-free"
fi

note "== C-PDS-PRIM. core/primitives/dynamics/dynamics-modulator.h: gate-ready when present (FR-024) =="
_cpdsprim_files=$(find core/primitives/dynamics -type f -name 'dynamics-modulator.h' 2>/dev/null)
if [ -z "$_cpdsprim_files" ]; then
  note "  OK (vacuous): core/primitives/dynamics/dynamics-modulator.h absent — gate ready for graduation (T009)"
else
  _cpdsprim_fail=0
  while IFS= read -r f; do
    if grep -En 'juce|libDaisy|daisy_seed|<Audio\.h>|<Arduino\.h>' "$f" 2>/dev/null; then
      note "  FAIL $f: dynamics-modulator primitive includes a platform header"
      _cpdsprim_fail=1
      fail=1
    fi
    if grep -En '#include.*effects/' "$f" 2>/dev/null; then
      note "  FAIL $f: dynamics-modulator primitive includes an effects/ path"
      _cpdsprim_fail=1
      fail=1
    fi
    if grep -En '#include.*labs/[^/]*/harness/' "$f" 2>/dev/null; then
      note "  FAIL $f: dynamics-modulator primitive includes a harness path"
      _cpdsprim_fail=1
      fail=1
    fi
  done <<< "$_cpdsprim_files"
  [ "$_cpdsprim_fail" -eq 0 ] && note "  OK: core/primitives/dynamics/dynamics-modulator.h is platform-free, effects-free, and harness-free"
fi

note "== C-PDS-LAB. program-dependent-saturation lab kernel headers: harness-free + platform-free (FR-024) =="
_cpdslab_files=$(find core/labs/program-dependent-saturation -type f -name '*.h' -not -path '*/harness/*' 2>/dev/null)
if [ -z "$_cpdslab_files" ]; then
  note "  OK (vacuous): core/labs/program-dependent-saturation/ has no non-harness kernel headers — gate ready"
else
  _cpdslab_fail=0
  while IFS= read -r f; do
    if grep -En '#include.*labs/[^/]*/harness/' "$f" 2>/dev/null; then
      note "  FAIL $f: program-dependent-saturation kernel header includes a harness path"
      _cpdslab_fail=1
      fail=1
    fi
    if grep -En 'juce|libDaisy|daisy_seed|<Audio\.h>|<Arduino\.h>' "$f" 2>/dev/null; then
      note "  FAIL $f: program-dependent-saturation kernel header includes a platform header"
      _cpdslab_fail=1
      fail=1
    fi
  done <<< "$_cpdslab_files"
  [ "$_cpdslab_fail" -eq 0 ] && note "  OK: program-dependent-saturation lab kernel headers are harness-free and platform-free"
fi

note "== C-PDS-SFX. core/effects/program-dependent-saturation: gate-ready when present (FR-024) =="
_cpdssfx_files=$(find core/effects/program-dependent-saturation -type f \( -name '*.h' -o -name '*.cpp' \) 2>/dev/null)
if [ -z "$_cpdssfx_files" ]; then
  note "  OK (vacuous): core/effects/program-dependent-saturation/ absent or empty — gate ready"
else
  _cpdssfx_fail=0
  while IFS= read -r f; do
    if grep -En 'juce|libDaisy|daisy_seed|<Audio\.h>|<Arduino\.h>' "$f" 2>/dev/null; then
      note "  FAIL $f: program-dependent-saturation effect includes a platform header"
      _cpdssfx_fail=1
      fail=1
    fi
    if grep -En '#include.*labs/[^/]*/harness/' "$f" 2>/dev/null; then
      note "  FAIL $f: program-dependent-saturation effect includes a harness path"
      _cpdssfx_fail=1
      fail=1
    fi
  done <<< "$_cpdssfx_files"
  [ "$_cpdssfx_fail" -eq 0 ] && note "  OK: core/effects/program-dependent-saturation/ is platform-free and harness-free"
fi

note "== C-TD-PRIM. core/primitives/nonlinear/hysteresis.h: gate-ready when present (FR-017) =="
_ctdprim_files=$(find core/primitives/nonlinear -type f -name 'hysteresis.h' 2>/dev/null)
if [ -z "$_ctdprim_files" ]; then
  note "  OK (vacuous): core/primitives/nonlinear/hysteresis.h absent — gate ready for graduation"
else
  _ctdprim_fail=0
  while IFS= read -r f; do
    if grep -En 'juce|libDaisy|daisy_seed|<Audio\.h>|<Arduino\.h>' "$f" 2>/dev/null; then
      note "  FAIL $f: hysteresis primitive includes a platform header"
      _ctdprim_fail=1
      fail=1
    fi
    if grep -En '#include.*effects/' "$f" 2>/dev/null; then
      note "  FAIL $f: hysteresis primitive includes an effects/ path"
      _ctdprim_fail=1
      fail=1
    fi
    if grep -En '#include.*labs/[^/]*/harness/' "$f" 2>/dev/null; then
      note "  FAIL $f: hysteresis primitive includes a harness path"
      _ctdprim_fail=1
      fail=1
    fi
  done <<< "$_ctdprim_files"
  [ "$_ctdprim_fail" -eq 0 ] && note "  OK: core/primitives/nonlinear/hysteresis.h is platform-free, effects-free, and harness-free"
fi

note "== C-TD-LAB. tape-dynamics lab kernel headers: harness-free + platform-free (FR-015/017) =="
_ctdlab_files=$(find core/labs/tape-dynamics -type f -name '*.h' -not -path '*/harness/*' 2>/dev/null)
if [ -z "$_ctdlab_files" ]; then
  note "  OK (vacuous): core/labs/tape-dynamics/ has no non-harness kernel headers — gate ready"
else
  _ctdlab_fail=0
  while IFS= read -r f; do
    if grep -En '#include.*labs/[^/]*/harness/' "$f" 2>/dev/null; then
      note "  FAIL $f: tape-dynamics kernel header includes a harness path"
      _ctdlab_fail=1
      fail=1
    fi
    if grep -En 'juce|libDaisy|daisy_seed|<Audio\.h>|<Arduino\.h>' "$f" 2>/dev/null; then
      note "  FAIL $f: tape-dynamics kernel header includes a platform header"
      _ctdlab_fail=1
      fail=1
    fi
  done <<< "$_ctdlab_files"
  [ "$_ctdlab_fail" -eq 0 ] && note "  OK: tape-dynamics lab kernel headers are harness-free and platform-free"
fi

note "== C-TD-SFX. core/effects/tape-dynamics: gate-ready when present (FR-008/017) =="
_ctdsfx_files=$(find core/effects/tape-dynamics -type f \( -name '*.h' -o -name '*.cpp' \) 2>/dev/null)
if [ -z "$_ctdsfx_files" ]; then
  note "  OK (vacuous): core/effects/tape-dynamics/ absent or empty — gate ready"
else
  _ctdsfx_fail=0
  while IFS= read -r f; do
    if grep -En 'juce|libDaisy|daisy_seed|<Audio\.h>|<Arduino\.h>' "$f" 2>/dev/null; then
      note "  FAIL $f: tape-dynamics effect includes a platform header"
      _ctdsfx_fail=1
      fail=1
    fi
    if grep -En '#include.*labs/[^/]*/harness/' "$f" 2>/dev/null; then
      note "  FAIL $f: tape-dynamics effect includes a harness path"
      _ctdsfx_fail=1
      fail=1
    fi
  done <<< "$_ctdsfx_files"
  [ "$_ctdsfx_fail" -eq 0 ] && note "  OK: core/effects/tape-dynamics/ is platform-free and harness-free"
fi

if [ "$fail" -eq 0 ]; then
  note ""
  note "All portability gates passed."
else
  note ""
  note "Portability gate FAILED."
fi
exit "$fail"
