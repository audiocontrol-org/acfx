#include <doctest/doctest.h>

#include <complex>
#include <stdexcept>
#include <vector>

#include "primitives/circuit/wdf/one-port.h"
#include "primitives/circuit/wdf/wave-elements.h"

// WDF reactive element (capacitor/inductor) suite (wdf-primitives feature,
// T007/T008 — User Story 2, the WDF-defining reactive-as-unit-delay behavior).
//
// Covers the bilinear-discretized capacitor and inductor one-ports
// (contracts/wdf-one-ports.md "Capacitor" C1-C2, "Inductor" L1-L2, G4 duality;
// data-model.md "Capacitor"/"Inductor"; research R2 bilinear derivation):
//   - port resistance Rp computed once (cap T/(2C), ind 2L/T),
//   - reflected wave as a UNIT DELAY of the incident wave (cap b[n]=a[n-1],
//     ind b[n]=-a[n-1]; b[0]=0 before any incident),
//   - duality (Rp and reflected-sign swapped under one dt),
//   - bilinear port-impedance agreement (Z(z)=Rp(1+S)/(1-S) from the leaf's
//     measured unit-delay reflectance equals the bilinear-discretized analog
//     impedance, derived from first principles — NO transcribed numbers),
//   - no-fallback parameter validation (C/L/dt <= 0 throw at construction),
//   - the OnePort concept trait.
//
// T008 (core/primitives/circuit/wdf/wave-elements.h, namespace acfx::wdf)
// MUST define `Capacitor` and `Inductor`:
//
//   Capacitor(double C, double dt);
//   double portResistance() const noexcept;   // T/(2C), T = dt
//   double reflected()      const noexcept;    // state (= a[n-1]); 0 initially
//   void   incident(double a) noexcept;        // state := a
//   static constexpr bool isAdaptable = true;
//
//   Inductor(double L, double dt);
//   double portResistance() const noexcept;   // 2L/T
//   double reflected()      const noexcept;    // -state (= -a[n-1])
//   void   incident(double a) noexcept;        // state := a
//   static constexpr bool isAdaptable = true;
//
// This file intentionally does NOT compile/pass until T008 lands -- the
// expected RED state for T007.

namespace {

using Complex = std::complex<double>;

// Measure a leaf's discrete reflectance S(e^{jwT}) = b/a directly from its
// impulse response, then form the port impedance Z(z) = Rp*(1+S)/(1-S).
//
// Principled + first-principles: we drive the ACTUAL leaf with a Kronecker
// impulse (a[0]=1, a[n>0]=0) honoring the adaptable call order (reflected()
// BEFORE this sample's incident()), collect the reflected impulse response
// b[n], and take its DTFT at the test frequency. No z^-1 is hard-coded -- the
// unit delay falls out of the measured b[n]. For a correct capacitor the
// impulse response is [0, 1, 0, 0, ...] so S -> e^{-jwT}; for the inductor
// [0, -1, 0, 0, ...] so S -> -e^{-jwT}.
template <typename Leaf>
Complex measuredPortImpedance(Leaf& leaf, double omegaT, int nSamples) {
    const double Rp = leaf.portResistance();

    Complex S(0.0, 0.0);
    for (int n = 0; n < nSamples; ++n) {
        const double b = leaf.reflected();          // b[n] (up-sweep, state)
        const double a = (n == 0) ? 1.0 : 0.0;      // Kronecker impulse
        leaf.incident(a);                           // down-sweep: state := a
        // DTFT accumulation: S(e^{jwT}) = sum_n b[n] * e^{-j w T n}.
        S += b * std::exp(Complex(0.0, -omegaT * static_cast<double>(n)));
    }
    return Rp * (Complex(1.0, 0.0) + S) / (Complex(1.0, 0.0) - S);
}

// bilinear operator s(z) = (2/T)*(1 - z^-1)/(1 + z^-1) at z = e^{jwT}.
Complex bilinearS(double omegaT, double T) {
    const Complex zInv = std::exp(Complex(0.0, -omegaT));  // z^-1 = e^{-jwT}
    return (2.0 / T) * (Complex(1.0, 0.0) - zInv) / (Complex(1.0, 0.0) + zInv);
}

void checkComplexApprox(const Complex& got, const Complex& want) {
    CHECK(got.real() == doctest::Approx(want.real()));
    CHECK(got.imag() == doctest::Approx(want.imag()));
}

}  // namespace

