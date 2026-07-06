// diode-clippers-harness.cpp
// Host-only lab harness: never included by portable code.
//
// Setup-phase stub (T002). The real PASS/FAIL validations — linear RC
// recurrence, DC-limit bisection oracle, symmetry/saturation/passivity, the
// reactive signature, and the explicit non-convergence check — are filled in
// by T019 (US3). Exits 0 so the target links until then.
//
// Include root: core/ (see the acfx_lab_diode_clippers_harness CMake target).
// Compile with -std=c++20.

int main() {
    return 0;
}
