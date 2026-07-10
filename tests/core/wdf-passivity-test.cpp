#include <doctest/doctest.h>

#include <cmath>
#include <cstddef>
#include <vector>

#include "primitives/circuit/wdf/one-port.h"
#include "primitives/circuit/wdf/wave-elements.h"
#include "primitives/circuit/wdf/wave-terminations.h"

// WDF passivity and physical-invariant suite (wdf-primitives feature, T015 —
// User Story 7; contracts/wdf-one-ports.md G3, spec FR-017/FR-021/SC-003,
// research R8/R9). Passivity is a VALIDATED property here, never an enforced
// clamp (FR-016): nothing in the leaves limits a reflection to fake passivity,
// so these tests must prove the leaves are passive on their own.
//
// The subtle part (a correction from the third-party spec review, research R8):
// there are TWO DISTINCT passivity criteria, and conflating them is a bug.
//
//   - MEMORYLESS passive leaves (adapted Resistor, ResistiveTermination) satisfy
//     the INSTANTANEOUS bound |b| <= |a| together with Rp > 0. (Being adapted,
//     reflected() is identically 0, so |b| = 0 <= |a| holds trivially — but the
//     bound is the criterion, so we assert it.)
//
//   - REACTIVE leaves (Capacitor, Inductor) DO NOT obey same-sample |b| <= |a|,
//     and asserting it would WRONGLY REJECT a correct implementation. Because
//     b[n] = a[n-1] (a unit delay), the reflected wave returns energy STORED on
//     a PREVIOUS sample: a[n-1] = 1, a[n] = 0 gives b[n] = 1 > |a[n]| = 0 for a
//     perfectly correct capacitor. The correct criterion is the WAVE-POWER
//     BALANCE across state transitions: the accumulated absorbed wave energy
//     Sigma_{k=0..N}(a[k]^2 - b[k]^2) stays >= 0 at every prefix, and for the
//     lossless capacitor it TELESCOPES to the currently-stored a[N]^2 (energy
//     stored and returned, never created or dissipated). Derivation (R8): the
//     instantaneous port power is p = (a^2 - b^2)/(4 Rp); with b[n] = a[n-1] and
//     b[0] = 0, Sigma_{k=0..N}(a[k]^2 - b[k]^2) = a[N]^2 >= 0 by telescoping.
//     The inductor is the dual: b[n] = -a[n-1] so b[n]^2 = a[n-1]^2 — the same
//     telescoping magnitude, so its balance also equals a[N]^2.

namespace {

// Memoryless passivity criterion: |b| <= |a| for every incident wave, and
// Rp > 0 throughout. For the adapted memoryless leaves reflected() is identically
// 0 (fully absorbing / matched), so |b| = 0 <= |a| — asserted directly (SC-003:
// b = 0, the leaf absorbs). Generic over Resistor / ResistiveTermination.
template <typename Leaf>
void checkMemorylessPassivity(Leaf& leaf) {
    CHECK(leaf.portResistance() > 0.0);  // G3 / FR-021: Rp > 0

    const double incidents[] = {0.0,   1.0,   -1.0,  3.7,   -12.5,
                                100.0, -1.0e6, 1.0e-9, 0.5,   -0.5};
    for (double a : incidents) {
        const double b = leaf.reflected();  // adaptable: b valid BEFORE incident (I2)
        CHECK(std::abs(b) <= std::abs(a) + 1e-15);  // the memoryless bound |b| <= |a|
        CHECK(b == doctest::Approx(0.0));           // adapted: fully absorbing (SC-003)
        leaf.incident(a);                           // no-op for these leaves
        CHECK(leaf.portResistance() > 0.0);         // Rp never clamped/altered
    }
}

// Reactive wave-power balance. Drives the incident sequence honoring the
// adaptable I2 timing (reflected() read BEFORE this sample's incident()) and
// accumulates Sigma(a[k]^2 - b[k]^2). Asserts, at EVERY prefix k:
//   (1) the running sum stays >= 0 (passivity: never creates energy), and
//   (2) the running sum EQUALS a[k]^2 (losslessness: b[k]^2 = a[k-1]^2 for both
//       the capacitor b[k]=a[k-1] and the inductor b[k]=-a[k-1], so the sum
//       telescopes exactly to the currently-stored a[k]^2).
// Returns the final accumulated balance (== a[N]^2 for a lossless reactive leaf).
template <typename Leaf>
double driveWavePowerBalance(Leaf& leaf, const std::vector<double>& seq) {
    constexpr double kTol = 1e-9;  // FP accumulation slack on the >= 0 guard
    double running = 0.0;
    for (std::size_t k = 0; k < seq.size(); ++k) {
        const double b = leaf.reflected();   // b[k]: stored state, read before incident (I2)
        const double a = seq[k];
        running += a * a - b * b;            // accumulate Sigma(a^2 - b^2)
        CHECK(running >= -kTol);                       // (1) passivity at this prefix
        CHECK(running == doctest::Approx(a * a));       // (2) telescopes to a[k]^2 (lossless)
        leaf.incident(a);                    // down-sweep: state := a[k]
    }
    return running;
}

}  // namespace

// ---------------------------------------------------------------------------
// Memoryless passivity: |b| <= |a| and Rp > 0 (adapted Resistor, Termination).
// ---------------------------------------------------------------------------

TEST_CASE("Memoryless passivity: adapted Resistor satisfies |b| <= |a| and Rp > 0") {
    acfx::wdf::Resistor r(4700.0);
    checkMemorylessPassivity(r);
}

