#include <doctest/doctest.h>

#include <cmath>
#include <limits>

#include "primitives/nonlinear/hysteresis.h"
#include "core/hysteresis-test-support.h"

using namespace acfx;
using namespace acfx::hysteresistest;

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

// T009 — the stiff-solver stability guard wired into process() (FR-006,
// contract C3). Two focused properties, for ALL THREE solvers: (1) a hot
// transient under deliberately stiff params can never produce a non-finite
// or unbounded output, and the primitive recovers to normal bounded output
// once the transient passes; (2) a non-finite input can never propagate
// NaN/Inf into the output or poison subsequent state. The full SC-005 sweep
// is a later task; this only pins the guard itself.
namespace {

void checkHotTransientRecovers(Solver solver) {
    Hysteresis h;
    h.prepare(48000.0);
    // Physically well-behaved params (same shape T007/T008 already prove is
    // stable/bounded under normal drive) — the stress here comes from the
    // TRANSIENT (an enormous single-sample |dH|), not from pathological
    // physics, so "recovers afterward" is a meaningful assertion rather than
    // an artifact of params that never settle.
    JAParams p;
    p.Ms = 1.0;
    p.a = 1.0;
    p.alpha = 0.0;
    p.k = 0.6;   // widen the loop, as T007/T008 do
    p.c = 0.5;
    h.setParams(p);
    h.setSolver(solver);
    h.reset();

    const double bound = kGuardBoundMultiplier * p.Ms;

    // Adversarial hot transient: enormous alternating steps (|dH| ~ 1e5-2e5)
    // stress every solver's stiff-transient handling — a step this large
    // drives the explicit RK stages to overshoot the physical M range by
    // orders of magnitude before the JA sign-correction can rein it in,
    // exactly the regime the T009 guard exists for.
    const double transientSteps[] = {1.0e5, -1.0e5, 1.5e5, -2.0e5, 5.0e4};
    for (double H : transientSteps) {
        const float out = h.process(static_cast<float>(H));
        CHECK(std::isfinite(out));
        CHECK(std::fabs(static_cast<double>(out)) <= bound + 1e-6);
    }

    // Recovery: resume a normal, gentle drive and confirm the primitive
    // settles back into the physically-bounded (~Ms) operating range rather
    // than staying pinned at the guard's clamp bound.
    double lastOut = 0.0;
    for (int n = 0; n < 2000; ++n) {
        const double H = 0.5 * p.Ms * std::sin(2.0 * M_PI * n / 256.0);
        const float out = h.process(static_cast<float>(H));
        CHECK(std::isfinite(out));
        CHECK(std::fabs(static_cast<double>(out)) <= bound + 1e-6);
        lastOut = static_cast<double>(out);
    }
    // After 2000 gentle samples the primitive must have recovered to the
    // physical (Ms-scale) range, not remained pinned at the guard bound.
    CHECK(std::fabs(lastOut) <= 1.5 * p.Ms);
}

}  // namespace

TEST_CASE("Hysteresis::process — T009 stiff-solver stability guard (FR-006/C3)") {
    SUBCASE("rk2: hot transient stays finite+bounded and recovers") {
        checkHotTransientRecovers(Solver::rk2);
    }
    SUBCASE("rk4: hot transient stays finite+bounded and recovers") {
        checkHotTransientRecovers(Solver::rk4);
    }
    SUBCASE("newtonRaphson: hot transient stays finite+bounded and recovers") {
        checkHotTransientRecovers(Solver::newtonRaphson);
    }

    SUBCASE("non-finite input never propagates NaN/Inf, for any solver") {
        const Solver solvers[] = {Solver::rk2, Solver::rk4, Solver::newtonRaphson};
        for (Solver solver : solvers) {
            Hysteresis h;
            h.prepare(48000.0);
            h.setParams(JAParams{});
            h.setSolver(solver);
            h.reset();

            const float badInputs[] = {
                std::numeric_limits<float>::quiet_NaN(),
                std::numeric_limits<float>::infinity(),
                -std::numeric_limits<float>::infinity(),
            };
            for (float bad : badInputs) {
                const float out = h.process(bad);
                CHECK(std::isfinite(out));
            }

            // State must not be poisoned: normal processing afterward stays
            // finite and settles back into the Ms-scale operating range.
            double lastOut = 0.0;
            for (int n = 0; n < 1000; ++n) {
                const double H = 0.5 * std::sin(2.0 * M_PI * n / 256.0);
                const float out = h.process(static_cast<float>(H));
                CHECK(std::isfinite(out));
                lastOut = static_cast<double>(out);
            }
            CHECK(std::fabs(lastOut) <= 1.5);
        }
    }
}
