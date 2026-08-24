// Nucleo adapter shim: clock, GPIO/LED, TinyUSB init, the OTG_FS ISR and the
// service loop. This file currently holds the SystemCoreClock definition, the
// fault-LED GPIO init, the call into the register-level clock bring-up, and
// the call into the OTG_FS pin configuration (which live in the sibling
// clock-init.h, otg-fs-gpio-init.h and usb-audio-service.h purely so no single
// file outgrows the repo's file-size limit); the remaining shim
// responsibilities land as the adapter is built out.
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
#include "otg-fs-gpio-init.h"

// The audio data path's shim half (T032): the adapter's ring/stats instances
// and ServiceUsbAudioOut(), which is the one tud_audio_read() call. Split into
// a sibling header for the same file-size reason as clock-init.h; the
// platform-independent logic it wires up lives under support/ and is what the
// host doctest binary exercises. Included exactly once, here.
#include "usb-audio-service.h"

// TinyUSB's own public API (tusb_init/tusb_int_handler/tud_task). Pulled in as
// a SYSTEM include by acfx_nucleo_tinyusb's PUBLIC target_include_directories
// (adapters/nucleo/CMakeLists.txt), which is also where tusb_config.h's
// directory (this one) is added to the include path so tusb_option.h's bare
// `#include "tusb_config.h"` resolves. This header is C++-safe: it wraps its
// declarations in `#ifdef __cplusplus extern "C" { ... }`, so no extern "C"
// wrapper is needed here.
#include "tusb.h"

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

// Enables the OTG_FS peripheral's AHB2 bus clock (RCC_AHB2ENR.OTGFSEN).
//
// MUST run before InitTinyUsbStack() below (see main()'s ordering comment).
// It would be reasonable to assume TinyUSB's own STM32 DWC2 port does this as
// part of bringing the core up — it has a function named exactly for that,
// dwc2_clock_init(rhport, role), called first thing inside dcd_init()
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
void InitOtgFsClock()
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
// main()'s handling of it below. `tusb_init`'s 2-argument form expands
// directly to `tusb_rhport_init(0, NULL)` (src/tusb.h), which is itself
// `TU_ASSERT`-guarded at every internal step (device-controller init
// included), so a `false` return means SOMETHING in stack bring-up failed,
// not merely "not yet ready".
[[nodiscard]] bool InitTinyUsbStack()
{
  return tusb_init(0, nullptr);
}

}  // namespace

