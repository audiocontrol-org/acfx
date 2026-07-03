#include <doctest/doctest.h>

#include <cmath>

#include "primitives/nonlinear/hysteresis.h"

using namespace acfx;

// Test accessor for the private, RT-internal JA derivative dMdH() (T006 seam).
// Declared as a friend on acfx::Hysteresis; owns no state of its own.
namespace acfx {
struct HysteresisTestAccess {
    [[nodiscard]] static double dMdH(const Hysteresis& h, double H, double M,
                                     double dH) noexcept {
        return h.dMdH(H, M, dH);
    }
};
}  // namespace acfx

TEST_CASE("Hysteresis primitive stub (T012)") {
    // TODO(T012): Implement Hysteresis primitive tests: Jiles-Atherton ODE
    // integrator, solver selection (rk2/rk4/newtonRaphson), state management,
    // reset invariants, and RT-safety (no allocation on audio path).
    CHECK(true);
}

// T006 — focused sanity checks on the shared Jiles-Atherton derivative dMdH.
// The full loop-area / solver-agreement suite is T012's; this only pins the
// derivative's local sanity (finite, sane origin slope, no NaN/Inf when swept).
TEST_CASE("Hysteresis::dMdH — Jiles-Atherton derivative sanity (T006)") {
    Hysteresis h;
    JAParams p;  // defaults: Ms=1, a=1, alpha=0, k=1, c=0.5
    h.setParams(p);

    SUBCASE("origin slope ~ anhysteretic Ms/(3a) with alpha=0") {
        // At H=0, M=0: H_e=0, dMan/dHe = Ms/a * 1/3. With alpha=0 the combined
        // form collapses to (1-c)*dMirr/dH + c*dMan/dHe; at the origin
        // dMirr/dH = 0 (M_an - M = 0), so dM/dH = c * Ms/(3a).
        const double d = HysteresisTestAccess::dMdH(h, 0.0, 0.0, 1.0);
        CHECK(std::isfinite(d));
        const double expected = 0.5 * (1.0 / (3.0 * 1.0));  // c * Ms/(3a)
        CHECK(d == doctest::Approx(expected).epsilon(1e-9));
    }

    SUBCASE("finite across a swept H for both dH signs, no NaN/Inf") {
        for (double H = -50.0; H <= 50.0; H += 0.25) {
            for (double M = -1.5; M <= 1.5; M += 0.5) {
                const double dpos = HysteresisTestAccess::dMdH(h, H, M, 1.0);
                const double dneg = HysteresisTestAccess::dMdH(h, H, M, -1.0);
                const double dzero = HysteresisTestAccess::dMdH(h, H, M, 0.0);
                CHECK(std::isfinite(dpos));
                CHECK(std::isfinite(dneg));
                CHECK(std::isfinite(dzero));
            }
        }
    }

    SUBCASE("stays finite under nonzero coupling and hot field") {
        h.setAlpha(1.0e-3);
        h.setK(0.5);
        h.setMs(2.0);
        h.setA(0.5);
        for (double H = -1000.0; H <= 1000.0; H += 5.0) {
            const double d = HysteresisTestAccess::dMdH(h, H, 0.3, 1.0);
            CHECK(std::isfinite(d));
        }
    }
}
