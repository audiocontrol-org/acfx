// program-dependent-saturation-harness.cpp
// Host-only harness stub for program-dependent saturation measurement evidence.
// Minimal placeholder pending real measurement infrastructure (task T039).
//
// Include roots: core/ and tests/ (see the acfx_lab_program_dependent_saturation_harness
// CMake target). Compile with -std=c++20. May allocate, loop, printf. Never
// included by portable code (C-1 gate in scripts/check-portability.sh).

#include <cstdio>

int main() {
    std::printf("program-dependent-saturation harness — measurement evidence pending (T039)\n");
    return 0;
}