static_assert(acfx::wdf::is_one_port_v<acfx::wdf::Capacitor>,
              "Capacitor must satisfy the OnePort concept trait");
static_assert(acfx::wdf::is_one_port_v<acfx::wdf::Inductor>,
              "Inductor must satisfy the OnePort concept trait");

// ---------------------------------------------------------------------------
// Port resistance (Rp computed once from the bilinear discretization).
// ---------------------------------------------------------------------------

TEST_CASE("Capacitor portResistance() == T/(2C)") {
    const double C = 1.0e-6;
    const double dt = 1.0 / 48000.0;
    const acfx::wdf::Capacitor cap(C, dt);
    CHECK(cap.portResistance() == doctest::Approx(dt / (2.0 * C)));
}

TEST_CASE("Inductor portResistance() == 2L/T") {
    const double L = 1.0e-3;
    const double dt = 1.0 / 48000.0;
    const acfx::wdf::Inductor ind(L, dt);
    CHECK(ind.portResistance() == doctest::Approx(2.0 * L / dt));
}

// ---------------------------------------------------------------------------
// Unit delay: reflected() THIS sample == the PREVIOUS sample's incident wave.
// ---------------------------------------------------------------------------

TEST_CASE("Capacitor reflected() is a unit delay of incident: b[n] = a[n-1], b[0] = 0") {
    acfx::wdf::Capacitor cap(1.0e-6, 1.0 / 48000.0);

    // Before any incident() the stored state is 0 -> b[0] = 0.
    CHECK(cap.reflected() == doctest::Approx(0.0));

    const double seq[] = {1.0, -2.5, 0.0, 7.25, -3.0, 4.0};
    double prev = 0.0;  // a[-1] treated as 0 for the first assertion
    for (double a : seq) {
        // Adaptable ordering: reflected() (b[n]) is valid BEFORE incident(a[n]).
        CHECK(cap.reflected() == doctest::Approx(prev));  // b[n] == a[n-1]
        cap.incident(a);                                  // state := a[n]
        prev = a;
    }
    // One more reflected() after the loop returns the last incident.
    CHECK(cap.reflected() == doctest::Approx(seq[5]));
}

TEST_CASE("Inductor reflected() is a sign-inverted unit delay: b[n] = -a[n-1], b[0] = 0") {
    acfx::wdf::Inductor ind(1.0e-3, 1.0 / 48000.0);

    CHECK(ind.reflected() == doctest::Approx(0.0));  // -0 initially

    const double seq[] = {1.0, -2.5, 0.0, 7.25, -3.0, 4.0};
    double prev = 0.0;
    for (double a : seq) {
        CHECK(ind.reflected() == doctest::Approx(-prev));  // b[n] == -a[n-1]
        ind.incident(a);
        prev = a;
    }
    CHECK(ind.reflected() == doctest::Approx(-seq[5]));
}

// ---------------------------------------------------------------------------
// G4 duality: cap Rp=T/2C & reflected=+state vs ind Rp=2L/T & reflected=-state.
// ---------------------------------------------------------------------------

TEST_CASE("Duality (G4): capacitor and inductor are duals under a single dt") {
    const double dt = 1.0 / 44100.0;
    const double C = 4.7e-6;
    const double L = 2.2e-3;

    acfx::wdf::Capacitor cap(C, dt);
    acfx::wdf::Inductor ind(L, dt);

    // Port resistances are the dual forms (T/2C vs 2L/T).
    CHECK(cap.portResistance() == doctest::Approx(dt / (2.0 * C)));
    CHECK(ind.portResistance() == doctest::Approx(2.0 * L / dt));

    // Reflected-wave signs are dual: for the SAME incident history the
    // capacitor reflects +state and the inductor reflects -state.
    const double a = 3.5;
    cap.incident(a);
    ind.incident(a);
    CHECK(cap.reflected() == doctest::Approx(+a));
    CHECK(ind.reflected() == doctest::Approx(-a));
    // Exact sign duality (not merely Approx): cap == -ind for equal history.
    CHECK(cap.reflected() == doctest::Approx(-ind.reflected()));
}