// The OTG_FS interrupt handler (FR-046). Overrides the WEAK
// `OTG_FS_IRQHandler` alias to `Default_Handler` that vector-table.cpp
// declares for every IRQ slot (see that file's header comment, which names
// this exact override as the intended mechanism) — giving this an
// `extern "C"` definition with C linkage and the exact CMSIS name is
// sufficient; no vector-table edit is needed or made.
//
// THIS ONLY ENQUEUES, per FR-046 and the task brief: `tusb_int_handler`
// (declared in tusb.h, defined in tusb.c, confirmed by name against both the
// pinned tree and this repo's own vendored hw/bsp/stm32f4/family.c, which
// installs an OTG_FS_IRQHandler calling this exact function the exact same
// way) reads the DWC2 controller's interrupt-status registers and pushes
// decoded events onto TinyUSB's internal queue for tud_task() to drain from
// the main loop below. No audio work, no DSP, no blocking call, no LED
// access happens here or in anything this calls — SetFaultLed()/
// BusyWaitApprox() above are never reachable from an interrupt context.
//
// `rhport = 0`: the only root-hub port this adapter uses (CFG_TUSB_RHPORT0_MODE
// in tusb_config.h; the same 0 passed to InitTinyUsbStack() above).
// `in_isr = true`: this function IS an ISR — it tells TinyUSB's internal
// event queue push to use its ISR-safe path rather than the task-context one.
//
// Interrupt priority: left at the CMSIS/NVIC reset default (0 — the highest
// priority on this part's 4-bit priority scheme), considered and not
// changed. This is deliberate, not an oversight: OTG_FS is currently the
// ONLY interrupt source enabled anywhere in this firmware (no SysTick, no
// other peripheral IRQ), so there is no other interrupt for it to preempt or
// be preempted by, and therefore no priority ordering to get wrong. A later
// task that adds a second interrupt source is the one that needs to revisit
// this, with both priorities considered together.
//
// NVIC enable: there is deliberately no explicit `NVIC_EnableIRQ(OTG_FS_IRQn)`
// call anywhere in this file. Reading the pinned tree shows TinyUSB already
// does this itself, in the correct order: `tud_rhport_init()`
// (src/device/usbd.c) calls `dcd_init(rhport, rh_init)` and THEN
// `dcd_int_enable(rhport)`; on the DWC2/STM32 port `dcd_int_enable` is
// `dwc2_dcd_int_enable`, which calls `NVIC_EnableIRQ` on this exact IRQ
// number (dwc2_stm32.h's `_dwc2_controller[]` table). So `InitTinyUsbStack()`
// above — a single call to `tusb_init()` — already performs "NVIC enable
// after the stack is initialised" as an internal step, in the right order,
// without this file repeating it. A second, redundant `NVIC_EnableIRQ` call
// here would not be wrong (the call is idempotent), but it would restate,
// outside TinyUSB, an ordering guarantee TinyUSB already owns and already
// gets right — exactly the kind of implicit-dependency-turned-explicit-and-
// therefore-duplicated-logic this codebase's comments elsewhere argue
// against. If a future TinyUSB upgrade ever stops doing this, the OTG_FS
// interrupt simply never firing (board enumerates negotiation-wise up to the
// point USB requires an interrupt, then stalls) is the observable symptom
// that would send a reader back to this comment.
extern "C" void OTG_FS_IRQHandler()
{
  tusb_int_handler(0, true);
}

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
//   2b. PA11/PA12 alternate-function GPIO for OTG_FS (otg-fs-gpio-init.h).
//      Done below. MUST come after step 1 (InitFaultLed enables GPIOA's
//      peripheral clock; see InitOtgFsGpio's precondition comment) — the
//      relative order versus clock bring-up itself does not matter, since
//      GPIO configuration does not depend on SYSCLK.
//   3. OTG_FS peripheral clock (RCC_AHB2ENR.OTGFSEN) — MUST come before step
//      4: see InitOtgFsClock's comment for why TinyUSB will not do this for
//      us. Done below (T030).
//   4. TinyUSB init (tusb_init() against the composite descriptor,
//      InitTinyUsbStack() above) — installs the DWC2 device controller,
//      which per InitTinyUsbStack's/OTG_FS_IRQHandler's comments also
//      enables the OTG_FS NVIC interrupt as its own last internal step, so
//      nothing below needs to enable it again. Done below (T030).
//   5. The tud_task() service loop, run forever (T030; the audio/MIDI data
//      path itself is T032-T035, telemetry is T058 — see the loop's comment).
// All five steps exist now.
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

  // PA11/PA12 alternate-function GPIO for OTG_FS (T025). Requires GPIOA's
  // peripheral clock already enabled — true here because InitFaultLed()
  // above already enabled it (see otg-fs-gpio-init.h's precondition
  // comment); this call must not be moved ahead of that one.
  acfx::nucleo::InitOtgFsGpio();

  // OTG_FS peripheral clock (T030). MUST come before InitTinyUsbStack() below
  // — see InitOtgFsClock's comment for why TinyUSB itself will not do this on
  // this MCU family, and why skipping or reordering this leaves every OTG_FS
  // register reading as zero.
  InitOtgFsClock();

  // TinyUSB device-stack init (FR-046, T030). The return value IS checked
  // (task brief: "no fallbacks... report or halt loudly"). A `false` return
  // means bring-up genuinely failed somewhere inside tusb_init()'s chain of
  // TU_ASSERTs (see InitTinyUsbStack's comment) — not "not ready yet", and
  // there is no retry that would make it ready. This is deliberately NOT
  // folded into SignalFatalClockFaultAndHalt()'s three-pulse pattern above:
  // that pattern is FR-015's specific, already-load-bearing signal for a PLL
  // bring-up failure, and reusing it here for a DIFFERENT failure (USB stack
  // init, with a fully locked, working clock) would make a future reader
  // seeing that exact blink pattern wrongly suspect the clock. A distinct,
  // named fault signal for this case is a design decision this task does not
  // own (no such signal is specified for FR-046); until one is, the correct
  // "halt loudly, no silent degradation" behaviour is: do not enter the
  // tud_task() loop at all. No audio/MIDI/CDC ever comes up, no half-
  // initialised stack is serviced, and the board's silence on the USB side
  // (in contrast to a healthy board's normal enumeration) IS the visible
  // failure signal available to the operator's host-side check.
  const bool tinyUsbInitOk = InitTinyUsbStack();
  if (!tinyUsbInitOk) {
    for (;;) {
    }
  }

  // The service loop (T030). tud_task() drains the event queue
  // OTG_FS_IRQHandler's tusb_int_handler() call enqueues into, running every
  // mounted class driver's (audio/MIDI/CDC) protocol state machine from task
  // context — polled, per tusb_config.h's CFG_TUSB_OS = OPT_OS_NONE (no RTOS
  // to hand events to instead).
  //
  // The OUT half of the audio data path (polled tud_audio_read()) is wired
  // below as of T032. Still to land in this same loop, after tud_task(): block
  // assembly and AppEffect::process() (T033), the IN path's tud_audio_write()
  // (T035), the DWT block timer (T036) and CDC telemetry snapshots (T058).
  // Each lands as an additional statement here — not as a replacement for
  // tud_task(), and not as anything that blocks or spends unbounded time
  // before tud_task() runs again, since USB servicing cadence depends on this
  // loop iterating promptly. ServiceUsbAudioOut() satisfies that: it is one
  // read of at most one maximum packet plus a convert-and-write of at most 49
  // frames, with no wait of any kind (see its comment on the adaptive-sink
  // contract and on why a backlog is drained by coming back here rather than
  // by looping inside).
  for (;;) {
    tud_task();
    acfx::nucleo::ServiceUsbAudioOut();
    // T033/T035/T036 (block assembly, IN path, block timer) and T058 (CDC
    // telemetry) land here.
  }
}
