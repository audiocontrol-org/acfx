// Nucleo adapter shim: clock, GPIO/LED, TinyUSB init, the OTG_FS ISR and the
// service loop. This file currently holds the SystemCoreClock definition, the
// fault-LED GPIO init, and the call into the register-level clock bring-up
// (which lives in the sibling clock-init.h purely so neither file outgrows the
// repo's file-size limit); the remaining shim responsibilities land as the
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
// 168 MHz SYSCLK and exactly 48 MHz on PLLQ (the USB full-speed clock).
//
// acfx::nucleo::InitSystemClock() (clock-init.h) is the code that MAKES that
// value true. The two are not independently maintained constants that happen
// to agree: SystemCoreClock is initialized from `kSysclkHz`, which is *derived*
// from the same kHseHz/kPllM/kPllN/kPllP divider constants that compose the
// PLLCFGR word actually written to the hardware. Change a divider and this
// value follows automatically; there is no way to configure the PLL for one
// frequency and advertise another to TinyUSB. That is deliberate, because (per
// the paragraph above) a mismatch here fails SILENTLY.
//
// CMSIS declares `extern uint32_t SystemCoreClock;` inside an
// `#ifdef __cplusplus extern "C" { ... }` block in system_stm32f4xx.h, so the
// declaration has C linkage. This file is compiled as C++; without `extern "C"`
// here, C++ name mangling would give this definition a different symbol name than
// the one CMSIS/TinyUSB reference, producing an undefined-reference link error
// (not a silent bug, but still worth getting right on the first try).

#include <cstdint>

#include "stm32f446xx.h"
#include "clock-init.h"

// See the file header: this is the TinyUSB-visible core clock, and it is
// derived from the same constants InitSystemClock() programs into the PLL.
extern "C" {
uint32_t SystemCoreClock = acfx::nucleo::kSysclkHz;
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

// Approximate busy-wait delay used only by the fault-signal path below, before
// the clock is validated (FR-015c). Deliberately NOT SysTick or any hardware
// timer: both are configured against (or read out) a clock tree that, in the
// fault path, does not yet exist as configured — SystemCoreClock above is
// pinned to the 168 MHz TARGET value and would make a timer-derived delay
// wrong by roughly 10x here, where the core is still running on the
// reset-default 16 MHz HSI RC oscillator.
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
void BusyWaitApprox(uint32_t iterations)
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
constexpr uint32_t kIterationsPerMs = 2667u;

// Short pulse (LED on) and the gap between pulses within a triplet: counted
// for ~225ms each at the estimate above. Comfortably above the threshold
// where individual blinks blur together and comfortably below the point
// where a blink starts reading as a "hold" rather than a pulse.
constexpr uint32_t kPulseIterations = 600000u;      // ~225ms (600,000 / 2,667/ms)

// Long gap after the third pulse, before the pattern repeats: counted for
// ~1.35s at the estimate above, 6x a single pulse's iteration count. Long
// enough to read unmistakably as a pause separating repeats of "three short
// pulses", not as a steady or heartbeat indication and not as a dark board
// between repeats.
constexpr uint32_t kLongGapIterations = 3600000u;   // ~1.35s (3,600,000 / 2,667/ms)

// Fatal clock-fault signal (FR-015a/b/c): blinks LD2 (PA5) three short
// pulses, a long gap, repeating indefinitely, and never returns. This IS the
// halt — there is nothing after the pattern to fall through to, by design
// (FR-015: a PLL-lock failure MUST NOT proceed to USB init or fall back to
// the internal oscillator; the only correct outcome is stopping here,
// visibly, forever).
//
// Callers: none yet. T051 wires the actual PLL-lock-failure check (T024's
// clock bring-up, which per this file's header comment also lands in
// nucleo-main.cpp) to invoke this; until then it is reachable code with no
// caller, which is correct and expected for this task (see T050's scope
// boundary) — not dead code that needs deleting, and not a placeholder that
// needs a fabricated failure condition to "demo" it.
//
// `[[gnu::used]]`: this function (and BusyWaitApprox, only reachable through
// it) currently has zero callers. Without this attribute the compiler's own
// IPA dead-code-elimination pass would omit the function from the compiled
// object entirely before T051 ever adds a call. `used` keeps it emitted in
// the object file (verified: `nm` on nucleo-main.cpp.obj shows it present)
// despite having no caller yet, without fabricating one.
//
// This project's --gc-sections linker flag (see the nucleo toolchain file's
// -ffunction-sections/--gc-sections comment) still discards the still-unused
// section from the final linked .elf at link time — `[[gnu::retain]]`, the
// attribute meant to survive that, is silently ignored by this toolchain's
// linker (tested; it emits a `-Wattributes` warning and has no effect, so it
// is deliberately NOT used here). That is fine: T050's disassembly
// verification below is done against the compiled .o (same optimizer, same
// flags as the shipped build), and once T051 adds the real call the function
// becomes referenced and --gc-sections keeps it in the final .elf like any
// other reachable code.
[[gnu::used]] [[noreturn]] void SignalFatalClockFaultAndHalt()
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

}  // namespace

// Firmware entry point. `Reset_Handler` in the startup code calls this after
// zeroing .bss and copying .data; it must never return (there is nothing to
// return to). This shim's responsibilities land here in order as the later
// build-out tasks run:
//   1. GPIO/LED init (the fault-signalling LED) — MUST come first, before
//      clock validation, so a clock bring-up failure has a working indicator
//      (FR-015c). Done below.
//   2. Clock bring-up (HSE bypass, PLL configuration). Done below.
//   2a. Lock-failure handling: turning a non-kOk ClockInitStatus into the
//      fatal SignalFatalClockFaultAndHalt() blink (FR-015/FR-015a). NOT done
//      below — T051 owns that policy; see the discard comment.
//   3. TinyUSB init (tusb_init() against the composite descriptor).
//   4. The tud_task() service loop, run forever.
// Steps 1 and 2 exist so far, so the loop below is an explicit, empty spin: a
// deliberate placeholder for the eventual service loop, not simulated
// behaviour.
int main() {
  InitFaultLed();

  // THE one place T051 wires the failure policy. `clockStatus` is deliberately
  // captured and then explicitly discarded rather than the call being made and
  // ignored: the discard is a visible, greppable statement that the status is
  // known and not yet acted on. T051 replaces the discard with the FR-015
  // policy — on anything other than kOk, call SignalFatalClockFaultAndHalt()
  // and never return (no HSI fallback, no proceeding to USB init).
  //
  // Nothing here may "handle" a failure by continuing quietly; until T051
  // lands, a bring-up failure leaves the board in the same inert spin it would
  // have been in anyway, with no USB stack yet to mislead anyone.
  const acfx::nucleo::ClockInitStatus clockStatus = acfx::nucleo::InitSystemClock();
  static_cast<void>(clockStatus);

  for (;;) {
  }
}
