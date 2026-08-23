/*
 * Interrupt vector table for the STM32F446RE (Nucleo-F446RE), Cortex-M4F.
 *
 * The table's extent is derived from the pinned CMSIS device header's
 * IRQn_Type enum (external/.cpm-cache/cmsis_device_f4/.../Include/
 * stm32f446xx.h) rather than hand-counted: kExternalInterruptCount is
 * FMPI2C1_ER_IRQn + 1, the enum's last (highest-numbered) external
 * interrupt. A table sized only for the core exceptions would send the
 * NVIC past the end of the array the moment any interrupt at or above
 * that boundary fires — notably OTG_FS_IRQn, which this device places at
 * 67, well past a core-exceptions-only table's bounds.
 *
 * Layout, matching the Armv7-M vector table format:
 *   [0]        initial stack pointer (the address of _estack, a linker
 *              symbol from nucleo-f446.ld — not a value to dereference)
 *   [1]        Reset_Handler (owned and defined by startup.cpp)
 *   [2..15]    the fixed Cortex-M4 exceptions, in architecture order,
 *              with the reserved slots left null
 *   [16..]     one slot per external interrupt number 0..FMPI2C1_ER_IRQn,
 *              in IRQn order; slots the CMSIS enum leaves unnamed (this
 *              device does not implement every IRQ number the shared
 *              STM32F4-family header reserves) are left null
 *
 * Every named exception and external-interrupt handler is a weak alias
 * to Default_Handler (an infinite loop), so a later translation unit can
 * override any one of them — e.g. OTG_FS_IRQHandler — simply by defining
 * a strong symbol of the same name; the linker prefers the strong symbol
 * and this table's function pointer resolves to it unchanged.
 *
 * Placed in the .isr_vector section, which the linker script positions
 * first in flash (and KEEPs against --gc-sections) so the boot ROM finds
 * it at the image base.
 */

#include <cstdint>

#include "stm32f446xx.h"

extern "C" {
extern std::uint32_t _estack;
void Reset_Handler();
}

namespace {

constexpr unsigned kCoreExceptionCount = 16;
constexpr unsigned kExternalInterruptCount =
    static_cast<unsigned>(FMPI2C1_ER_IRQn) + 1;
constexpr unsigned kVectorTableLength =
    kCoreExceptionCount + kExternalInterruptCount;

}  // namespace

