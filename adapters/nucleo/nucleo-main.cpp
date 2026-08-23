// Nucleo adapter shim: clock, GPIO/LED, TinyUSB init, the OTG_FS ISR and the
// service loop. This file currently holds only the SystemCoreClock definition;
// the remaining shim responsibilities land as the adapter is built out.
//
// ST's system_stm32f4xx.c is deliberately NOT compiled into this adapter (see the
// design record's D14 and spec FR-013), so nothing else in the link provides the
// `SystemCoreClock` symbol that CMSIS declares (system_stm32f4xx.h) and that
// TinyUSB reads to derive USB full-speed PHY turnaround timing. If this value does
// not match the TRUE configured core clock, the failure is SILENT: the board still
// enumerates over USB, but PHY timing is wrong and audio misbehaves with nothing
// pointing back here. There is no fatal-clock-style diagnostic for this value being
// wrong — it is the single trusted source TinyUSB's timing math is built on.
//
// The value below (168 MHz) is the SYSCLK pinned by FR-014 / decision D6: HSE
// bypass against the ST-Link MCU's 8 MHz MCO, PLL M=4 N=168 P=2 Q=7, yielding
// 168 MHz SYSCLK and exactly 48 MHz on PLLQ (the USB full-speed clock). Clock
// bring-up itself (HSE/PLL configuration, lock-failure handling) is owned by a
// later task, not this one.
//
// CMSIS declares `extern uint32_t SystemCoreClock;` inside an
// `#ifdef __cplusplus extern "C" { ... }` block in system_stm32f4xx.h, so the
// declaration has C linkage. This file is compiled as C++; without `extern "C"`
// here, C++ name mangling would give this definition a different symbol name than
// the one CMSIS/TinyUSB reference, producing an undefined-reference link error
// (not a silent bug, but still worth getting right on the first try).

#include <cstdint>

extern "C" {
uint32_t SystemCoreClock = 168000000u;
}

// Firmware entry point. `Reset_Handler` in the startup code calls this after
// zeroing .bss and copying .data; it must never return (there is nothing to
// return to). This shim's responsibilities land here in order as the later
// build-out tasks run:
//   1. Clock bring-up (HSE bypass, PLL configuration, lock-failure handling).
//   2. GPIO/LED init (the fault-signalling LED).
//   3. TinyUSB init (tusb_init() against the composite descriptor).
//   4. The tud_task() service loop, run forever.
// None of that exists yet, so the loop below is an explicit, empty spin: a
// deliberate placeholder for the eventual service loop, not simulated
// behaviour.
int main() {
  for (;;) {
  }
}
