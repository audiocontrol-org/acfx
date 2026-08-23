---
id: TASK-31
title: fpu-enable-lost-with-systeminit-exclusion
status: To Do
assignee: []
created_date: '2026-08-23 22:37'
labels:
  - agent-found
  - 'type:bug'
dependencies: []
references:
  - adapters/nucleo/startup/startup.cpp
ordinal: 31000
---

## Description

<!-- SECTION:DESCRIPTION:BEGIN -->
Excluding ST's system_stm32f4xx.c (FR-013 / D14) silently dropped the Cortex-M4F FPU enable, and nothing replaced it. The board hard-faulted on the first floating-point instruction and never enumerated.

Observed on hardware 2026-08-23 with the full USB stack flashed and every other bring-up value correct:
  SCB_CPACR = 0x00000000   CP10/CP11 access = 0 (FPU DISABLED)
  SCB_CFSR  = 0x00080000   UFSR bit 3 = NOCP, "no coprocessor" usage fault
  SCB_HFSR  = 0x40000000   FORCED, escalated to HardFault
  NVIC_ISER[2] bit 3 = 0   OTG_FS interrupt never enabled
  OTG_FS GINTSTS = 0x4480bc38 with USBRST and ENUMDNE set and UNCLEARED

The toolchain builds this target with -mfpu=fpv4-sp-d16 -mfloat-abi=hard (cmake/toolchains/nucleo-f446.cmake:56), so the compiler emits VFP instructions freely. On reset the Cortex-M4F FPU is disabled; executing a VFP instruction then raises a NOCP UsageFault which, with no UsageFault handler installed, escalates straight to HardFault. On ST parts the CPACR enable normally lives in SystemInit() inside system_stm32f4xx.c. D14's decision to exclude that file was correct on its merits, but one line of it still had to be replaced and nothing did.

Why this was expensive to find rather than obvious: every register that bring-up code normally checks looked perfectly healthy. RCC_PLLCFGR read back the exact spike value, HSERDY/PLLRDY/SWS were all correct, the OTG_FS peripheral clock was on, GUSBCFG.FDMOD was set, DCTL.SDIS was clear, and the B-session valid override was in place. The host even completed bus reset and speed detection. The only signals that anything was wrong were an NVIC bit that was never set and interrupt flags that were never cleared -- both absences rather than errors. A dead board with a correct-looking clock tree is the exact shape this class of bug takes.

Fixed in adapters/nucleo/startup/startup.cpp: EnableFpu() sets CPACR CP10/CP11 to full access as step 0 of Reset_Handler, before __libc_init_array, because a static constructor in a DSP codebase is free to touch a float. Followed by DSB and ISB as the architecture requires. The register is addressed directly rather than via CMSIS, since the reset path must not depend on device headers and the address is identical on every Cortex-M4F.

Class of gap worth generalising, and the reason this is filed rather than just fixed: the same reasoning applies to any other target that declines a vendor init file. Excluding vendor scaffolding is a good instinct in a thin-adapter codebase, but it needs an explicit inventory of what that file was doing, item by item, rather than an assumption that it was all scaffolding. For the F4, SystemInit() does at minimum the FPU enable and the vector-table relocation. Related to TASK-24, which recorded that no review caught the missing C-runtime startup -- same shape: reviews reasoned over the artifacts present and never over whether the described set was sufficient to boot.

Consider also: no UsageFault/HardFault handler is installed, so a fault is indistinguishable from a hang. A minimal fault handler that parks in a recognisable way (or drives the LD2 fault pattern from T050) would have made this visible in seconds instead of requiring SWD register archaeology.
<!-- SECTION:DESCRIPTION:END -->