extern "C" {

void Default_Handler()
{
  for (;;) {
  }
}

#define ACFX_ISR(name) \
  void name() __attribute__((weak, alias("Default_Handler")))

ACFX_ISR(NMI_Handler);
ACFX_ISR(HardFault_Handler);
ACFX_ISR(MemManage_Handler);
ACFX_ISR(BusFault_Handler);
ACFX_ISR(UsageFault_Handler);
ACFX_ISR(SVC_Handler);
ACFX_ISR(DebugMon_Handler);
ACFX_ISR(PendSV_Handler);
ACFX_ISR(SysTick_Handler);

ACFX_ISR(WWDG_IRQHandler);
ACFX_ISR(PVD_IRQHandler);
ACFX_ISR(TAMP_STAMP_IRQHandler);
ACFX_ISR(RTC_WKUP_IRQHandler);
ACFX_ISR(FLASH_IRQHandler);
ACFX_ISR(RCC_IRQHandler);
ACFX_ISR(EXTI0_IRQHandler);
ACFX_ISR(EXTI1_IRQHandler);
ACFX_ISR(EXTI2_IRQHandler);
ACFX_ISR(EXTI3_IRQHandler);
ACFX_ISR(EXTI4_IRQHandler);
ACFX_ISR(DMA1_Stream0_IRQHandler);
ACFX_ISR(DMA1_Stream1_IRQHandler);
ACFX_ISR(DMA1_Stream2_IRQHandler);
ACFX_ISR(DMA1_Stream3_IRQHandler);
ACFX_ISR(DMA1_Stream4_IRQHandler);
ACFX_ISR(DMA1_Stream5_IRQHandler);
ACFX_ISR(DMA1_Stream6_IRQHandler);
ACFX_ISR(ADC_IRQHandler);
ACFX_ISR(CAN1_TX_IRQHandler);
ACFX_ISR(CAN1_RX0_IRQHandler);
ACFX_ISR(CAN1_RX1_IRQHandler);
ACFX_ISR(CAN1_SCE_IRQHandler);
ACFX_ISR(EXTI9_5_IRQHandler);
ACFX_ISR(TIM1_BRK_TIM9_IRQHandler);
ACFX_ISR(TIM1_UP_TIM10_IRQHandler);
ACFX_ISR(TIM1_TRG_COM_TIM11_IRQHandler);
ACFX_ISR(TIM1_CC_IRQHandler);
ACFX_ISR(TIM2_IRQHandler);
ACFX_ISR(TIM3_IRQHandler);
ACFX_ISR(TIM4_IRQHandler);
ACFX_ISR(I2C1_EV_IRQHandler);
ACFX_ISR(I2C1_ER_IRQHandler);
ACFX_ISR(I2C2_EV_IRQHandler);
ACFX_ISR(I2C2_ER_IRQHandler);
ACFX_ISR(SPI1_IRQHandler);
ACFX_ISR(SPI2_IRQHandler);
ACFX_ISR(USART1_IRQHandler);
ACFX_ISR(USART2_IRQHandler);
ACFX_ISR(USART3_IRQHandler);
ACFX_ISR(EXTI15_10_IRQHandler);
ACFX_ISR(RTC_Alarm_IRQHandler);
ACFX_ISR(OTG_FS_WKUP_IRQHandler);
ACFX_ISR(TIM8_BRK_TIM12_IRQHandler);
ACFX_ISR(TIM8_UP_TIM13_IRQHandler);
ACFX_ISR(TIM8_TRG_COM_TIM14_IRQHandler);
ACFX_ISR(TIM8_CC_IRQHandler);
ACFX_ISR(DMA1_Stream7_IRQHandler);
ACFX_ISR(FMC_IRQHandler);
ACFX_ISR(SDIO_IRQHandler);
ACFX_ISR(TIM5_IRQHandler);
ACFX_ISR(SPI3_IRQHandler);
ACFX_ISR(UART4_IRQHandler);
ACFX_ISR(UART5_IRQHandler);
ACFX_ISR(TIM6_DAC_IRQHandler);
ACFX_ISR(TIM7_IRQHandler);
ACFX_ISR(DMA2_Stream0_IRQHandler);
ACFX_ISR(DMA2_Stream1_IRQHandler);
ACFX_ISR(DMA2_Stream2_IRQHandler);
ACFX_ISR(DMA2_Stream3_IRQHandler);
ACFX_ISR(DMA2_Stream4_IRQHandler);
ACFX_ISR(CAN2_TX_IRQHandler);
ACFX_ISR(CAN2_RX0_IRQHandler);
ACFX_ISR(CAN2_RX1_IRQHandler);
ACFX_ISR(CAN2_SCE_IRQHandler);
ACFX_ISR(OTG_FS_IRQHandler);
ACFX_ISR(DMA2_Stream5_IRQHandler);
ACFX_ISR(DMA2_Stream6_IRQHandler);
ACFX_ISR(DMA2_Stream7_IRQHandler);
ACFX_ISR(USART6_IRQHandler);
ACFX_ISR(I2C3_EV_IRQHandler);
ACFX_ISR(I2C3_ER_IRQHandler);
ACFX_ISR(OTG_HS_EP1_OUT_IRQHandler);
ACFX_ISR(OTG_HS_EP1_IN_IRQHandler);
ACFX_ISR(OTG_HS_WKUP_IRQHandler);
ACFX_ISR(OTG_HS_IRQHandler);
ACFX_ISR(DCMI_IRQHandler);
ACFX_ISR(FPU_IRQHandler);
ACFX_ISR(SPI4_IRQHandler);
ACFX_ISR(SAI1_IRQHandler);
ACFX_ISR(SAI2_IRQHandler);
ACFX_ISR(QUADSPI_IRQHandler);
ACFX_ISR(CEC_IRQHandler);
ACFX_ISR(SPDIF_RX_IRQHandler);
ACFX_ISR(FMPI2C1_EV_IRQHandler);
ACFX_ISR(FMPI2C1_ER_IRQHandler);

#undef ACFX_ISR

}  // extern "C"

namespace {

using Handler = void (*)();

}  // namespace