TEST_CASE("Memoryless passivity: ResistiveTermination satisfies |b| <= |a| and Rp > 0") {
    acfx::wdf::ResistiveTermination term(50.0);
    checkMemorylessPassivity(term);
}

// ---------------------------------------------------------------------------
// Reactive wave-power balance: Sigma(a^2 - b^2) >= 0 at every prefix, and
// telescopes to the stored a[N]^2 for the lossless capacitor/inductor (R8).
// ---------------------------------------------------------------------------

TEST_CASE("Reactive wave-power balance: Capacitor Sigma(a^2 - b^2) >= 0 and telescopes to a[N]^2") {
    acfx::wdf::Capacitor cap(1.0e-6, 1.0 / 48000.0);

    const std::vector<double> seq = {0.7, 1.0, -2.5, 0.0, 7.25, -3.0, 4.0, -1.5};
    const double balance = driveWavePowerBalance(cap, seq);

    // Lossless: the accumulated absorbed wave energy equals exactly the energy
    // still stored in the port at the end — a[N]^2, with a[N] the last incident.
    const double aN = seq.back();
    CHECK(balance == doctest::Approx(aN * aN));  // energy stored and returned, none created
    CHECK(balance >= 0.0);                       // passivity over the whole sequence
}

TEST_CASE("Reactive wave-power balance: Inductor Sigma(a^2 - b^2) >= 0 and telescopes to a[N]^2") {
    acfx::wdf::Inductor ind(1.0e-3, 1.0 / 48000.0);

    // Same driven sequence: b[n] = -a[n-1] so b[n]^2 = a[n-1]^2 — the balance
    // telescopes to the identical a[N]^2 magnitude as the capacitor (duality G4).
    const std::vector<double> seq = {0.7, 1.0, -2.5, 0.0, 7.25, -3.0, 4.0, -1.5};
    const double balance = driveWavePowerBalance(ind, seq);

    const double aN = seq.back();
    CHECK(balance == doctest::Approx(aN * aN));
    CHECK(balance >= 0.0);
}

TEST_CASE("Reactive wave-power balance: ends at 0 when the port is left de-energized (a[N] = 0)") {
    // A sequence whose last incident is 0 leaves no stored energy, so the whole
    // balance telescopes to a[N]^2 = 0 — every joule that went in came back out.
    acfx::wdf::Capacitor cap(2.2e-9, 1.0 / 96000.0);
    const std::vector<double> seq = {3.0, -1.0, 2.0, 5.0, -4.0, 0.0};
    const double balance = driveWavePowerBalance(cap, seq);
    CHECK(balance == doctest::Approx(0.0));
    CHECK(balance >= 0.0);
}

// ---------------------------------------------------------------------------
// EXPLICIT NEGATIVE GUARD (why the wave-power balance is the CORRECT criterion).
//
// This case documents the exact scenario that would break a naive same-sample
// |b| <= |a| criterion applied to a reactive leaf. A perfectly CORRECT capacitor
// VIOLATES that bound: driving a[n-1] = 1 then a[n] = 0, the unit delay returns
// b[n] = a[n-1] = 1 while |a[n]| = 0, so |b[n]| = 1 > 0 = |a[n]|. The stored
// energy from the previous sample is simply coming back out. A same-sample
// criterion would WRONGLY REJECT this correct, lossless leaf (research R8's
// blocking review finding) — which is precisely why reactive leaves are
// validated by the wave-power balance Sigma(a^2 - b^2) >= 0 (energy stored on
// one sample, returned on the next), NEVER by same-sample |b| <= |a|.
// ---------------------------------------------------------------------------

TEST_CASE("Reactive negative guard: correct Capacitor VIOLATES same-sample |b| <= |a| (b[n]=1 > |a[n]|=0)") {
    acfx::wdf::Capacitor cap(1.0e-6, 1.0 / 48000.0);

    // sample n-1: a = 1 stores energy in the port.
    CHECK(cap.reflected() == doctest::Approx(0.0));  // b[0] = 0 (empty state)
    cap.incident(1.0);                                // state := 1

    // sample n: a = 0, yet the previously stored wave comes back out.
    const double b_n = cap.reflected();  // b[n] = a[n-1] = 1
    const double a_n = 0.0;
    CHECK(b_n == doctest::Approx(1.0));

    // THE POINT: the same-sample bound |b| <= |a| is FALSE here (1 > 0) for a
    // perfectly correct capacitor — so it is NOT a valid passivity criterion for
    // a reactive leaf. Asserting the violation makes the requirement explicit.
    CHECK(std::abs(b_n) > std::abs(a_n));  // 1 > 0: same-sample bound does NOT hold

    // The CORRECT criterion, the wave-power balance, IS satisfied and telescopes
    // to a[N]^2: Sigma = (1^2 - 0^2) + (0^2 - 1^2) = 0 = a[N]^2 (a[N] = 0).
    const double prefix0 = (1.0 * 1.0) - (0.0 * 0.0);   // after n-1: 1 == a[0]^2, >= 0
    CHECK(prefix0 == doctest::Approx(1.0));
    CHECK(prefix0 >= 0.0);
    const double prefix1 = prefix0 + (a_n * a_n - b_n * b_n);  // after n: 0 == a[1]^2
    CHECK(prefix1 == doctest::Approx(0.0));  // = a[N]^2, energy fully returned
    CHECK(prefix1 >= 0.0);                    // passivity holds by the correct criterion
}
