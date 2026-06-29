# Toolchain — Daisy (STM32H750, Cortex-M7) via arm-none-eabi-gcc.
#
# Daisy's SoC is an STM32H750IB (Cortex-M7 w/ double-precision FPU). These flags
# match the libDaisy/Daisy bootloader expectations. The core/ sources are
# platform-independent; this file only describes how the cross-compiler is
# invoked for the daisy adapter target.

set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR arm)

# A bare-metal cross toolchain cannot link a hosted test executable; build a
# static library for CMake's compiler probe instead.
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

find_program(ARM_CC  arm-none-eabi-gcc)
find_program(ARM_CXX arm-none-eabi-g++)
if(NOT ARM_CC OR NOT ARM_CXX)
  message(FATAL_ERROR
    "arm-none-eabi-gcc / arm-none-eabi-g++ not found on PATH. Install the ARM "
    "embedded toolchain to build the daisy preset (no host-side fallback).")
endif()

set(CMAKE_C_COMPILER   "${ARM_CC}")
set(CMAKE_CXX_COMPILER "${ARM_CXX}")
set(CMAKE_ASM_COMPILER "${ARM_CC}")

# The found toolchain must ship the C++ standard library. Homebrew's
# arm-none-eabi-gcc is C-only (no libstdc++/newlib C++ headers), so a build would
# otherwise fail deep inside DaisySP with a confusing "<algorithm>: No such file"
# error. Probe once and fail loud + actionable here instead (Constitution V: no
# silent/host-side fallback). Set ACFX_ARM_CXX_LIBCXX_OK to skip if you know better.
if(NOT DEFINED ACFX_ARM_CXX_LIBCXX_OK)
  set(_acfx_arm_probe "${CMAKE_BINARY_DIR}/acfx-arm-cxx-probe.cpp")
  file(WRITE "${_acfx_arm_probe}" "#include <algorithm>
int main() { return 0; }
")
  execute_process(
    COMMAND "${ARM_CXX}" -std=c++17 -c "${_acfx_arm_probe}" -o "${_acfx_arm_probe}.o"
    RESULT_VARIABLE _acfx_arm_probe_rc OUTPUT_QUIET ERROR_QUIET)
  if(_acfx_arm_probe_rc EQUAL 0)
    set(ACFX_ARM_CXX_LIBCXX_OK TRUE CACHE INTERNAL "ARM toolchain ships the C++ stdlib")
  else()
    set(ACFX_ARM_CXX_LIBCXX_OK FALSE CACHE INTERNAL "ARM toolchain ships the C++ stdlib")
  endif()
endif()
if(NOT ACFX_ARM_CXX_LIBCXX_OK)
  message(FATAL_ERROR
    "The ARM toolchain at '${ARM_CXX}' cannot compile C++: the standard library "
    "headers (e.g. <algorithm>) are missing. Homebrew's arm-none-eabi-gcc is C-only. "
    "Install a complete ARM embedded toolchain that ships libstdc++ — e.g. the official "
    "Arm GNU Toolchain (gcc-arm-none-eabi) — put its bin/ first on PATH, and reconfigure "
    "the daisy preset (no host-side fallback).")
endif()

set(_daisy_cpu_flags "-mcpu=cortex-m7 -mthumb -mfpu=fpv5-d16 -mfloat-abi=hard")
# -ffunction-sections/-fdata-sections place each symbol in its own section so the
# linker's --gc-sections (set on each firmware target) can drop everything unused.
# libDaisy's own toolchain (stm32h750xx.cmake) sets these; omitting them here left
# the whole daisy static lib un-collectable and overflowed the 128 KB internal
# FLASH. -fomit-frame-pointer trims further. Match libDaisy.
set(_daisy_size_flags "-ffunction-sections -fdata-sections -fomit-frame-pointer")
set(CMAKE_C_FLAGS_INIT   "${_daisy_cpu_flags} ${_daisy_size_flags}")
set(CMAKE_CXX_FLAGS_INIT "${_daisy_cpu_flags} ${_daisy_size_flags} -fno-exceptions -fno-rtti")
set(CMAKE_EXE_LINKER_FLAGS_INIT "${_daisy_cpu_flags} --specs=nano.specs --specs=nosys.specs")

# STM32H750 device-selection defines. libDaisy normally injects these from its own
# toolchain (cmake/toolchains/stm32h750xx.cmake) via add_compile_definitions, but
# that toolchain is bypassed when we supply this one (libDaisy applies it only when
# CMAKE_TOOLCHAIN_FILE is unset). Without them the CMSIS/HAL headers cannot select
# the target device (`#error "Please select first the target STM32H7xx device"`)
# and uint32_t goes undeclared. Match libDaisy's stm32h750xx.cmake exactly.
add_compile_definitions(
  CORE_CM7
  STM32H750xx
  STM32H750IB
  ARM_MATH_CM7
  HSE_VALUE=16000000
)

set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
