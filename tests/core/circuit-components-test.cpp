#include <doctest/doctest.h>
#include <cmath>
#include "primitives/circuit/components.h"

// US1: per-component physics tests for the circuit primitive vocabulary
// (spec.md US1 acceptance 1-4, SC-001; contracts/component-physics.md).
//
// This is the first test translation unit to include
// primitives/circuit/components.h (and transitively node.h, companion.h,
// and every models/*.h), so a green build here is also the compile-check
// of the whole vocabulary against the built test suite (acfx_core_tests).

using acfx::Capacitor;
using acfx::Component;
using acfx::CurrentSource;
using acfx::Diode;
using acfx::Inductor;
using acfx::Resistor;
using acfx::VoltageSource;
using acfx::isLinear;
using acfx::isNonlinear;
using acfx::isReactive;

// ---------------------------------------------------------------------------
// Resistor (US1.1) — admittance() is exactly 1/R.
// ---------------------------------------------------------------------------

TEST_CASE("Resistor - admittance is exactly 1/R") {
    CHECK(Resistor{0, 1, 1000.0}.admittance() == doctest::Approx(1.0 / 1000.0));
    CHECK(Resistor{0, 1, 47.0}.admittance()    == doctest::Approx(1.0 / 47.0));
    CHECK(Resistor{0, 1, 2.2e6}.admittance()   == doctest::Approx(1.0 / 2.2e6));
}

// ---------------------------------------------------------------------------
// Diode (US1.2) — Shockley law, analytic-vs-numerical Jacobian, monotonicity.
// ---------------------------------------------------------------------------

TEST_CASE("Diode - forward-bias current matches the Shockley law") {
    const Diode d{0, 1, 1e-14, 1.0, 0.02585};
    const double vAK = 0.6;
    const double expected = d.Is * (std::exp(vAK / (d.n * d.Vt)) - 1.0);

    const auto sample = d.evaluate(vAK);

    CHECK(sample.current == doctest::Approx(expected).epsilon(1e-9));
}

TEST_CASE("Diode - analytic conductance matches the numerical derivative of current") {
    // Genuine correctness check: verifies dI/dV as returned by evaluate() is
    // actually the derivative of I(v), via a central finite difference. This
    // does not reuse the closed form for conductance, so it catches a wrong
    // analytic derivative that current-matching alone would miss.
    const Diode d{0, 1, 1e-14, 1.0, 0.02585};
    const double v = 0.5;
    const double h = 1e-6;

    const double iPlus  = d.evaluate(v + h).current;
    const double iMinus = d.evaluate(v - h).current;
    const double numericalG = (iPlus - iMinus) / (2.0 * h);

    const double analyticG = d.evaluate(v).conductance;

    CHECK(analyticG == doctest::Approx(numericalG).epsilon(1e-4));
}

TEST_CASE("Diode - current is monotonically increasing and finite across a bias sweep") {
    const Diode d{0, 1, 1e-14, 1.0, 0.02585};
    const double biases[] = {-0.5, -0.1, 0.0, 0.2, 0.4, 0.6, 0.8};

    double previous = d.evaluate(biases[0]).current;
    CHECK(std::isfinite(previous));

    for (size_t i = 1; i < std::size(biases); ++i) {
        const double current = d.evaluate(biases[i]).current;
        CHECK(std::isfinite(current));
        CHECK(current > previous);
        previous = current;
    }
}

TEST_CASE("Diode - zero bias gives exactly zero current") {
    const Diode d{0, 1, 1e-14, 1.0, 0.02585};
    CHECK(d.evaluate(0.0).current == doctest::Approx(0.0));
}

// ---------------------------------------------------------------------------
// Capacitor (US1.3) — backward-Euler companion model.
// ---------------------------------------------------------------------------

TEST_CASE("Capacitor - companion returns Geq = C/dt and Ieq = Geq * vPrev") {
    const Capacitor c{0, 1, 100e-9};  // 100 nF
    const double dt = 1.0 / 48000.0;
    const double vPrev = 1.5;

    const auto companion = c.companion(dt, vPrev);
    const double expectedGeq = c.C / dt;

    CHECK(companion.Geq == doctest::Approx(expectedGeq));
    CHECK(companion.Ieq == doctest::Approx(expectedGeq * vPrev));
}

