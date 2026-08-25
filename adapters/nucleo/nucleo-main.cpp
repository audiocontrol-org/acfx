// Nucleo adapter shim: clock, GPIO/LED, TinyUSB init, the OTG_FS ISR and the
// service loop. This file currently holds the SystemCoreClock definition,
// the OTG_FS interrupt handler and the service loop, plus the calls into
// board bring-up, register-level clock bring-up, and OTG_FS pin
// configuration (which live in the sibling board-init.h, clock-init.h,
// otg-fs-gpio-init.h and usb-audio-service.h purely so no single file
// outgrows the repo's file-size limit); the remaining shim responsibilities
// land as the adapter is built out.
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
#include "board-init.h"
#include "clock-init.h"
#include "otg-fs-gpio-init.h"

// The audio data path's shim half (T032): the adapter's ring/stats instances
// and ServiceUsbAudioOut(), which is the one tud_audio_read() call. Split into
// a sibling header for the same file-size reason as clock-init.h; the
// platform-independent logic it wires up lives under support/ and is what the
// host doctest binary exercises. Included exactly once, here.
#include "usb-audio-service.h"

// The compiled-in effect instance and its startup prepare() call (T034;
// FR-036/FR-036a/FR-036b, D28). Split into a sibling header for the same
// file-size reason as the includes above. Included exactly once, here.
#include "effect-instance.h"

// The DSP block path's shim half (T033; FR-030a/FR-036a/FR-037): binds the two
// rings, the effect instance and the stats record to support/dsp-block-path.h,
// and exposes ServiceDspBlock(). Split into a sibling header for the same
// file-size reason as the includes above. Included exactly once, here.
#include "dsp-block-service.h"

// The rate-change poll-loop reaction's shim half (T011; FR-006, research
// §R9): binds the shared rate-change latch (usb-audio-service.h) to
// effect-instance.h's PrepareEffect() and exposes ServiceRateChange(). Must
// be included from nucleo-main.cpp specifically, never from anything
// acfx_nucleo_usb compiles — see that file's own header comment for why.
// Included exactly once, here.
#include "rate-change-service.h"

// The format-transition poll-loop reaction's shim half (T018; FR-006,
// research §R9): binds the shared format-change latch (usb-audio-service.h)
// to a transport-only ring reset and exposes ServiceFormatChange(). The
// structural mirror of rate-change-service.h above, EXCEPT it deliberately
// does NOT call PrepareEffect() — see that header's own comment for why a
// bit-depth change is not a DSP-affecting event. Included exactly once, here.
#include "format-change-service.h"

// The live-parameter path's shim half (T045; FR-039/FR-042, contract PSRC3):
// decodes USB-MIDI Control Change packets, feeds
// support/parameter-source.h's MidiParameterSource, and flushes
// support/parameter-shadow.h's ParameterShadow to the effect via
// ServiceParameters(). Split for the same file-size reason as the includes
// above. Included exactly once, here.
#include "parameter-service.h"

