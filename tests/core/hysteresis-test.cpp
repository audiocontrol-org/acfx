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

// T007 — the explicit RK2/RK4 steppers wired into process(). Drives a few
// cycles of a sinusoidal H and checks the integrated magnetization is well
// behaved and hysteretic. The full loop-area / cross-solver-agreement suite is
// T012/T019; this pins the steppers' core behavior only.
namespace {

// Drive `cycles` full sine cycles of amplitude `amp` through process() at
// `stepsPerCycle` samples/cycle, asserting the output is finite and bounded by
// a small multiple of Ms throughout. Returns the M sampled at the +amp*sin
// phase crossings on the rising vs falling branch of the LAST cycle, so the
// caller can assert a genuinely open loop.
struct BranchProbe {
    double risingM = 0.0;   // M near H = 0 while H increasing (rising branch)
    double fallingM = 0.0;  // M near H = 0 while H decreasing (falling branch)
    bool sawRising = false;
    bool sawFalling = false;
};

BranchProbe driveSine(Hysteresis& h, double amp, int cycles, int stepsPerCycle,
                      double Ms) {
    BranchProbe probe;
    const int total = cycles * stepsPerCycle;
    double prevH = 0.0;
    const double bound = 4.0 * Ms;  // small multiple of the saturation ceiling
    for (int n = 0; n <= total; ++n) {
        const double phase =
            2.0 * M_PI * (static_cast<double>(n) / stepsPerCycle);
        const double H = amp * std::sin(phase);
        const float out = h.process(static_cast<float>(H));
        CHECK(std::isfinite(out));
        CHECK(std::fabs(static_cast<double>(out)) <= bound);

        // Probe the last cycle: capture M as H sweeps through ~0 in each
        // direction, giving one point on the rising and one on the falling
        // branch at (nearly) the same field.
        if (n > (cycles - 1) * stepsPerCycle && std::fabs(H) < 0.05 * amp) {
            if (H > prevH) {
                probe.risingM = static_cast<double>(out);
                probe.sawRising = true;
            } else if (H < prevH) {
                probe.fallingM = static_cast<double>(out);
                probe.sawFalling = true;
            }
        }
        prevH = H;
    }
    return probe;
}

}  // namespace

TEST_CASE("Hysteresis::process — RK2/RK4 steppers integrate a loop (T007)") {
    const double Ms = 1.0;

    SUBCASE("RK2: finite, bounded, and a closed (open) loop forms") {
        Hysteresis h;
        h.prepare(48000.0);
        JAParams p;  // Ms=1, a=1, alpha=0, k=1, c=0.5
        p.k = 0.6;   // widen the loop so the branch split is unambiguous
        h.setParams(p);
        h.setSolver(Solver::rk2);
        h.reset();

        const BranchProbe probe = driveSine(h, 1.5, 4, 256, Ms);
        REQUIRE(probe.sawRising);
        REQUIRE(probe.sawFalling);
        // Rising vs falling branch differ at the same H => open hysteresis loop.
        CHECK(std::fabs(probe.risingM - probe.fallingM) > 1e-4);
    }

    SUBCASE("RK4: finite, bounded, and a closed (open) loop forms") {
        Hysteresis h;
        h.prepare(48000.0);
        JAParams p;
        p.k = 0.6;
        h.setParams(p);
        h.setSolver(Solver::rk4);
        h.reset();

        const BranchProbe probe = driveSine(h, 1.5, 4, 256, Ms);
        REQUIRE(probe.sawRising);
        REQUIRE(probe.sawFalling);
        CHECK(std::fabs(probe.risingM - probe.fallingM) > 1e-4);
    }

    SUBCASE("newtonRaphson selection is callable (routed to RK4 for T007)") {
        // T008 owns the real implicit stepper; here we only assert the case
        // compiles, runs, and stays finite (temporarily routed to RK4).
        Hysteresis h;
        h.prepare(48000.0);
        h.setParams(JAParams{});
        h.setSolver(Solver::newtonRaphson);
        h.reset();
        for (int n = 0; n < 512; ++n) {
            const double H = 1.2 * std::sin(2.0 * M_PI * n / 128.0);
            CHECK(std::isfinite(h.process(static_cast<float>(H))));
        }
    }
}

// T008 — the Newton-Raphson IMPLICIT stepper wired into process(). Drives a
// sinusoidal H and checks the implicitly-integrated magnetization is finite,
// bounded, and forms an open hysteresis loop; then cross-checks that under a
// mild drive the implicit loop stays CLOSE to the explicit RK4 loop within a
// loose tolerance. The full cross-solver agreement suite is T019; this pins the
// implicit stepper's core behavior and its rough consistency with RK4 only.
TEST_CASE("Hysteresis::process — Newton-Raphson implicit stepper (T008)") {
    const double Ms = 1.0;

    SUBCASE("newtonRaphson: finite, bounded, and an open loop forms") {
        Hysteresis h;
        h.prepare(48000.0);
        JAParams p;  // Ms=1, a=1, alpha=0, k=1, c=0.5
        p.k = 0.6;   // widen the loop so the branch split is unambiguous
        h.setParams(p);
        h.setSolver(Solver::newtonRaphson);
        h.reset();

        const BranchProbe probe = driveSine(h, 1.5, 4, 256, Ms);
        REQUIRE(probe.sawRising);
        REQUIRE(probe.sawFalling);
        // Rising vs falling branch differ at the same H => open hysteresis loop.
        CHECK(std::fabs(probe.risingM - probe.fallingM) > 1e-4);
    }

    SUBCASE("newtonRaphson stays close to RK4 under a mild drive") {
        // Same parameters + input drive through both solvers; the per-sample
        // outputs should track within a loose tolerance (both integrate the
        // same JA ODE; the implicit/explicit discretizations differ by O(step)).
        JAParams p;   // defaults; alpha=0, gentle so neither stepper is stiff
        const double amp = 0.6;      // mild drive (well below hard saturation)
        const int stepsPerCycle = 512;
        const int cycles = 3;
        const int total = cycles * stepsPerCycle;

        Hysteresis hn;
        hn.prepare(48000.0);
        hn.setParams(p);
        hn.setSolver(Solver::newtonRaphson);
        hn.reset();

        Hysteresis hr;
        hr.prepare(48000.0);
        hr.setParams(p);
        hr.setSolver(Solver::rk4);
        hr.reset();

        double maxAbsDiff = 0.0;
        for (int n = 0; n <= total; ++n) {
            const double phase =
                2.0 * M_PI * (static_cast<double>(n) / stepsPerCycle);
            const double H = amp * std::sin(phase);
            const double on = static_cast<double>(hn.process(static_cast<float>(H)));
            const double orr = static_cast<double>(hr.process(static_cast<float>(H)));
            CHECK(std::isfinite(on));
            const double diff = std::fabs(on - orr);
            if (diff > maxAbsDiff) maxAbsDiff = diff;
        }
        // Loose agreement: the implicit and explicit loops must not diverge.
        CHECK(maxAbsDiff < 5.0e-2);
    }
}
