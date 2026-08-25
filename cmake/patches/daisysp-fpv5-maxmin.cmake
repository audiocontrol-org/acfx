# Idempotent patch: gate DaisySP's hand-written vmaxnm/vminnm inline assembly on
# an FPv5 opt-in macro instead of bare __arm__.
#
# WHY. DaisySP's Source/Utility/dsp.h defines fmax()/fmin() with inline assembly
# emitting the FP-ARMv8 (FPv5) instructions VMAXNM.F32 / VMINNM.F32, guarded on
# `#ifdef __arm__` alone. Those instructions exist on the Cortex-M7 (FPv5) the
# Daisy platform targets, but NOT on the Cortex-M4F (FPv4) used by the NUCLEO-
# F446RE adapter. On an M4 the instruction raises a NOCP UsageFault that escalates
# to HardFault the first time any DaisySP float min/max runs — which for the
# Nucleo firmware is AppEffect::prepare(), before USB can enumerate. GCC exposes
# no predefined macro that distinguishes FPv4 from FPv5, so the guard cannot be
# derived automatically; the acfx M7 toolchains (daisy/teensy) opt in explicitly
# by defining DSY_FPV5_MAXMIN, and every other target (M4 Nucleo + host) falls to
# DaisySP's own portable `(a > b) ? a : b` path.
#
# Invoked as a CPM PATCH_COMMAND; the working directory is the DaisySP source
# root. Idempotent: after the first run the bare `#ifdef __arm__` guards on the
# min/max asm no longer exist, so a re-run is a no-op (safe for a warm CPM cache
# and offline reconfigures).

# Resolve dsp.h. As a CPM PATCH_COMMAND this runs with the working directory set
# to the populated DaisySP source root (so CMAKE_SOURCE_DIR / the CWD-relative path
# both point there); DAISYSP_DSP_H is an explicit override for manual invocation.
set(_dsp_h "")
foreach(_cand "${DAISYSP_DSP_H}" "Source/Utility/dsp.h" "${CMAKE_SOURCE_DIR}/Source/Utility/dsp.h")
  if(_cand AND EXISTS "${_cand}")
    set(_dsp_h "${_cand}")
    break()
  endif()
endforeach()
if(_dsp_h STREQUAL "")
  message(FATAL_ERROR "daisysp-fpv5-maxmin patch: cannot find Source/Utility/dsp.h (cwd: ${CMAKE_SOURCE_DIR})")
endif()

file(READ "${_dsp_h}" _contents)

if(_contents MATCHES "DSY_FPV5_MAXMIN")
  # Already patched (warm cache / re-run) — nothing to do.
  return()
endif()

string(REPLACE
  "#ifdef __arm__"
  "#if defined(__arm__) && defined(DSY_FPV5_MAXMIN)"
  _patched "${_contents}")

if(_patched STREQUAL _contents)
  message(FATAL_ERROR
    "daisysp-fpv5-maxmin patch: expected `#ifdef __arm__` guard(s) in ${_dsp_h} but found none — "
    "the pinned DaisySP source has changed shape; re-verify the fmax/fmin definitions before re-pinning.")
endif()

file(WRITE "${_dsp_h}" "${_patched}")
message(STATUS "daisysp-fpv5-maxmin: patched ${_dsp_h} (min/max asm now gated on DSY_FPV5_MAXMIN)")
