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

# The found toolchain must ship the C++ standard library. Homebrew's
# arm-none-eabi-gcc is C-only (no libstdc++ headers); a build would otherwise fail
# deep in a TU with a confusing "<algorithm>: No such file" error. Probe once and
# fail loud + actionable here (Constitution V: no silent/host-side fallback). Set
# ACFX_ARM_CXX_LIBCXX_OK to skip if you know better.
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
    "Install a complete ARM embedded toolchain that ships libstdc++ (the Teensy/Arm GNU "
    "Toolchain), put its bin/ first on PATH, and reconfigure the teensy preset (no "
    "host-side fallback).")
endif()

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
