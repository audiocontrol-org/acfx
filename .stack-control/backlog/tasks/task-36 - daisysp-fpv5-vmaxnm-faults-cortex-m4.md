---
id: TASK-36
title: daisysp-fpv5-vmaxnm-faults-cortex-m4
status: To Do
assignee: []
created_date: '2026-08-24 04:01'
labels:
  - agent-found
  - 'type:bug'
dependencies: []
ordinal: 36000
---

## Description

<!-- SECTION:DESCRIPTION:BEGIN -->
DaisySP portability bug (upstreamable): Source/Utility/dsp.h defines fmax()/fmin() with inline asm emitting FPv5-only VMAXNM.F32/VMINNM.F32, gated on bare #ifdef __arm__ with no FPU-version check. Valid on the Cortex-M7 (FPv5) Daisy platform; on the Cortex-M4F (FPv4) NUCLEO-F446RE it raises a NOCP UsageFault -> HardFault the first time any DaisySP float min/max runs (fclamp in Svf::SetFreq, called from AppEffect::prepare()). The firmware links and flashes cleanly and then sits dead in the HardFault handler, never enumerating -- a link is not a boot check (relates TASK-25). GCC exposes NO predefined macro distinguishing FPv4 from FPv5 (M4 and M7 -dM macro sets are identical for ARM_FP/FPV/ARCH/FEATURE), so the guard cannot be derived automatically and no compiler flag (-fno-builtin, -march=armv7e-m+fp, -O0..-O3) removes it since it is hand-written asm. Fixed in acfx by an idempotent CPM PATCH_COMMAND (cmake/patches/daisysp-fpv5-maxmin.cmake) that re-gates the asm on DSY_FPV5_MAXMIN, defined only by the M7 daisy/teensy toolchains; M4 + host use DaisySP's portable (a>b)?a:b path. Committed a50a7ab. TODO: submit the guard fix upstream to electro-smith/DaisySP (change `#ifdef __arm__` to a positive FPv5 opt-in, or provide a portable fallback that does not assume FPv5) so future re-pins do not reintroduce it -- the pin is currently 599511b. Also: this is the second time a silent HardFault-as-hang cost significant SWD archaeology to diagnose (first was the FPU-enable fault last session); it strongly motivates the parked 'point the fault vectors at the T050 LD2 blink pattern' work so an abort presents as a signal, not a hang.
<!-- SECTION:DESCRIPTION:END -->
