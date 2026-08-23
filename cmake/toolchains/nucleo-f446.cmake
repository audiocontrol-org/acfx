# Toolchain — NUCLEO-F446RE (STM32F446RE, Cortex-M4F) via arm-none-eabi-gcc.
#
# The NUCLEO-F446RE's SoC is an STM32F446RE (Cortex-M4 w/ single-precision FPU,
# fpv4-sp-d16). These flags match what a bare-metal STM32F446 firmware image
# requires. The core/ sources are platform-independent; this file only describes
# how the cross-compiler is invoked for the nucleo adapter target.

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
    "embedded toolchain to build the nucleo preset (no host-side fallback).")
endif()

set(CMAKE_C_COMPILER   "${ARM_CC}")
set(CMAKE_CXX_COMPILER "${ARM_CXX}")
set(CMAKE_ASM_COMPILER "${ARM_CC}")

# The found toolchain must ship the C++ standard library. Homebrew's
# arm-none-eabi-gcc is C-only (no libstdc++/newlib C++ headers), so a build would
# otherwise fail deep inside the effect sources with a confusing "<algorithm>: No
# such file" error. Probe once and fail loud + actionable here instead (Constitution
# V: no silent/host-side fallback). Set ACFX_ARM_CXX_LIBCXX_OK to skip if you know
# better.
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
    "the nucleo preset (no host-side fallback).")
endif()

set(_nucleo_cpu_flags "-mcpu=cortex-m4 -mthumb -mfpu=fpv4-sp-d16 -mfloat-abi=hard")
# -ffunction-sections/-fdata-sections place each symbol in its own section so the
# linker's --gc-sections (set on each firmware target) has something to discard;
# without them --gc-sections has no per-symbol boundaries to work with and is
# largely inert.
set(_nucleo_size_flags "-ffunction-sections -fdata-sections")
set(CMAKE_C_FLAGS_INIT   "${_nucleo_cpu_flags} ${_nucleo_size_flags}")
set(CMAKE_CXX_FLAGS_INIT "${_nucleo_cpu_flags} ${_nucleo_size_flags} -fno-exceptions -fno-rtti")
set(CMAKE_EXE_LINKER_FLAGS_INIT "${_nucleo_cpu_flags} --specs=nano.specs --specs=nosys.specs")

set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