// The main-loop CDC diagnostic-telemetry service's shim half (T058;
// FR-033a/c/d, R7) — see that header for the full contract. Included once, here.
#include "diagnostic-service.h"

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
// BusyWaitApprox() (board-init.h) are never reachable from an interrupt
// context.
//
// `rhport = 0`: the only root-hub port this adapter uses (CFG_TUSB_RHPORT0_MODE
// in tusb_config.h; the same 0 passed to InitTinyUsbStack() (board-init.h)
// below).
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
// (board-init.h), called from main() below — a single call to `tusb_init()`
// — already performs "NVIC enable after the stack is initialised" as an
// internal step, in the right order, without this file repeating it. A
// second, redundant `NVIC_EnableIRQ` call here would not be wrong (the call
// is idempotent), but it would restate,
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
//   1. GPIO/LED init (the fault-signalling LED, board-init.h) — MUST come
//      first, before clock validation, so a clock bring-up failure has a
//      working indicator (FR-015c). Done below.
//   2. Clock bring-up (HSE bypass, PLL configuration). Done below.
//   2a. Lock-failure handling: turning a non-kOk ClockInitStatus into the
//      fatal SignalFatalClockFaultAndHalt() (board-init.h) blink
//      (FR-015/FR-015a). NOT done below — T051 owns that policy; see the
//      discard comment.
//   2b. PA11/PA12 alternate-function GPIO for OTG_FS (otg-fs-gpio-init.h).
//      Done below. MUST come after step 1 (InitFaultLed enables GPIOA's
//      peripheral clock; see InitOtgFsGpio's precondition comment) — the
//      relative order versus clock bring-up itself does not matter, since
//      GPIO configuration does not depend on SYSCLK.
//   3. OTG_FS peripheral clock (RCC_AHB2ENR.OTGFSEN, board-init.h) — MUST
//      come before step 4: see InitOtgFsClock's comment for why TinyUSB will
//      not do this for us. Done below (T030).
//   4. TinyUSB init (tusb_init() against the composite descriptor,
//      InitTinyUsbStack() in board-init.h) — installs the DWC2 device
//      controller, which per InitTinyUsbStack's/OTG_FS_IRQHandler's comments
//      also enables the OTG_FS NVIC interrupt as its own last internal step,
//      so nothing below needs to enable it again. Done below (T030).
//   4a. Prepare the compiled-in effect (effect-instance.h's PrepareEffect(),
//      T034; FR-036/FR-036a/FR-036b, D28) at 48 kHz / 48 frames / 2 channels
//      — MUST come before step 5, since T033's process() calls (landing in
//      the loop below) require prepare() to have already run. The audio
//      stream is not yet running at this point, satisfying the effect's
//      "prepare while stopped" precondition. Done below (T034).
//   5. The tud_task() service loop, run forever (T030; the audio/MIDI data
//      path itself is T032-T035, telemetry is T058 — see the loop's comment).
// All steps through 4a exist now.
int main() {
  acfx::nucleo::InitFaultLed();

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
  acfx::nucleo::InitOtgFsClock();

  // TinyUSB device-stack init (FR-046, T030). The return value IS checked
  // (task brief: "no fallbacks... report or halt loudly"). A `false` return
  // means bring-up genuinely failed somewhere inside tusb_init()'s chain of
  // TU_ASSERTs (see InitTinyUsbStack's comment) — not "not ready yet", and
  // there is no retry that would make it ready. This is deliberately NOT
  // folded into SignalFatalClockFaultAndHalt()'s (board-init.h) three-pulse
  // pattern: that pattern is FR-015's specific, already-load-bearing signal for a PLL
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
  const bool tinyUsbInitOk = acfx::nucleo::InitTinyUsbStack();
  if (!tinyUsbInitOk) {
    for (;;) {
    }
  }

  // Prepare the compiled-in effect (T034; FR-036/FR-036a/FR-036b, D28). See
  // effect-instance.h for the concrete type, the instance, and why the block
  // size here is 48 (kBlockFrames), never 49 (kMaxPacketFrames). Runs once,
  // before the service loop below ever calls into T033's process() path.
  acfx::nucleo::PrepareEffect();

  // Enable the DWT cycle counter and verify it actually took (T036 + T037;
  // FR-034, FR-034b, research R6) so ServiceDspBlock()'s first call has a
  // running g_blockClock to read, or — if this part's DWT is unavailable —
  // g_transportStats.timingSourceLive already reads false and
  // worstBlockMicros already carries the dead-timer sentinel before that
  // first call. Must run before the service loop below, for the same reason
  // PrepareEffect() does: the block path starts consuming it on the very
  // first pass.
  acfx::nucleo::EnableBlockTimer();

  // The service loop (T030). tud_task() drains the event queue
  // OTG_FS_IRQHandler's tusb_int_handler() call enqueues into, running every
  // mounted class driver's (audio/MIDI/CDC) protocol state machine from task
  // context — polled, per tusb_config.h's CFG_TUSB_OS = OPT_OS_NONE (no RTOS
  // to hand events to instead).
  //
  // The OUT half of the audio data path (tud_audio_read()) is wired below
  // as of T032, the 48-frame DSP block as of T033 (ServiceDspBlock() also
  // times it via DWT CYCCNT -> worstBlockMicros, bracketing only
  // effect.process() inside runOneBlock()), the IN half (tud_audio_write())
  // as of T035, and CDC telemetry as of T058. Each lands here as an
  // additional bounded statement, after tud_task() — never a replacement
  // for it, never blocking or open-ended, since USB servicing cadence
  // depends on this loop iterating promptly. All six calls below satisfy that:
  //   ServiceUsbLifecycle() (T056/FR-055): two bool compares + at most two reset()s, edge-only.
  //   ServiceRateChange() (T011; FR-006, research §R9) consumes the rate-change
  //   latch — two field reads and a branch when nothing is pending — and only
  //   on a pending change re-prepares the effect (allocation-free) and resets
  //   both rings, all off EP0 context.
  //   ServiceFormatChange() (T018; FR-006, research §R9) is the same shape as
  //   ServiceRateChange() above but for a bit-depth change: it consumes the
  //   format-change latch and, only on a pending change, resets both rings —
  //   NEVER re-prepares the effect, since a format change carries no
  //   DSP-relevant information (see format-change-service.h's comment).
  //   ServiceUsbAudioOut() is one read of at most one maximum packet plus a convert-and-write of at most 49 frames, with no wait of any kind.
  //   ServiceParameters() (T045; FR-039/FR-042) drains USB-MIDI's finite RX
  //   fifo (tud_midi_packet_read() returns false once empty, never blocks),
  //   forwards Control Change packets to the one registered source, polls it
  //   into the shadow once, and flushes the shadow to the effect once —
  //   dead-banded so a steady controller costs at most one fifo-empty check.
  //   ServiceDspBlock() is at most ONE 48-frame process() call, and none at
  //   all unless the input ring is Running with a whole block in it.
  //   ServiceUsbAudioIn() is at most one room-bounded pull-and-write (or one
  //   carryover retry), and none at all unless the output ring is Running
  //   and TinyUSB's own IN software fifo currently reports room.
  //   ServiceDiagnostics() (T058/FR-033d): one snapshot, one bounded serialize, one non-blocking CDC write; drops (never queues) when closed or full.
  // None loops internally without a bound: a backlog on any side drains by
  // coming back here, far faster than the host's 1 ms cadence.
  //
  // ORDER MATTERS, mildly and deliberately: the OUT service runs first so a
  // packet that just arrived is in the ring before the block path looks at
  // occupancy. ServiceParameters() runs next, before ServiceDspBlock(), so a
  // CC that just arrived lands on the effect before the block that will use
  // it — one pass later than this if the order were reversed. The IN service
  // runs last so a block the DSP just published this pass reaches the output
  // ring before the IN path decides how much to pull. Nothing below depends on
  // this ordering beyond parameters-before-DSP — the rings decouple the other
  // cadences (FR-030a) — and every stage is a no-op when it has nothing to do.
  for (;;) {
    tud_task();  // T053-T055 suspend/resume/mount callbacks fire from in here
    acfx::nucleo::ServiceUsbLifecycle();
    acfx::nucleo::ServiceRateChange();
    acfx::nucleo::ServiceFormatChange();
    acfx::nucleo::ServiceUsbAudioOut();
    acfx::nucleo::ServiceParameters();
    acfx::nucleo::ServiceDspBlock();
    acfx::nucleo::ServiceUsbAudioIn();
    acfx::nucleo::ServiceDiagnostics();
  }
}
