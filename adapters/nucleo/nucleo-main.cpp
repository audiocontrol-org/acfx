// Nucleo adapter shim: clock, GPIO/LED, TinyUSB init, the OTG_FS ISR and the
// service loop. This file currently holds the SystemCoreClock definition and
// the fault-LED GPIO init; the remaining shim responsibilities land as the
// adapter is built out.
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

#include "stm32f446xx.h"

extern "C" {
uint32_t SystemCoreClock = 168000000u;
}

namespace {

// Enables the GPIOA peripheral clock and configures PA5 (LD2, the Nucleo
// board's user LED) as a general-purpose push-pull output.
//
// This is the ONLY fault-signalling channel available before the clock is
// validated: without a locked PLL, USB never enumerates, so neither the CDC
// nor the MIDI function can report anything (FR-015a). That forces this init
// to run before clock bring-up/validation (FR-015c), which in turn means it
// necessarily executes on the reset-default HSI (16 MHz internal RC
// oscillator), not the 168 MHz SYSCLK defined above via PLL/HSE — the PLL
// isn't configured yet when this runs. A later blink pattern driven off this
// clock therefore has an approximate, not exact, cadence; per FR-015c that is
// accepted by design, because the pattern's SHAPE (three short pulses, long
// gap, repeat — FR-015b), not its timing, is what carries the fault signal.
//
// Deliberately minimal, per FR-015a ("the minimum needed to signal the
// pattern"): only MODER is touched. OTYPER's reset value already selects
// push-pull (0) and OSPEEDR/PUPDR's reset values (low speed, no pull) are
// adequate for driving an LED, so this does not also write them — doing so
// would be general GPIO-abstraction scope this task explicitly excludes.
void InitFaultLed()
{
  RCC->AHB1ENR |= RCC_AHB1ENR_GPIOAEN;

  // MODER5 = 01 (general-purpose output). Read-modify-write against the
  // field mask rather than a plain OR, so this does not depend on MODER's
  // reset value (0x0C000000, i.e. PA5 reset to analog mode = 11) already
  // having the field cleared.
  GPIOA->MODER = (GPIOA->MODER & ~GPIO_MODER_MODER5_Msk) | GPIO_MODER_MODER5_0;
}

// Drives the fault LED (PA5) on or off via GPIOA's BSRR bit-set/reset
// register. BSRR is atomic (a single write, no read-modify-write), which
// matters here even though this file is not yet interrupt-driven: a later
// task's blink loop will call this repeatedly, and BSRR is the correct tool
// for single-pin sets regardless of how it ends up being called.
void SetFaultLed(bool on)
{
  GPIOA->BSRR = on ? GPIO_BSRR_BS5 : GPIO_BSRR_BR5;
}

}  // namespace

// Firmware entry point. `Reset_Handler` in the startup code calls this after
// zeroing .bss and copying .data; it must never return (there is nothing to
// return to). This shim's responsibilities land here in order as the later
// build-out tasks run:
//   1. GPIO/LED init (the fault-signalling LED) — MUST come first, before
//      clock validation, so a clock bring-up failure has a working indicator
//      (FR-015c). Done below.
//   2. Clock bring-up (HSE bypass, PLL configuration, lock-failure handling,
//      blinking the fault LED via SetFaultLed() above on failure).
//   3. TinyUSB init (tusb_init() against the composite descriptor).
//   4. The tud_task() service loop, run forever.
// Only step 1 exists so far, so the loop below is an explicit, empty spin: a
// deliberate placeholder for the eventual service loop, not simulated
// behaviour.
int main() {
  InitFaultLed();

  for (;;) {
  }
}
