#pragma once

// Register-level clock bring-up for the NUCLEO-F446RE (FR-013/FR-014, D6/D14):
// HSE in bypass mode against the ST-Link MCU's 8 MHz MCO, PLL M=4 N=168 P=2
// Q=7, giving 168 MHz SYSCLK and exactly 48 MHz on PLLQ for USB full speed.
//
// Split out of nucleo-main.cpp purely for file size — logically this IS part of
// the Nucleo shim and has no other consumer. It is deliberately NOT in
// adapters/nucleo/support/: everything under support/ is the platform-
// independent half of the adapter and must compile under the `test` preset with
// no toolchain file, and this header is CMSIS/board code by definition.
//
// ST's system_stm32f4xx.c is not compiled into this adapter (FR-013, D14), so
// there is no vendor SystemInit() doing any of this behind our backs. This
// header is the only thing that configures the clock tree.

#include <cstdint>

#include "stm32f446xx.h"

namespace acfx::nucleo {

// --- Clock-tree arithmetic (FR-014 / D6) ----------------------------------
// Every frequency below is DERIVED from the divider constants, never written
// out as a separate literal, so the numbers cannot drift apart:
//
//   VCO input = HSE / M       =   8 MHz / 4   =   2 MHz   (RM0390: 1-2 MHz)
//   VCO       = VCO input * N =   2 MHz * 168 = 336 MHz   (RM0390: 100-432 MHz)
//   SYSCLK    = VCO / P       = 336 MHz / 2   = 168 MHz
//   USB       = VCO / Q       = 336 MHz / 7   =  48 MHz   EXACTLY
//
// The Q=7 leg is the whole reason this operating point was chosen over the
// part's 180 MHz maximum: 180 MHz needs VCO=360, and 360/Q is not integral for
// any legal Q, which would force USB onto PLLSAI (research.md R3). A USB clock
// that is merely "close to" 48 MHz does not fail loudly — it degrades
// enumeration and isochronous timing silently. The static_asserts below make
// the integrality a build error rather than a hardware surprise.
inline constexpr uint32_t kHseHz = 8000000u;  // ST-Link MCO, driven in BYPASS
inline constexpr uint32_t kPllM = 4u;
inline constexpr uint32_t kPllN = 168u;
inline constexpr uint32_t kPllP = 2u;
inline constexpr uint32_t kPllQ = 7u;

static_assert(kHseHz % kPllM == 0u, "VCO input must be integral");
inline constexpr uint32_t kPllVcoInHz = kHseHz / kPllM;
static_assert(kPllVcoInHz == 2000000u, "RM0390 requires PLL input in 1-2 MHz");

inline constexpr uint32_t kPllVcoHz = kPllVcoInHz * kPllN;
static_assert(kPllVcoHz == 336000000u, "VCO must be 336 MHz");
static_assert(kPllVcoHz >= 100000000u && kPllVcoHz <= 432000000u,
              "RM0390 requires the VCO in 100-432 MHz");

static_assert(kPllVcoHz % kPllP == 0u, "SYSCLK must be integral");
inline constexpr uint32_t kSysclkHz = kPllVcoHz / kPllP;
static_assert(kSysclkHz == 168000000u, "FR-014 pins SYSCLK at 168 MHz");

static_assert(kPllVcoHz % kPllQ == 0u,
              "USB clock must be EXACTLY 48 MHz, not merely close");
inline constexpr uint32_t kUsbClkHz = kPllVcoHz / kPllQ;
static_assert(kUsbClkHz == 48000000u, "FR-014 pins the PLLQ leg at 48 MHz");

// Bus prescalers. AHB takes SYSCLK undivided (168 MHz, the part maximum). The
// APB buses have HARD maxima on this part — APB1 42 MHz, APB2 84 MHz — so at
// 168 MHz HCLK they need /4 and /2 respectively. These are not tuning knobs:
// with the reset-default /1 prescalers, engaging the PLL overclocks both
// peripheral buses by 4x and 2x, which is why they are written BEFORE the
// SYSCLK switch in InitSystemClock() below and not after.
static_assert(kSysclkHz / 1u <= 168000000u, "AHB (HCLK) max is 168 MHz");
static_assert(kSysclkHz / 4u <= 42000000u, "APB1 max is 42 MHz");
static_assert(kSysclkHz / 2u <= 84000000u, "APB2 max is 84 MHz");

// RCC_PLLCFGR composed from named CMSIS fields.
//
// PLLP is the trap in this word: the field is an ENCODING, not the divider.
// 00 = /2, 01 = /4, 10 = /6, 11 = /8 — so writing the literal 2 would select
// /6. `kPllP / 2 - 1` is that encoding, and it is written as arithmetic on
// kPllP so it stays correct if the divider ever changes.
inline constexpr uint32_t kPllCfgr =
    (kPllQ << RCC_PLLCFGR_PLLQ_Pos) | RCC_PLLCFGR_PLLSRC_HSE |
    (((kPllP / 2u) - 1u) << RCC_PLLCFGR_PLLP_Pos) |
    (kPllN << RCC_PLLCFGR_PLLN_Pos) | (kPllM << RCC_PLLCFGR_PLLM_Pos);

// The spike ran this exact configuration on the physical board and recorded
// RCC_PLLCFGR reading back as 0x07402a04, with HSERDY=1, PLLRDY=1,
// RCC_CFGR.SWS=PLL and the USB clock measured at exactly 48.0000 MHz
// (research.md R3). Asserting the composed word against that recorded value
// turns the hardware measurement into a COMPILE-TIME check: any future edit to
// the divider constants or the field composition that would change what the
// board actually sees breaks the build here, instead of producing firmware
// that boots at the wrong frequency and misbehaves over USB. It also catches
// the PLLP-encoding mistake described above (writing /6 would give
// 0x07422a04).
static_assert(kPllCfgr == 0x07402a04u,
              "PLLCFGR must match the hardware-verified spike value 0x07402a04");

// --- Bring-up -------------------------------------------------------------

// Which step of InitSystemClock() failed. Returned rather than acted on: this
// header owns the bring-up, T051 owns the POLICY (FR-015: a failure is fatal,
// must not fall back to HSI, must not proceed to USB init). Naming the failing
// step — instead of a bare bool — is what makes an unbootable board
// diagnosable: the only channel available at that point is an LED (FR-015a),
// and T051 can map these to distinguishable signals if it needs to.
enum class ClockInitStatus : uint32_t {
  kOk = 0,
  kHsePriorDisableTimeout,  // HSE would not turn OFF so HSEBYP could be set
  kHseNotReady,             // HSERDY never asserted (see the HSEBYP note below)
  kFlashLatencyRejected,    // FLASH_ACR readback did not show 5 wait states
  kPllPriorDisableTimeout,  // PLL would not turn OFF so PLLCFGR could be written
  kPllNotReady,             // PLLRDY never asserted — the PLL did not lock
  kSysclkSwitchFailed,      // RCC_CFGR.SWS never read back as PLL
};

// Iteration bound for every readiness poll below.
//
// NO wait loop here may be written `while (!ready) {}`. An unbounded spin on a
// clock that never comes up leaves a DARK, silent board — precisely the
// failure mode FR-015a exists to make observable. Every poll is bounded and
// every timeout is reported to the caller.
//
// The bound is deliberately generous rather than calibrated. These loops run
// on the reset-default 16 MHz HSI (the HSE/PLL polls) or at 168 MHz (the SWS
// poll), and each iteration is a volatile register read plus a compare and
// branch — call it single-digit cycles. At 16 MHz that puts this bound in the
// tens of milliseconds, versus a bypass-mode HSE that is ready in microseconds
// (no crystal to start — it is already an oscillating digital input) and a PLL
// that locks in a few hundred microseconds. Orders of magnitude of headroom,
// while still terminating.
inline constexpr uint32_t kReadyPollLimit = 100000u;

// Configures the clock tree to the FR-014 operating point and switches SYSCLK
// onto the PLL. Returns kOk only if every step was confirmed by reading the
// hardware back.
//
// THE ORDER OF THE STEPS BELOW IS LOAD-BEARING. Each numbered block states the
// constraint that pins it. Do not reorder them; several of the orderings are
// the difference between a board that boots and one that hard-faults or comes
// up subtly wrong.
[[nodiscard]] inline ClockInitStatus InitSystemClock()
{
  // --- 1. PWR peripheral clock + regulator voltage scale -------------------
  // Must precede the PLL: the VOS field is only writable while the PLL is off,
  // and the regulator must already be at the right level before the core runs
  // at 168 MHz.
  //
  // Scale 1 (VOS = 0b11) is the choice. RM0390's voltage-scaling table for the
  // F446: Scale 3 tops out at 120 MHz, Scale 2 at 144 MHz with over-drive off,
  // Scale 1 at 168 MHz with over-drive off and 180 MHz with over-drive on.
  // 168 MHz therefore needs Scale 1 and needs NO over-drive — which is a
  // second reason this operating point beats 180 MHz: no PWR_CR.ODEN/ODSWEN
  // dance, no ODRDY/ODSWRDY handshake to get wrong.
  //
  // Deliberately NOT gated on PWR_CSR.VOSRDY. RM0390 documents VOSRDY, but
  // ST's own F446 voltage-scaling path (__HAL_PWR_VOLTAGESCALING_CONFIG, and
  // the CubeMX-generated 168 MHz clock config that uses it) sets VOS and does
  // only a readback — VOSRDY gating belongs to the over-drive sequence, which
  // this operating point does not use. Adding a VOSRDY spin here would create
  // a way for a correctly-clocked board to be declared fatally broken, which
  // is a worse failure than the one it would guard against. The readback below
  // still forces the write itself to retire.
  RCC->APB1ENR |= RCC_APB1ENR_PWREN;
  // Read-back of the enable bit. Writes to an RCC peripheral-clock-enable
  // register take effect after a delay on this bus; a read of the same
  // register stalls until the write has retired, so the PWR register accesses
  // that follow cannot land before PWR is actually clocked.
  static_cast<void>(RCC->APB1ENR & RCC_APB1ENR_PWREN);

  PWR->CR = (PWR->CR & ~PWR_CR_VOS_Msk) | PWR_CR_VOS;  // VOS = 0b11, Scale 1
  static_cast<void>(PWR->CR);

  // --- 2. HSE in BYPASS mode ----------------------------------------------
  // The Nucleo-F446RE has NO crystal on the MCU. The 8 MHz arrives on OSC_IN
  // as a digital square wave from the ST-Link MCU's MCO output, so the HSE
  // oscillator's driver must be bypassed (HSEBYP=1). With HSEBYP=0 the analog
  // oscillator drives a pin that has nothing resonant on it, HSERDY never
  // asserts, and the board never boots — a completely dark failure.
  //
  // RM0390: HSEBYP is writable ONLY while HSEON=0. Rather than assume the
  // reset state (which is only true on the very first call after reset), turn
  // HSE off and wait for HSERDY to clear first. Without that wait, a second
  // call would see a stale HSERDY=1 and "succeed" against a clock that is
  // actually being reconfigured underneath it.
  RCC->CR &= ~RCC_CR_HSEON;
  for (uint32_t poll = 0u;; ++poll) {
    if ((RCC->CR & RCC_CR_HSERDY) == 0u) {
      break;
    }
    if (poll >= kReadyPollLimit) {
      return ClockInitStatus::kHsePriorDisableTimeout;
    }
  }

  RCC->CR |= RCC_CR_HSEBYP;  // MUST be set before HSEON, per the note above
  RCC->CR |= RCC_CR_HSEON;
  for (uint32_t poll = 0u;; ++poll) {
    if ((RCC->CR & RCC_CR_HSERDY) != 0u) {
      break;
    }
    if (poll >= kReadyPollLimit) {
      return ClockInitStatus::kHseNotReady;
    }
  }

  // --- 3. Flash wait states, BEFORE the clock goes up ----------------------
  // At 168 MHz and 2.7-3.6 V the flash needs 5 wait states (RM0390's
  // FLASH_ACR.LATENCY table; the Nucleo runs at 3.3 V). Raising SYSCLK first
  // and the latency afterwards means the core fetches instructions the flash
  // cannot deliver in time: a hard fault, or worse, silently corrupt
  // execution. So latency goes up before the clock does, and it is confirmed
  // by readback before anything downstream runs — FLASH_ACR writes can be
  // ignored (e.g. if the flash interface is busy), and an ignored write here
  // is invisible until the moment it destroys execution.
  //
  // Prefetch and the instruction/data caches are enabled in the same write.
  // They are performance, not correctness, but at 5 wait states running
  // without them gives back a large fraction of the clock increase this whole
  // sequence exists to obtain. Both caches are disabled and empty out of
  // reset, so no ICRST/DCRST flush is needed before enabling them.
  FLASH->ACR = FLASH_ACR_PRFTEN | FLASH_ACR_ICEN | FLASH_ACR_DCEN |
               FLASH_ACR_LATENCY_5WS;
  for (uint32_t poll = 0u;; ++poll) {
    if ((FLASH->ACR & FLASH_ACR_LATENCY_Msk) == FLASH_ACR_LATENCY_5WS) {
      break;
    }
    if (poll >= kReadyPollLimit) {
      return ClockInitStatus::kFlashLatencyRejected;
    }
  }

  // --- 4. Bus prescalers, BEFORE the clock goes up -------------------------
  // See the APB static_asserts above. SYSCLK is still HSI (16 MHz) at this
  // instant, so /1, /4, /2 are harmlessly slow right now and are exactly right
  // the moment the PLL is engaged. Written as their own store, separate from
  // the SW switch in step 6, so that no future edit can fold the two together
  // and leave a window where 168 MHz reaches an APB bus still on its
  // reset-default /1 prescaler.
  RCC->CFGR = (RCC->CFGR &
               ~(RCC_CFGR_HPRE_Msk | RCC_CFGR_PPRE1_Msk | RCC_CFGR_PPRE2_Msk)) |
              RCC_CFGR_HPRE_DIV1 | RCC_CFGR_PPRE1_DIV4 | RCC_CFGR_PPRE2_DIV2;

  // --- 5. Configure and lock the PLL ---------------------------------------
  // RM0390: PLLCFGR is writable only while PLLON=0. Same reasoning as HSEBYP
  // in step 2 — turn it off and wait for PLLRDY to clear rather than trusting
  // the reset state, so a stale PLLRDY can never be mistaken for a fresh lock.
  RCC->CR &= ~RCC_CR_PLLON;
  for (uint32_t poll = 0u;; ++poll) {
    if ((RCC->CR & RCC_CR_PLLRDY) == 0u) {
      break;
    }
    if (poll >= kReadyPollLimit) {
      return ClockInitStatus::kPllPriorDisableTimeout;
    }
  }

  // Whole-register store, not a read-modify-write: every field of PLLCFGR is
  // specified by kPllCfgr, and this is the value the spike read back from real
  // hardware (0x07402a04, reserved bits included). A read-modify-write would
  // preserve PLLCFGR's reset value in the reserved bits and no longer match
  // what was measured.
  RCC->PLLCFGR = kPllCfgr;

  // The USB 48 MHz mux. On the F446 (unlike earlier F4s) PLL48CLK is selected
  // by RCC_DCKCFGR2.CK48MSEL: 0 = PLLQ, 1 = PLLSAI-P. 0 is the reset value, so
  // this write changes no state — it is here so the 48 MHz leg computed above
  // is CONNECTED to USB by an explicit statement rather than by an unstated
  // dependence on a reset default. Read-modify-write, because DCKCFGR2's other
  // fields select clocks for peripherals this adapter does not own.
  RCC->DCKCFGR2 &= ~RCC_DCKCFGR2_CK48MSEL;

  RCC->CR |= RCC_CR_PLLON;
  for (uint32_t poll = 0u;; ++poll) {
    if ((RCC->CR & RCC_CR_PLLRDY) != 0u) {
      break;
    }
    if (poll >= kReadyPollLimit) {
      return ClockInitStatus::kPllNotReady;
    }
  }

  // --- 6. Switch SYSCLK to the PLL, and CONFIRM the switch -----------------
  // Writing SW is a REQUEST. The hardware performs the changeover on its own
  // schedule and reports the clock actually in use in the separate SWS field.
  // Returning after the write alone would mean the rest of the firmware — and
  // SystemCoreClock, and TinyUSB's timing math derived from it — assumes
  // 168 MHz while the core may still be on the 16 MHz HSI. That is a silent
  // 10x error, so poll SWS until it genuinely reads PLL.
  RCC->CFGR = (RCC->CFGR & ~RCC_CFGR_SW_Msk) | RCC_CFGR_SW_PLL;
  for (uint32_t poll = 0u;; ++poll) {
    if ((RCC->CFGR & RCC_CFGR_SWS_Msk) == RCC_CFGR_SWS_PLL) {
      break;
    }
    if (poll >= kReadyPollLimit) {
      return ClockInitStatus::kSysclkSwitchFailed;
    }
  }

  // HSI is left running. That is NOT a fallback (FR-015 / D7 forbid one) and
  // nothing selects it: SW/SWS now read PLL, and every failure path above
  // returns before reaching this point rather than degrading to HSI. It is
  // simply not turned off, because turning it off is a power optimisation this
  // task does not own and one more way to get reset-path behaviour wrong.
  return ClockInitStatus::kOk;
}

}  // namespace acfx::nucleo