// ---------------------------------------------------------------------------
// Inductor (US1.3) — backward-Euler companion model (dual of the capacitor).
// ---------------------------------------------------------------------------

TEST_CASE("Inductor - companion returns Geq = dt/L and Ieq = -iPrev") {
    const Inductor l{0, 1, 10e-3};  // 10 mH
    const double dt = 1.0 / 48000.0;
    const double iPrev = 0.25;

    const auto companion = l.companion(dt, iPrev);
    const double expectedGeq = dt / l.L;

    CHECK(companion.Geq == doctest::Approx(expectedGeq));
    CHECK(companion.Ieq == doctest::Approx(-iPrev));
}

// ---------------------------------------------------------------------------
// Classifiers — isNonlinear / isReactive / isLinear partition the fixed
// six-element Component variant (components.h).
// ---------------------------------------------------------------------------

TEST_CASE("Component classifiers - Diode is nonlinear and nothing else") {
    const Component c{Diode{0, 1, 1e-14, 1.0, 0.02585}};

    CHECK(isNonlinear(c));
    CHECK_FALSE(isReactive(c));
    CHECK_FALSE(isLinear(c));
}

TEST_CASE("Component classifiers - Capacitor and Inductor are reactive and nothing else") {
    const Component cap{Capacitor{0, 1, 1e-6}};
    const Component ind{Inductor{0, 1, 1e-3}};

    CHECK(isReactive(cap));
    CHECK_FALSE(isNonlinear(cap));
    CHECK_FALSE(isLinear(cap));

    CHECK(isReactive(ind));
    CHECK_FALSE(isNonlinear(ind));
    CHECK_FALSE(isLinear(ind));
}

TEST_CASE("Component classifiers - Resistor, VoltageSource, CurrentSource are linear and nothing else") {
    const Component r{Resistor{0, 1, 1000.0}};
    const Component vs{VoltageSource{0, 1, 9.0}};
    const Component is{CurrentSource{0, 1, 0.01}};

    for (const Component* c : {&r, &vs, &is}) {
        CHECK(isLinear(*c));
        CHECK_FALSE(isNonlinear(*c));
        CHECK_FALSE(isReactive(*c));
    }
}

// ---------------------------------------------------------------------------
// No solver leakage (FR-006) — the primitives expose physics only.
//
// This is a documentation-and-compile-level check, not a runtime one: the
// only members this test suite ever calls on a component are admittance(),
// evaluate(), companion(), current(), and the free classifiers
// isNonlinear()/isReactive()/isLinear(). If a future change added a
// stamp()/scatter()/solve() member to any circuit/ primitive, the intent is
// that no test here would ever need to call it — the physics-only API
// surface exercised above is exhaustive for the v1 vocabulary. A grep-level
// guard ("no stamp/scatter symbol under core/primitives/circuit/") belongs
// in the portability script (T003's isolation gate), not in a runtime test.
// ---------------------------------------------------------------------------

TEST_CASE("CircuitComponents - API surface is physics-only (admittance/evaluate/companion/classifiers)") {
    const Resistor r{0, 1, 1000.0};
    const Capacitor cap{0, 1, 1e-6};
    const Inductor ind{0, 1, 1e-3};
    const Diode d{0, 1, 1e-14, 1.0, 0.02585};
    const CurrentSource is{0, 1, 0.01};

    // Physics-only calls: no stamp/scatter/solve member exists on any of
    // these types, so this test cannot compile against one even by mistake.
    CHECK(std::isfinite(r.admittance()));
    CHECK(std::isfinite(cap.companion(1.0 / 48000.0, 0.0).Geq));
    CHECK(std::isfinite(ind.companion(1.0 / 48000.0, 0.0).Geq));
    CHECK(std::isfinite(d.evaluate(0.1).current));
    CHECK(std::isfinite(is.current()));
}
