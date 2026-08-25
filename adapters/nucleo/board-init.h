#pragma once

// Board-level bring-up for the NUCLEO-F446RE, called from nucleo-main.cpp's
// main(): the fault-signalling LED (PA5), its blink-pattern primitives, the
// OTG_FS peripheral clock enable, and the TinyUSB stack init wrapper.
//
// Split out of nucleo-main.cpp purely for file size — logically this IS part
// of the Nucleo shim and has no other consumer. It is deliberately NOT in
// adapters/nucleo/support/: everything under support/ is the platform-
// independent half of the adapter and must compile under the `test` preset
// with no toolchain file, and this header is CMSIS/board (and, for
// InitTinyUsbStack(), TinyUSB stack-init) code by definition. It belongs
// beside clock-init.h and otg-fs-gpio-init.h for the same reason.
//
// The OTG_FS_IRQHandler ISR itself deliberately stays in nucleo-main.cpp
// rather than moving here: it overrides a WEAK alias from vector-table.cpp
// and must remain a strong, defined `extern "C"` symbol in the firmware
// link. Keeping it in the one file that has always defined it removes any
// risk of that override silently reverting to the weak default.

#include <cstdint>

#include "stm32f446xx.h"
#include "tusb.h"

namespace acfx::nucleo {

// Enables the GPIOA peripheral clock and configures PA5 (LD2, the Nucleo
// board's user LED) as a general-purpose push-pull output.
//
// This is the ONLY fault-signalling channel available before the clock is
// validated: without a locked PLL, USB never enumerates, so neither the CDC
// nor the MIDI function can report anything (FR-015a). That forces this init
// to run before clock bring-up/validation (FR-015c), which in turn means it
// necessarily executes on the reset-default HSI (16 MHz internal RC
// oscillator), not the 168 MHz SYSCLK defined in nucleo-main.cpp via
// PLL/HSE — the PLL isn't configured yet when this runs. A later blink
// pattern driven off this clock therefore has an approximate, not exact,
// cadence; per FR-015c that is accepted by design, because the pattern's
// SHAPE (three short pulses, long gap, repeat — FR-015b), not its timing, is
// what carries the fault signal.
//
// Deliberately minimal, per FR-015a ("the minimum needed to signal the
// pattern"): only MODER is touched. OTYPER's reset value already selects
// push-pull (0) and OSPEEDR/PUPDR's reset values (low speed, no pull) are
// adequate for driving an LED, so this does not also write them — doing so
// would be general GPIO-abstraction scope this task explicitly excludes.
inline void InitFaultLed()
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
inline void SetFaultLed(bool on)
{
  GPIOA->BSRR = on ? GPIO_BSRR_BS5 : GPIO_BSRR_BR5;
}

// Approximate busy-wait delay used only by the fault-signal path below, before
// the clock is validated (FR-015c). Deliberately NOT SysTick or any hardware
// timer: both are configured against (or read out) a clock tree that, in the
// fault path, does not yet exist as configured — SystemCoreClock (defined in
// nucleo-main.cpp) is pinned to the 168 MHz TARGET value and would make a
// timer-derived delay wrong by roughly 10x here, where the core is still
// running on the reset-default 16 MHz HSI RC oscillator.
//
// `counter` is `volatile` specifically so the compiler cannot delete or hoist
// the loop. An ordinary (non-volatile) `for (uint32_t i = 0; i < iterations;
// ++i) {}` has no observable side effect, so the optimizer is entitled to —
// and at this project's RelWithDebInfo/-O2 build type, WILL — eliminate it
// entirely, turning three intended pulses into an invisible flicker. Marking
// `counter` volatile forces every decrement to be an observable memory
// read-modify-write that the compiler must actually emit and execute.
//
// The decrement is written as a plain assignment (`counter = counter - 1`)
// rather than `--counter`: C++20 deprecates pre/post increment-decrement on
// a volatile-qualified operand ([depr.volatile.type]), which arm-none-eabi-g++
// flags as -Wvolatile; a plain assignment reads and writes the same volatile
// object without tripping that deprecation.
inline void BusyWaitApprox(uint32_t iterations)
{
  volatile uint32_t counter = iterations;
  while (counter != 0) {
    counter = counter - 1;
  }
}

// --- Fault-pattern timing arithmetic --------------------------------------
// Approximate by design (FR-015c): the pattern's SHAPE, not its timing,
// carries the fault signal, because this runs on the un-configured
// reset-default 16 MHz HSI (see BusyWaitApprox above and InitFaultLed's
// comment). Per the T050 disassembly of this function (arm-none-eabi-objdump
// -d on the compiled object; the whole loop body, `sp`-relative because
// `counter` is a stack-spilled volatile, not a register), each iteration is
// 6 Thumb instructions: `ldr` (reload counter), `subs` (decrement), `str`
// (spill it back — the volatile write), a second `ldr` (reload again for the
// comparison, since a volatile read must not be reused from a register),
// `cmp`, and `bne`. Treating each as roughly one cycle at zero flash wait
// states (a simplification — the two `ldr`/`str` SRAM accesses plausibly
// cost more than register-only ops on real hardware) gives:
//
//   16,000,000 cycles/sec / 6 cycles/iteration ~= 2,666,667 iterations/sec
//                                              ~= 2,667 iterations/ms
//
// This is a rough order-of-magnitude estimate, not a calibrated delay: real
// per-iteration cost depends on exact SRAM access timing, so actual
// durations could plausibly be off by roughly 2x either way. Per FR-015c
// that is acceptable — at these counts a pulse reads as somewhere in the
// 100-450ms range and the long gap as 700ms-2.7s, so the short/long
// contrast (roughly 6x) holds regardless, still unmistakably three quick
// blinks against a much longer pause, and unmistakably blinking against a
// dark board.
inline constexpr uint32_t kIterationsPerMs = 2667u;

// Short pulse (LED on) and the gap between pulses within a triplet: counted
// for ~225ms each at the estimate above. Comfortably above the threshold
// where individual blinks blur together and comfortably below the point
// where a blink starts reading as a "hold" rather than a pulse.
inline constexpr uint32_t kPulseIterations = 600000u;      // ~225ms (600,000 / 2,667/ms)

// Long gap after the third pulse, before the pattern repeats: counted for
// ~1.35s at the estimate above, 6x a single pulse's iteration count. Long
// enough to read unmistakably as a pause separating repeats of "three short
// pulses", not as a steady or heartbeat indication and not as a dark board
// between repeats.
inline constexpr uint32_t kLongGapIterations = 3600000u;   // ~1.35s (3,600,000 / 2,667/ms)

// Fatal clock-fault signal (FR-015a/b/c): blinks LD2 (PA5) three short
// pulses, a long gap, repeating indefinitely, and never returns. This IS the
// halt — there is nothing after the pattern to fall through to, by design
// (FR-015: a PLL-lock failure MUST NOT proceed to USB init or fall back to
// the internal oscillator; the only correct outcome is stopping here,
// visibly, forever).
//
// Callers: none yet. T051 wires the actual PLL-lock-failure check (T024's
// clock bring-up, which lands in nucleo-main.cpp's main()) to invoke this;
// until then it is reachable code with no caller, which is correct and
// expected for this task (see T050's scope boundary) — not dead code that
// needs deleting, and not a placeholder that needs a fabricated failure
// condition to "demo" it.
//
// `[[gnu::used]]`: this function (and BusyWaitApprox, only reachable through
// it) currently has zero callers. Without this attribute the compiler's own
// IPA dead-code-elimination pass would omit the function from the compiled
// object entirely before T051 ever adds a call. `used` keeps it emitted in
// the object file (verified: `nm` on nucleo-main.cpp.obj shows it present,
// since this header is included only from nucleo-main.cpp) despite having
// no caller yet, without fabricating one.
//
// This project's --gc-sections linker flag (see the nucleo toolchain file's
// -ffunction-sections/--gc-sections comment) still discards the still-unused
// section from the final linked .elf at link time — `[[gnu::retain]]`, the
// attribute meant to survive that, is silently ignored by this toolchain's
// linker (tested; it emits a `-Wattributes` warning and has no effect, so it
// is deliberately NOT used here). That is fine: T050's disassembly
// verification is done against the compiled .o (same optimizer, same
// flags as the shipped build), and once T051 adds the real call the function
// becomes referenced and --gc-sections keeps it in the final .elf like any
// other reachable code.
[[gnu::used]] [[noreturn]] inline void SignalFatalClockFaultAndHalt()
{
  for (;;) {
    for (int pulse = 0; pulse < 3; ++pulse) {
      SetFaultLed(true);
      BusyWaitApprox(kPulseIterations);
      SetFaultLed(false);
      if (pulse < 2) {
        BusyWaitApprox(kPulseIterations);  // gap between pulses within the triplet
      }
    }
    BusyWaitApprox(kLongGapIterations);  // long gap before the pattern repeats
  }
}

// Enables the OTG_FS peripheral's AHB2 bus clock (RCC_AHB2ENR.OTGFSEN).
//
// MUST run before InitTinyUsbStack() below (see nucleo-main.cpp's main()
// ordering comment). It would be reasonable to assume TinyUSB's own STM32
// DWC2 port does this as part of bringing the core up — it has a function
// named exactly for that, dwc2_clock_init(rhport, role), called first thing
// inside dcd_init()
// (external/.cpm-cache/tinyusb/.../src/portable/synopsys/dwc2/dcd_dwc2.c:452).
// Reading its STM32 implementation
// (.../src/portable/synopsys/dwc2/dwc2_stm32.h) shows it is an EMPTY inline
// stub on this MCU family — `(void) rhport; (void) role;` and nothing else.
// TinyUSB does not enable this clock, on this port, ever. Nothing else in
// this adapter did either, before this task (otg-fs-gpio-init.h's own header
// comment says so explicitly: pin muxing only, no peripheral clock). Without
// it, every OTG_FS register — including the ones dcd_init() reads while
// bringing the core up — reads back as zero, its TU_ASSERT checks against
// those zero reads fail, and tusb_init() returns false with no further
// diagnostic: a device that is silently, permanently dead on the USB side.
inline void InitOtgFsClock()
{
  RCC->AHB2ENR |= RCC_AHB2ENR_OTGFSEN;
  // Read-back forces the write to retire before InitTinyUsbStack() touches
  // any OTG_FS register immediately after this call returns — the same
  // peripheral-clock-enable/readback pairing clock-init.h's InitSystemClock()
  // uses for PWR and RCC.
  static_cast<void>(RCC->AHB2ENR & RCC_AHB2ENR_OTGFSEN);
}

// Initialises the TinyUSB device stack on root-hub port 0 (FR-046).
//
// Calls `tusb_init(rhport, rh_init)` — the CURRENT (0.21.0) API — not the
// older `tud_init(rhport)`: reading device/usbd.h shows `tud_init` is
// `TU_ATTR_DEPRECATED("Please use tusb_init(rhport, rh_init) instead")` as of
// this pinned version. Passing `rh_init = nullptr` selects tusb.h's
// documented backward-compatible path (`tusb_rhport_init(rhport, NULL)`),
// which builds a `{.role = TUSB_ROLE_DEVICE, .speed = ...}` init struct from
// `CFG_TUSB_RHPORT0_MODE` — exactly the device/full-speed mode
// tusb_config.h (T026) already pins — so there is nothing this call needs to
// state that tusb_config.h does not already state once, in one place.
//
// Returns bool; the return value IS checked (per this task's brief) — see
// nucleo-main.cpp's main() handling of it. `tusb_init`'s 2-argument form
// expands directly to `tusb_rhport_init(0, NULL)` (src/tusb.h), which is
// itself `TU_ASSERT`-guarded at every internal step (device-controller init
// included), so a `false` return means SOMETHING in stack bring-up failed,
// not merely "not yet ready".
[[nodiscard]] inline bool InitTinyUsbStack()
{
  return tusb_init(0, nullptr);
}

}  // namespace acfx::nucleo
