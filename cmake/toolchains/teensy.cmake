# Toolchain — Teensy 4.x (IMXRT1062, Cortex-M7) via the ARM embedded toolchain.
#
# Teensy 4.x is a Cortex-M7 with a double-precision FPU. The C++ standard this
# target builds at is verified against the installed Teensy toolchain during
# implementation (research.md decision 3 / tasks.md T034); ACFX_TEENSY_CXX_STANDARD
# is set to the highest standard that toolchain supports (>= 17). The same
# core/effects/svf source compiles here; where C++20 concepts are unavailable the
# Effect contract degrades to a duck-typed template (guarded by __cpp_concepts).

set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR arm)
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

find_program(ARM_CC  arm-none-eabi-gcc)
find_program(ARM_CXX arm-none-eabi-g++)
if(NOT ARM_CC OR NOT ARM_CXX)
  message(FATAL_ERROR
    "arm-none-eabi-gcc / arm-none-eabi-g++ not found on PATH. Install the Teensy "
    "ARM toolchain to build the teensy preset (no host-side fallback).")
endif()

set(CMAKE_C_COMPILER   "${ARM_CC}")
set(CMAKE_CXX_COMPILER "${ARM_CXX}")
set(CMAKE_ASM_COMPILER "${ARM_CC}")

# Verified during implementation (T034); default to C++17, the level the Teensy
# core is known to support. Raised here if the installed toolchain supports more.
if(NOT DEFINED ACFX_TEENSY_CXX_STANDARD)
  set(ACFX_TEENSY_CXX_STANDARD 17)
endif()
set(CMAKE_CXX_STANDARD ${ACFX_TEENSY_CXX_STANDARD})

set(_teensy_cpu_flags "-mcpu=cortex-m7 -mthumb -mfpu=fpv5-d16 -mfloat-abi=hard -DF_CPU=600000000 -D__IMXRT1062__")
set(CMAKE_C_FLAGS_INIT   "${_teensy_cpu_flags}")
set(CMAKE_CXX_FLAGS_INIT "${_teensy_cpu_flags} -fno-exceptions -fno-rtti")
set(CMAKE_EXE_LINKER_FLAGS_INIT "${_teensy_cpu_flags} --specs=nano.specs --specs=nosys.specs")

set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
