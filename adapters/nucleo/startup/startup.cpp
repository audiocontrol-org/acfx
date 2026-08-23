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

extern "C" void Reset_Handler()
{
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
