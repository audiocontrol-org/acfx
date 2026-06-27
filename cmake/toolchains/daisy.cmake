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

set(_daisy_cpu_flags "-mcpu=cortex-m7 -mthumb -mfpu=fpv5-d16 -mfloat-abi=hard")
set(CMAKE_C_FLAGS_INIT   "${_daisy_cpu_flags}")
set(CMAKE_CXX_FLAGS_INIT "${_daisy_cpu_flags} -fno-exceptions -fno-rtti")
set(CMAKE_EXE_LINKER_FLAGS_INIT "${_daisy_cpu_flags} --specs=nano.specs --specs=nosys.specs")

set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
