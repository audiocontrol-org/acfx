/*
 * Linker script for the STM32F446RE (Nucleo-F446RE), Cortex-M4F.
 *
 * Memory layout (verified against the pinned CMSIS header, under
 * external/.cpm-cache/cmsis_device_f4/, at Include/stm32f446xx.h):
 *   - FLASH: 512 KB starting at 0x08000000 (FLASH_BASE .. FLASH_END).
 *   - RAM:   128 KB starting at 0x20000000. The F446 exposes this as
 *            SRAM1 (112 KB @ 0x20000000) contiguous with SRAM2 (16 KB @
 *            0x2001C000), so the pair is modeled here as one 128 KB region.
 *
 * Exported symbols consumed by the C-runtime startup (startup.cpp, T071)
 * and the vector table (vector-table.cpp, T011):
 *   _estack          initial stack pointer = top of RAM
 *   _sidata          LMA (flash) start of the .data initializer image
 *   _sdata / _edata  VMA (RAM) start/end of .data
 *   _sbss / _ebss    start/end of .bss
 */

ENTRY(Reset_Handler)

MEMORY
{
  FLASH (rx)  : ORIGIN = 0x08000000, LENGTH = 512K
  RAM (xrw)   : ORIGIN = 0x20000000, LENGTH = 128K
}

/* Minimum heap/stack reservation; a link error here means .data + .bss
 * have left too little RAM for the application. */
_Min_Heap_Size  = 0x200;
_Min_Stack_Size = 0x400;

SECTIONS
{
  /* Vector table first, at the image base, so the boot ROM finds it.
   * KEEP() is required: every firmware target links with --gc-sections,
   * and nothing else references this section by name. */
  .isr_vector :
  {
    . = ALIGN(4);
    KEEP(*(.isr_vector))
    . = ALIGN(4);
  } >FLASH

  .text :
  {
    . = ALIGN(4);
    *(.text)
    *(.text*)
    *(.glue_7)
    *(.glue_7t)
    *(.eh_frame)

    KEEP(*(.init))
    KEEP(*(.fini))

    . = ALIGN(4);
    _etext = .;
  } >FLASH

  .rodata :
  {
    . = ALIGN(4);
    *(.rodata)
    *(.rodata*)
    . = ALIGN(4);
  } >FLASH

  .ARM.extab :
  {
    *(.ARM.extab* .gnu.linkonce.armextab.*)
  } >FLASH

  .ARM.exidx :
  {
    __exidx_start = .;
    *(.ARM.exidx*)
    __exidx_end = .;
  } >FLASH

  /* C++ static-constructor tables; __libc_init_array walks these.
   * KEEP() because nothing else references them by name. */
  .preinit_array :
  {
    PROVIDE_HIDDEN(__preinit_array_start = .);
    KEEP(*(.preinit_array*))
    PROVIDE_HIDDEN(__preinit_array_end = .);
  } >FLASH

  .init_array :
  {
    PROVIDE_HIDDEN(__init_array_start = .);
    KEEP(*(SORT(.init_array.*)))
    KEEP(*(.init_array*))
    PROVIDE_HIDDEN(__init_array_end = .);
  } >FLASH

  .fini_array :
  {
    PROVIDE_HIDDEN(__fini_array_start = .);
    KEEP(*(SORT(.fini_array.*)))
    KEEP(*(.fini_array*))
    PROVIDE_HIDDEN(__fini_array_end = .);
  } >FLASH

  /* Initialized data: lives in RAM at runtime (VMA), initializer image
   * is stored in FLASH (LMA) and copied by Reset_Handler using _sidata. */
  .data :
  {
    . = ALIGN(4);
    _sdata = .;
    *(.data)
    *(.data*)
    . = ALIGN(4);
    _edata = .;
  } >RAM AT> FLASH

  _sidata = LOADADDR(.data);

  /* Zero-initialized data, cleared by Reset_Handler using _sbss/_ebss. */
  .bss :
  {
    . = ALIGN(4);
    _sbss = .;
    __bss_start__ = _sbss;
    *(.bss)
    *(.bss*)
    *(COMMON)
    . = ALIGN(4);
    _ebss = .;
    __bss_end__ = _ebss;
  } >RAM

  /* Heap/stack reservation: not a real allocation, just a link-time
   * guard that .data + .bss + this region still fit in RAM. */
  ._user_heap_stack :
  {
    . = ALIGN(8);
    PROVIDE(end = .);
    PROVIDE(_end = .);
    . = . + _Min_Heap_Size;
    . = . + _Min_Stack_Size;
    . = ALIGN(8);
  } >RAM

  .ARM.attributes 0 : { *(.ARM.attributes) }

  /DISCARD/ :
  {
    libc.a ( * )
    libm.a ( * )
    libgcc.a ( * )
  }
}

/* Initial stack pointer: top of RAM, stack grows down. */
_estack = ORIGIN(RAM) + LENGTH(RAM);