// Entry 0 holds the initial stack pointer, a data address rather than a
// handler; taking &_estack and reinterpreting it as a Handler is the
// standard, toolchain-supported way to place it in a function-pointer
// table (mirrors what CMSIS's own C startup files do with a plain cast).
extern "C" __attribute__((section(".isr_vector"), used))
const Handler g_vectorTable[kVectorTableLength] = {
  reinterpret_cast<Handler>(&_estack),
  Reset_Handler,
  NMI_Handler,
  HardFault_Handler,
  MemManage_Handler,
  BusFault_Handler,
  UsageFault_Handler,
  nullptr,  // reserved
  nullptr,  // reserved
  nullptr,  // reserved
  nullptr,  // reserved
  SVC_Handler,
  DebugMon_Handler,
  nullptr,  // reserved
  PendSV_Handler,
  SysTick_Handler,

  WWDG_IRQHandler,  // IRQn 0: WWDG_IRQn
  PVD_IRQHandler,  // IRQn 1: PVD_IRQn
  TAMP_STAMP_IRQHandler,  // IRQn 2: TAMP_STAMP_IRQn
  RTC_WKUP_IRQHandler,  // IRQn 3: RTC_WKUP_IRQn
  FLASH_IRQHandler,  // IRQn 4: FLASH_IRQn
  RCC_IRQHandler,  // IRQn 5: RCC_IRQn
  EXTI0_IRQHandler,  // IRQn 6: EXTI0_IRQn
  EXTI1_IRQHandler,  // IRQn 7: EXTI1_IRQn
  EXTI2_IRQHandler,  // IRQn 8: EXTI2_IRQn
  EXTI3_IRQHandler,  // IRQn 9: EXTI3_IRQn
  EXTI4_IRQHandler,  // IRQn 10: EXTI4_IRQn
  DMA1_Stream0_IRQHandler,  // IRQn 11: DMA1_Stream0_IRQn
  DMA1_Stream1_IRQHandler,  // IRQn 12: DMA1_Stream1_IRQn
  DMA1_Stream2_IRQHandler,  // IRQn 13: DMA1_Stream2_IRQn
  DMA1_Stream3_IRQHandler,  // IRQn 14: DMA1_Stream3_IRQn
  DMA1_Stream4_IRQHandler,  // IRQn 15: DMA1_Stream4_IRQn
  DMA1_Stream5_IRQHandler,  // IRQn 16: DMA1_Stream5_IRQn
  DMA1_Stream6_IRQHandler,  // IRQn 17: DMA1_Stream6_IRQn
  ADC_IRQHandler,  // IRQn 18: ADC_IRQn
  CAN1_TX_IRQHandler,  // IRQn 19: CAN1_TX_IRQn
  CAN1_RX0_IRQHandler,  // IRQn 20: CAN1_RX0_IRQn
  CAN1_RX1_IRQHandler,  // IRQn 21: CAN1_RX1_IRQn
  CAN1_SCE_IRQHandler,  // IRQn 22: CAN1_SCE_IRQn
  EXTI9_5_IRQHandler,  // IRQn 23: EXTI9_5_IRQn
  TIM1_BRK_TIM9_IRQHandler,  // IRQn 24: TIM1_BRK_TIM9_IRQn
  TIM1_UP_TIM10_IRQHandler,  // IRQn 25: TIM1_UP_TIM10_IRQn
  TIM1_TRG_COM_TIM11_IRQHandler,  // IRQn 26: TIM1_TRG_COM_TIM11_IRQn
  TIM1_CC_IRQHandler,  // IRQn 27: TIM1_CC_IRQn
  TIM2_IRQHandler,  // IRQn 28: TIM2_IRQn
  TIM3_IRQHandler,  // IRQn 29: TIM3_IRQn
  TIM4_IRQHandler,  // IRQn 30: TIM4_IRQn
  I2C1_EV_IRQHandler,  // IRQn 31: I2C1_EV_IRQn
  I2C1_ER_IRQHandler,  // IRQn 32: I2C1_ER_IRQn
  I2C2_EV_IRQHandler,  // IRQn 33: I2C2_EV_IRQn
  I2C2_ER_IRQHandler,  // IRQn 34: I2C2_ER_IRQn
  SPI1_IRQHandler,  // IRQn 35: SPI1_IRQn
  SPI2_IRQHandler,  // IRQn 36: SPI2_IRQn
  USART1_IRQHandler,  // IRQn 37: USART1_IRQn
  USART2_IRQHandler,  // IRQn 38: USART2_IRQn
  USART3_IRQHandler,  // IRQn 39: USART3_IRQn
  EXTI15_10_IRQHandler,  // IRQn 40: EXTI15_10_IRQn
  RTC_Alarm_IRQHandler,  // IRQn 41: RTC_Alarm_IRQn
  OTG_FS_WKUP_IRQHandler,  // IRQn 42: OTG_FS_WKUP_IRQn
  TIM8_BRK_TIM12_IRQHandler,  // IRQn 43: TIM8_BRK_TIM12_IRQn
  TIM8_UP_TIM13_IRQHandler,  // IRQn 44: TIM8_UP_TIM13_IRQn
  TIM8_TRG_COM_TIM14_IRQHandler,  // IRQn 45: TIM8_TRG_COM_TIM14_IRQn
  TIM8_CC_IRQHandler,  // IRQn 46: TIM8_CC_IRQn
  DMA1_Stream7_IRQHandler,  // IRQn 47: DMA1_Stream7_IRQn
  FMC_IRQHandler,  // IRQn 48: FMC_IRQn
  SDIO_IRQHandler,  // IRQn 49: SDIO_IRQn
  TIM5_IRQHandler,  // IRQn 50: TIM5_IRQn
  SPI3_IRQHandler,  // IRQn 51: SPI3_IRQn
  UART4_IRQHandler,  // IRQn 52: UART4_IRQn
  UART5_IRQHandler,  // IRQn 53: UART5_IRQn
  TIM6_DAC_IRQHandler,  // IRQn 54: TIM6_DAC_IRQn
  TIM7_IRQHandler,  // IRQn 55: TIM7_IRQn
  DMA2_Stream0_IRQHandler,  // IRQn 56: DMA2_Stream0_IRQn
  DMA2_Stream1_IRQHandler,  // IRQn 57: DMA2_Stream1_IRQn
  DMA2_Stream2_IRQHandler,  // IRQn 58: DMA2_Stream2_IRQn
  DMA2_Stream3_IRQHandler,  // IRQn 59: DMA2_Stream3_IRQn
  DMA2_Stream4_IRQHandler,  // IRQn 60: DMA2_Stream4_IRQn
  nullptr,  // IRQn 61: reserved (unimplemented on this device)
  nullptr,  // IRQn 62: reserved (unimplemented on this device)
  CAN2_TX_IRQHandler,  // IRQn 63: CAN2_TX_IRQn
  CAN2_RX0_IRQHandler,  // IRQn 64: CAN2_RX0_IRQn
  CAN2_RX1_IRQHandler,  // IRQn 65: CAN2_RX1_IRQn
  CAN2_SCE_IRQHandler,  // IRQn 66: CAN2_SCE_IRQn
  OTG_FS_IRQHandler,  // IRQn 67: OTG_FS_IRQn
  DMA2_Stream5_IRQHandler,  // IRQn 68: DMA2_Stream5_IRQn
  DMA2_Stream6_IRQHandler,  // IRQn 69: DMA2_Stream6_IRQn
  DMA2_Stream7_IRQHandler,  // IRQn 70: DMA2_Stream7_IRQn
  USART6_IRQHandler,  // IRQn 71: USART6_IRQn
  I2C3_EV_IRQHandler,  // IRQn 72: I2C3_EV_IRQn
  I2C3_ER_IRQHandler,  // IRQn 73: I2C3_ER_IRQn
  OTG_HS_EP1_OUT_IRQHandler,  // IRQn 74: OTG_HS_EP1_OUT_IRQn
  OTG_HS_EP1_IN_IRQHandler,  // IRQn 75: OTG_HS_EP1_IN_IRQn
  OTG_HS_WKUP_IRQHandler,  // IRQn 76: OTG_HS_WKUP_IRQn
  OTG_HS_IRQHandler,  // IRQn 77: OTG_HS_IRQn
  DCMI_IRQHandler,  // IRQn 78: DCMI_IRQn
  nullptr,  // IRQn 79: reserved (unimplemented on this device)
  nullptr,  // IRQn 80: reserved (unimplemented on this device)
  FPU_IRQHandler,  // IRQn 81: FPU_IRQn
  nullptr,  // IRQn 82: reserved (unimplemented on this device)
  nullptr,  // IRQn 83: reserved (unimplemented on this device)
  SPI4_IRQHandler,  // IRQn 84: SPI4_IRQn
  nullptr,  // IRQn 85: reserved (unimplemented on this device)
  nullptr,  // IRQn 86: reserved (unimplemented on this device)
  SAI1_IRQHandler,  // IRQn 87: SAI1_IRQn
  nullptr,  // IRQn 88: reserved (unimplemented on this device)
  nullptr,  // IRQn 89: reserved (unimplemented on this device)
  nullptr,  // IRQn 90: reserved (unimplemented on this device)
  SAI2_IRQHandler,  // IRQn 91: SAI2_IRQn
  QUADSPI_IRQHandler,  // IRQn 92: QUADSPI_IRQn
  CEC_IRQHandler,  // IRQn 93: CEC_IRQn
  SPDIF_RX_IRQHandler,  // IRQn 94: SPDIF_RX_IRQn
  FMPI2C1_EV_IRQHandler,  // IRQn 95: FMPI2C1_EV_IRQn
  FMPI2C1_ER_IRQHandler,  // IRQn 96: FMPI2C1_ER_IRQn
};

static_assert(kVectorTableLength == 113,
              "vector table length must track the CMSIS IRQn_Type range; "
              "if this fires, the device header's external-interrupt range "
              "changed and the table above must be re-derived to match");
