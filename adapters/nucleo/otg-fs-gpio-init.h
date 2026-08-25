#pragma once

// PA11/PA12 alternate-function GPIO configuration for the OTG_FS peripheral
// (T025, FR-017/FR-022): pin-level bring-up ONLY, for the pair the STM32F446
// calls OTG_FS_DM (PA11, D-) and OTG_FS_DP (PA12, D+).
//
// Split out of nucleo-main.cpp for the same reason as clock-init.h: this IS
// logically part of the Nucleo shim with no other consumer, and it belongs
// beside clock-init.h rather than under support/ because it is CMSIS/board
// register code — everything under support/ is the platform-independent half
// of the adapter that must compile under the `test` preset with no toolchain
// file.
//
// SCOPE BOUNDARY: pin configuration ONLY (MODER/OTYPER/OSPEEDR/PUPDR/AFRH).
// This file does NOT enable the OTG_FS peripheral clock (RCC_AHB2ENR.OTGFSEN),
// does NOT touch TinyUSB, does NOT write the OTG_FS interrupt handler, and
// does NOT write the service loop — those are T030 (and tusb_config.h/
// descriptors are T026/T027). It also does not touch PA9 (VBUS) or PA10
// (ID): this board's USB-C breakout has VBUS deliberately UNWIRED (spec
// FR-022/D17), and disabling the OTG_FS core's VBUS sensing so it does not
// wait on a pin that will never see 5V is tusb_config.h's job (T026), not a
// GPIO concern. Nothing here assumes VBUS is present.

#include <cstdint>

#include "stm32f446xx.h"

namespace acfx::nucleo {

// AF10 selects OTG_FS on PA11/PA12. Confirmed against this repo's own
// vendored TinyUSB STM32F4 board-support file (not trusted from a task brief
// alone): external/.cpm-cache/tinyusb/.../hw/bsp/stm32f4/family.c configures
// the identical PA11 (D-) / PA12 (D+) pair on the same STM32F4 family this
// part belongs to with `GPIO_InitStruct.Alternate = GPIO_AF10_OTG_FS;`, plus
// GPIO_MODE_AF_PP (alternate function, push-pull), GPIO_NOPULL, and
// GPIO_SPEED_HIGH (that HAL's name for the fastest of the four OSPEEDR
// encodings) — the same four field choices this function makes at the
// register level.
inline constexpr uint32_t kOtgFsAlternateFunction = 10u;

// Configures PA11 (OTG_FS_DM, D-) and PA12 (OTG_FS_DP, D+) as the OTG_FS
// peripheral's data lines: alternate function AF10, push-pull output,
// no pull, and the highest OSPEEDR speed setting.
//
// PA11/PA12 are the REAL port-A pins soldered to this board's USB-C
// breakout, NOT the Arduino-header-labelled D11/D12 silkscreen on CN9 (those
// map to PA7/PA6 — the documented trap in spec FR-017). This function
// configures PA11/PA12 exclusively; it does not touch PA6/PA7.
//
// PRECONDITION (not enforced by this function): GPIOA's peripheral clock
// (RCC->AHB1ENR.GPIOAEN) must already be enabled before this runs, or every
// write below lands on an unclocked peripheral and is silently dropped by
// the bus — no fault, no fallback, just GPIO registers that read back as if
// nothing happened. This function deliberately does NOT enable GPIOAEN
// itself: nucleo-main.cpp's main() already enables it via InitFaultLed()
// (FR-015a requires the fault LED — also on GPIOA — configured first, before
// anything else), and a second, redundant GPIOAEN write here would obscure
// that InitFaultLed() is the actual, load-bearing reason it is already on by
// the time this runs. Call this function from main() only AFTER
// InitFaultLed(); that ordering is stated explicitly at the call site, not
// left implicit.
//
// OSPEEDR is set to the HIGHEST speed encoding (0b11, "very high speed"),
// not left at MODER's low-speed reset default. This is NOT decoration: USB
// full-speed signalling toggles D+/D- at 12 Mb/s, and a GPIO output stage
// left at low or medium drive strength cannot slew fast enough to produce
// clean edges at that rate. The visible symptom of getting this wrong is not
// an obviously-wrong register value — it is marginal or outright failed
// enumeration, which looks exactly like a wiring fault and is much harder to
// root-cause than a speed field that is simply set incorrectly.
//
// OTYPER and PUPDR are written explicitly (push-pull, no pull) even though
// both already match the silicon reset default for these pins: OTG_FS drives
// D+/D- as a differential pair with its own internal terminations, and an
// internal weak pull here would fight that. Writing it explicitly — rather
// than relying on the reset default staying untouched — states the pin's
// full electrical configuration in one place instead of leaving part of it
// implicit and inherited.
inline void InitOtgFsGpio()
{
  // MODER11/12 = 10 (alternate function). Read-modify-write against the
  // field mask, not a blind OR: MODER's reset value for these pins is
  // analog mode (11), not 00, so an OR-only write would leave a stale high
  // bit behind — the same reasoning InitFaultLed() applies to MODER5 above.
  GPIOA->MODER =
      (GPIOA->MODER & ~(GPIO_MODER_MODER11_Msk | GPIO_MODER_MODER12_Msk)) |
      GPIO_MODER_MODER11_1 | GPIO_MODER_MODER12_1;

  // OTYPER11/12 = 0 (push-pull) — explicit per the comment above.
  GPIOA->OTYPER &= ~(GPIO_OTYPER_OT11_Msk | GPIO_OTYPER_OT12_Msk);

  // OSPEEDR11/12 = 11 (very high speed) — load-bearing, not decoration; see
  // the comment above.
  GPIOA->OSPEEDR =
      (GPIOA->OSPEEDR &
       ~(GPIO_OSPEEDR_OSPEED11_Msk | GPIO_OSPEEDR_OSPEED12_Msk)) |
      GPIO_OSPEEDR_OSPEED11_0 | GPIO_OSPEEDR_OSPEED11_1 |
      GPIO_OSPEEDR_OSPEED12_0 | GPIO_OSPEEDR_OSPEED12_1;

  // PUPDR11/12 = 00 (no pull) — explicit per the comment above.
  GPIOA->PUPDR &= ~(GPIO_PUPDR_PUPD11_Msk | GPIO_PUPDR_PUPD12_Msk);

  // AFRH (GPIOA->AFR[1]): pins 8-15 live in the HIGH alternate-function
  // register (AFR[0]/AFRL covers pins 0-7), so pins 11 and 12 are both here.
  // Both fields get AF10 (kOtgFsAlternateFunction, confirmed above).
  // Read-modify-write against the field masks for the same reason as MODER:
  // AFRH's reset value is 0x00000000, but relying on that instead of
  // explicitly clearing first would silently break on a second call.
  GPIOA->AFR[1] =
      (GPIOA->AFR[1] & ~(GPIO_AFRH_AFSEL11_Msk | GPIO_AFRH_AFSEL12_Msk)) |
      (kOtgFsAlternateFunction << GPIO_AFRH_AFSEL11_Pos) |
      (kOtgFsAlternateFunction << GPIO_AFRH_AFSEL12_Pos);
}

}  // namespace acfx::nucleo