// ---------------------------------------------------------------------------
// Bilinear port-impedance agreement (derived from first principles).
//   Capacitor: Z_a(s) = 1/(sC)  -> bilinear Z(z) = (T/2C)(1+z^-1)/(1-z^-1).
//   Inductor:  Z_a(s) = sL      -> bilinear Z(z) = (2L/T)(1-z^-1)/(1+z^-1).
// The leaf's port impedance is reconstructed from its MEASURED unit-delay
// reflectance S(z) via Z = Rp(1+S)/(1-S); it must equal the bilinear analog
// impedance at each test z = e^{jwT}.
// ---------------------------------------------------------------------------

TEST_CASE("Capacitor port impedance matches the bilinear-discretized 1/(sC) at z=e^{jwT}") {
    const double C = 1.0e-6;
    const double dt = 1.0 / 48000.0;

    for (double omegaT : {0.25, 0.8, 1.7}) {
        acfx::wdf::Capacitor cap(C, dt);
        const Complex zLeaf = measuredPortImpedance(cap, omegaT, 64);

        // Analog capacitor impedance 1/(sC), s from bilinear.
        const Complex s = bilinearS(omegaT, dt);
        const Complex zAnalog = Complex(1.0, 0.0) / (s * C);

        checkComplexApprox(zLeaf, zAnalog);
    }
}

TEST_CASE("Inductor port impedance matches the bilinear-discretized sL at z=e^{jwT}") {
    const double L = 1.0e-3;
    const double dt = 1.0 / 48000.0;

    for (double omegaT : {0.25, 0.8, 1.7}) {
        acfx::wdf::Inductor ind(L, dt);
        const Complex zLeaf = measuredPortImpedance(ind, omegaT, 64);

        // Analog inductor impedance sL, s from bilinear.
        const Complex s = bilinearS(omegaT, dt);
        const Complex zAnalog = s * L;

        checkComplexApprox(zLeaf, zAnalog);
    }
}

// ---------------------------------------------------------------------------
// No-fallback parameter validation: non-physical params throw, never clamp.
// ---------------------------------------------------------------------------

TEST_CASE("Capacitor throws std::invalid_argument for non-positive C or dt") {
    const double dt = 1.0 / 48000.0;
    const double C = 1.0e-6;
    CHECK_THROWS_AS(acfx::wdf::Capacitor(0.0, dt), std::invalid_argument);
    CHECK_THROWS_AS(acfx::wdf::Capacitor(-1.0e-6, dt), std::invalid_argument);
    CHECK_THROWS_AS(acfx::wdf::Capacitor(C, 0.0), std::invalid_argument);
    CHECK_THROWS_AS(acfx::wdf::Capacitor(C, -dt), std::invalid_argument);
}

TEST_CASE("Inductor throws std::invalid_argument for non-positive L or dt") {
    const double dt = 1.0 / 48000.0;
    const double L = 1.0e-3;
    CHECK_THROWS_AS(acfx::wdf::Inductor(0.0, dt), std::invalid_argument);
    CHECK_THROWS_AS(acfx::wdf::Inductor(-1.0e-3, dt), std::invalid_argument);
    CHECK_THROWS_AS(acfx::wdf::Inductor(L, 0.0), std::invalid_argument);
    CHECK_THROWS_AS(acfx::wdf::Inductor(L, -dt), std::invalid_argument);
}

TEST_CASE("Capacitor and Inductor are OnePorts (static_assert already checked at file scope)") {
    CHECK(acfx::wdf::is_one_port_v<acfx::wdf::Capacitor>);
    CHECK(acfx::wdf::is_one_port_v<acfx::wdf::Inductor>);
}
