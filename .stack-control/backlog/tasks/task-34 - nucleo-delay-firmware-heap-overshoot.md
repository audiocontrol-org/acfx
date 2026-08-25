---
id: TASK-34
title: nucleo-delay-firmware-heap-overshoot
status: Done
assignee: []
created_date: '2026-08-24 02:12'
updated_date: '2026-08-25 05:32'
labels:
  - agent-found
  - 'type:bug'
dependencies: []
ordinal: 34000
---

## Description

<!-- SECTION:DESCRIPTION:BEGIN -->
acfx_nucleo_delay firmware cannot boot: ModulatedDelayEffect::prepare() requests 768 KB of heap on a 128 KB part. Arithmetic: modulated-delay-effect.h:152 computes capacity = sampleRate * 2.0f + 2 = 96002 floats for a 2.0-second delay line, then line 155 does buffers_[idx].assign(capacity, 0.0f) per channel. That is 384008 bytes per channel and 768016 bytes for the stereo ProcessContext the Nucleo adapter prepares with. The STM32F446RE has 131072 bytes of SRAM total, of which the delay image already uses 14952 for .bss, and nucleo-f446.ld reserves _Min_Heap_Size = 0x200 (512 bytes). The request overshoots total RAM by 636944 bytes. malloc returns null, std::vector::assign takes the bad-alloc path, and the embedded toolchains build -fno-exceptions (cmake/toolchains/nucleo-f446.cmake:63), so the image aborts inside PrepareEffect() before the tud_task() service loop is ever entered. The board would present as a hang with no diagnostic, since the fault vectors still do not point at T050 blink pattern.

This is pre-existing incompatibility in shipped core effect code, not a defect introduced by T034 - but T034 is what made it REACHABLE, because nothing on this target called prepare() before. It is exactly the TASK-25 shape: acfx_nucleo_delay.elf links clean at text 27980 / data 132 / bss 14952 and is a dead image.

acfx_nucleo (SvfEffect) is unaffected - SvfEffect holds per-channel DaisySP Svf state with no vectors and no allocation.

Options for the operator, none taken: (a) give ModulatedDelayEffect a compile-time-bounded buffer sized from a max-delay constant that fits the part, which is a design change to shipped core code affecting desktop and Daisy too; (b) drop the acfx_nucleo_delay target from the Nucleo build until the delay effect has a bounded-memory variant; (c) keep the target and record it as known-nonviable on this part, with SVF as the working image. Also worth doing regardless: point the HardFault vectors at the T050 LD2 blink pattern so an abort like this presents as a signal rather than a hang - the journal already records that lesson from the FPU incident.

HARDWARE-CONFIRMED 2026-08-24 (Phase-12 / US9 live run, T064): flashed acfx_nucleo_delay.bin to the F446 (st-flash write+verify OK) - the board stays alive over SWD (st-flash read of 0x08000000 succeeds) but the acfx USB composite device does NOT enumerate: /dev/cu.usbmodem11206 (the acfx CDC that the SVF image presents) never appears, and `ffmpeg -f avfoundation -list_devices` shows NO "acfx Audio" device. This is precisely the predicted dead-image behavior - the image aborts in PrepareEffect() before tud_task(), so USB never comes up. It is a silent hang (no LD2 fault pattern, reinforcing the "point HardFault vectors at the blink pattern" note above). Consequence for T064: worstBlockMicros is UNMEASURABLE for the delay firmware because it never processes a block; the SVF firmware records wb=65 us (of a 1000 us/block budget). SVF firmware re-flashed afterward to restore the working image.
<!-- SECTION:DESCRIPTION:END -->

## Implementation Notes

<!-- SECTION:NOTES:BEGIN -->
Closed: Bounded heap-free ModulatedDelayEffect ships as acfx_nucleo_lofi_delay; boots+enumerates+delays live on the F446 (research R16). Old unbounded target dropped (option b).
<!-- SECTION:NOTES:END -->
