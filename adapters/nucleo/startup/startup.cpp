/*
 * C-runtime startup for the STM32F446RE (Nucleo-F446RE), Cortex-M4F.
 *
 * Owns exactly one symbol: Reset_Handler, the target the linker script
 * names via ENTRY(Reset_Handler) and the address the vector table's
 * reset-vector slot must resolve to. It is the first code the core runs
 * out of reset (after the boot ROM loads the initial stack pointer and
 * this handler's address from the vector table), and it is responsible
 * for bringing the C++ runtime up to the point where main() can run:
 *
 *   1. Copy the .data initializer image from its flash load address
 *      into its SRAM runtime address.
 *   2. Zero .bss.
 *   3. Run __libc_init_array(), which walks .preinit_array/.init_array
 *      and executes C++ static constructors — required because this is
 *      a C++ codebase and globals with nontrivial constructors must be
 *      initialized before main() touches them.
 *   4. Call main().
 *   5. Trap if main() ever returns; falling off the end into whatever
 *      follows in flash is not an option on a microcontroller with no
 *      operating system to return to.
 *
 * The addresses copied/cleared here come from linker-script symbols
 * (nucleo-f446.ld): _sidata/_sdata/_edata for the .data image and its
 * RAM destination, _sbss/_ebss for the .bss region. A linker symbol's
 * *value*, as seen from C++, is its address — these are declared as
 * opaque objects and referenced with &, never dereferenced as the
 * scalars their declared type would otherwise suggest.
 */

#include <cstdint>

extern "C" {
extern std::uint32_t _sidata;
extern std::uint32_t _sdata;
extern std::uint32_t _edata;
extern std::uint32_t _sbss;
extern std::uint32_t _ebss;

void __libc_init_array();

void Reset_Handler();
}

// Declared OUTSIDE the extern "C" block on purpose: [basic.start.main] makes a
// program that declares main with a linkage-specification ill-formed. GCC accepts
// it silently, but clang rejects it (-Wmain), and main is emitted unmangled
// regardless, so the C linkage bought nothing.
int main();

// Grant the Cortex-M4F floating-point unit full access (CP10/CP11 in the
// Coprocessor Access Control Register, SCB->CPACR at 0xE000ED88).
//
// THIS MUST RUN BEFORE ANY FLOATING-POINT INSTRUCTION EXECUTES, which in
// practice means before __libc_init_array() -- a static constructor is free to
// touch a float, and this is a DSP codebase.
//
// Why it lives here rather than in a vendor file: the toolchain builds this
// target with `-mfpu=fpv4-sp-d16 -mfloat-abi=hard`
// (cmake/toolchains/nucleo-f446.cmake:56), so the compiler emits VFP
// instructions freely. On reset the FPU is DISABLED, and executing a VFP
// instruction then raises a NOCP ("no coprocessor") UsageFault, which with no
// UsageFault handler installed escalates straight to HardFault. On ST parts
// this enable normally lives in SystemInit() inside system_stm32f4xx.c -- the
// file FR-013 / D14 deliberately does NOT compile. Excluding it was right; what
// was missed is that one line of it still had to be replaced, and nothing did.
//
// Observed on hardware 2026-08-23 before this fix: SCB_CPACR = 0x00000000,
// SCB_CFSR = 0x00080000 (UFSR bit 3, NOCP), SCB_HFSR = 0x40000000 (FORCED).
// The core hard-faulted before USB could come up, so the board never
// enumerated and RCC/OTG registers looked perfectly healthy while nothing ran.
// Written up as backlog TASK-31.
//
// CMSIS headers are deliberately not included here: this file owns the reset
// path and must not depend on device headers, so the register is addressed
// directly with its architectural address, which is identical on every Cortex-M4F.
static void EnableFpu()
{
  constexpr std::uint32_t kCpacrAddress = 0xE000ED88u;
  constexpr std::uint32_t kCp10FullAccess = 3u << 20;  // bits 21:20
  constexpr std::uint32_t kCp11FullAccess = 3u << 22;  // bits 23:22

  auto *const cpacr = reinterpret_cast<volatile std::uint32_t *>(kCpacrAddress);
  *cpacr |= (kCp10FullAccess | kCp11FullAccess);

  // Architecturally required after enabling the FPU: complete the write
  // (DSB) and flush the pipeline (ISB) so no already-fetched instruction
  // executes against the stale, disabled coprocessor state.
  __asm__ volatile("dsb 0xF" ::: "memory");
  __asm__ volatile("isb 0xF" ::: "memory");
}

extern "C" void Reset_Handler()
{
  // 0. Enable the FPU before anything else can execute a float instruction.
  EnableFpu();

  // 1. Copy the .data initializer image from flash (LMA) into SRAM (VMA).
  const std::uint32_t *src = &_sidata;
  std::uint32_t *dst = &_sdata;
  while (dst < &_edata) {
    *dst++ = *src++;
  }

  // 2. Zero .bss.
  dst = &_sbss;
  while (dst < &_ebss) {
    *dst++ = 0;
  }

  // 3. Run C++ static constructors (.preinit_array / .init_array).
  __libc_init_array();

  // 4. Hand off to the application.
  main();

  // 5. main() is not expected to return on this target; if it does,
  // trap here rather than falling off the end of the reset path.
  for (;;) {
  }
}
